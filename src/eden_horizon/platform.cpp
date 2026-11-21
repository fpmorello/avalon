// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_horizon/platform.h"
#include "eden_horizon/version.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include <dirent.h>
#include <strings.h>

#include <switch.h>
#include <switch2/memory/heap.h>

// Linker-provided start of this module, to turn crash addresses into ELF offsets.
extern "C" char __start__[];

// libnx calls this on a CPU exception in the process, on its own stack. Writes a
// report that can be symbolized with addr2line on eden-horizon.elf, then exits.
extern "C" {
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    const u64 base = reinterpret_cast<u64>(__start__);
    std::FILE* const file = std::fopen("sdmc:/switch/eden/crash.txt", "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%s\n", EDEN_HORIZON_VERSION);
    std::fprintf(file, "error_desc: 0x%x\n", ctx->error_desc);
    std::fprintf(file, "esr: 0x%x\n", ctx->esr);
    std::fprintf(file, "far: 0x%lx\n", ctx->far.x);
    std::fprintf(file, "module base: 0x%lx\n", base);
    std::fprintf(file, "pc: 0x%lx (offset 0x%lx)\n", ctx->pc.x, ctx->pc.x - base);
    std::fprintf(file, "lr: 0x%lx (offset 0x%lx)\n", ctx->lr.x, ctx->lr.x - base);
    std::fprintf(file, "sp: 0x%lx\n", ctx->sp.x);
    std::fprintf(file, "fp: 0x%lx\n", ctx->fp.x);
    for (int i = 0; i < 29; ++i) {
        std::fprintf(file, "x%d: 0x%lx\n", i, ctx->cpu_gprs[i].x);
    }
    std::fflush(file);

    // Return addresses saved along the frame pointer chain.
    u64 fp = ctx->fp.x;
    for (int depth = 0; depth < 32 && fp != 0 && (fp & 0xF) == 0; ++depth) {
        MemoryInfo info;
        u32 page_info;
        if (R_FAILED(svcQueryMemory(&info, &page_info, fp)) || !(info.perm & Perm_R)) {
            break;
        }
        const u64* const frame = reinterpret_cast<const u64*>(fp);
        std::fprintf(file, "frame %d: 0x%lx (offset 0x%lx)\n", depth, frame[1], frame[1] - base);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
    std::fclose(file);
}
}

namespace Platform {

namespace {
PadState pad;
} // namespace

void* GetNativeWindow() {
    return nwindowGetDefault();
}

WindowSize GetWindowSize() {
    u32 width = 1280, height = 720;
    nwindowGetDimensions(nwindowGetDefault(), &width, &height);
    return {width, height};
}

MemoryStats GetMemoryStats() {
    // The loader maps the whole heap up front, so the kernel's figures never change;
    // ask the allocator instead.
    MemoryStats stats{};
    stats.total = heapGetTotalSize();
    stats.used = stats.total - heapGetFreeSize();
    return stats;
}

std::uint64_t GetCoreMask() {
    u64 mask = 0;
    svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    return mask;
}

void RunExceptionTestIfRequested() {
    constexpr const char* RequestFile = "sdmc:/switch/eden/test_exception.txt";
    std::FILE* const request = std::fopen(RequestFile, "r");
    if (request == nullptr) {
        return;
    }
    std::fclose(request);
    std::remove(RequestFile);
    std::remove("sdmc:/switch/eden/crash.txt");

    volatile int* const invalid = nullptr;
    *invalid = 1;
    ExitProcess();
}

std::uintptr_t GetModuleBase() {
    return reinterpret_cast<std::uintptr_t>(__start__);
}

std::uint32_t SetCpuBoost(bool enable) {
    return appletSetCpuBoostMode(enable ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

void ExitProcess() {
    SetCpuBoost(false);
    svcExitProcess();
    __builtin_unreachable();
}

namespace {

// The motion sensor and rumble motors belong to a controller style (handheld, Pro
// Controller, Joy-Con pair...), which changes when controllers are attached or removed.
struct PadDevices {
    HidNpadIdType id{};
    u32 style{};
    bool valid{};
    HidSixAxisSensorHandle sensors[2]{};
    int sensor_count{};
    HidVibrationDeviceHandle motors[2]{};
    bool has_motors{};
    u64 last_motion_tick{};
};
PadDevices devices;

void AcquireDevices(HidNpadIdType id, u32 style_set) {
    for (int i = 0; i < devices.sensor_count; ++i) {
        hidStopSixAxisSensor(devices.sensors[i]);
    }
    devices = {};
    devices.id = id;
    devices.style = style_set;
    devices.valid = true;

    HidNpadStyleTag style{};
    int sensor_count = 1;
    if (style_set & HidNpadStyleTag_NpadHandheld) {
        style = HidNpadStyleTag_NpadHandheld;
    } else if (style_set & HidNpadStyleTag_NpadFullKey) {
        style = HidNpadStyleTag_NpadFullKey;
    } else if (style_set & HidNpadStyleTag_NpadJoyDual) {
        style = HidNpadStyleTag_NpadJoyDual;
        sensor_count = 2;
    } else {
        return;
    }

    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(devices.sensors, sensor_count, id, style))) {
        devices.sensor_count = sensor_count;
        for (int i = 0; i < sensor_count; ++i) {
            hidStartSixAxisSensor(devices.sensors[i]);
        }
    }
    devices.has_motors = R_SUCCEEDED(hidInitializeVibrationDevices(devices.motors, 2, id, style));
}

} // namespace

void InitializeInput() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    devices = {};
}

void SetVibration(const Vibration& left, const Vibration& right) {
    if (!devices.has_motors) {
        return;
    }
    const HidVibrationValue values[2]{
        {left.low_amplitude, left.low_frequency, left.high_amplitude, left.high_frequency},
        {right.low_amplitude, right.low_frequency, right.high_amplitude, right.high_frequency},
    };
    hidSendVibrationValues(devices.motors, values, 2);
}

bool PumpEvents(PadSnapshot& out) {
    if (!appletMainLoop()) {
        return false;
    }
    padUpdate(&pad);

    const HidNpadIdType id = padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1;
    const u32 style_set = padGetStyleSet(&pad);
    if (!devices.valid || devices.id != id || devices.style != style_set) {
        AcquireDevices(id, style_set);
    }

    out.has_motion = false;
    HidSixAxisSensorState motion{};
    // With a Joy-Con pair, the right one is the pointer.
    if (devices.sensor_count > 0 &&
        hidGetSixAxisSensorStates(devices.sensors[devices.sensor_count - 1], &motion, 1) > 0) {
        const u64 tick = armGetSystemTick();
        out.has_motion = true;
        out.motion_delta_us =
            devices.last_motion_tick != 0 ? armTicksToNs(tick - devices.last_motion_tick) / 1000 : 0;
        devices.last_motion_tick = tick;
        out.gyro[0] = motion.angular_velocity.x;
        out.gyro[1] = motion.angular_velocity.y;
        out.gyro[2] = motion.angular_velocity.z;
        out.accel[0] = motion.acceleration.x;
        out.accel[1] = motion.acceleration.y;
        out.accel[2] = motion.acceleration.z;
    }

    const u64 held = padGetButtons(&pad);
    if ((held & HidNpadButton_Plus) && (held & HidNpadButton_Minus)) {
        return false;
    }
    // libnx's first 16 button bits are in VirtualButton order.
    out.buttons = static_cast<std::uint32_t>(held & 0xFFFF);
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);
    constexpr float StickMax = static_cast<float>(JOYSTICK_MAX);
    out.left_x = static_cast<float>(left.x) / StickMax;
    out.left_y = static_cast<float>(left.y) / StickMax;
    out.right_x = static_cast<float>(right.x) / StickMax;
    out.right_y = static_cast<float>(right.y) / StickMax;
    svcSleepThread(4'000'000);
    return true;
}

namespace {

constexpr const char* LastFolderFile = "sdmc:/switch/eden/last_folder.txt";
constexpr const char* RootFolder = "sdmc:/";
constexpr int VisibleRows = 36;
constexpr std::size_t NameColumns = 74;

struct Entry {
    std::string name;
    bool is_folder;
};

bool IsGameFile(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string extension = name.substr(dot);
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".nsp" || extension == ".xci" || extension == ".nca" ||
           extension == ".nro" || extension == ".nso";
}

std::vector<Entry> ListFolder(const std::string& folder) {
    std::vector<Entry> entries;
    DIR* const dir = opendir(folder.c_str());
    if (dir == nullptr) {
        return entries;
    }
    while (const dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name.empty() || name[0] == '.') {
            continue;
        }
        const bool is_folder = item->d_type == DT_DIR;
        if (is_folder || IsGameFile(name)) {
            entries.push_back({name, is_folder});
        }
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_folder != b.is_folder) {
            return a.is_folder;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return entries;
}

std::string ParentFolder(const std::string& folder) {
    if (folder == RootFolder) {
        return folder;
    }
    // Folders end in '/': drop the last component.
    const std::size_t slash = folder.rfind('/', folder.size() - 2);
    return folder.substr(0, slash + 1);
}

std::string ReadLastFolder() {
    std::string folder;
    if (std::FILE* const file = std::fopen(LastFolderFile, "r")) {
        char buffer[512];
        if (std::fgets(buffer, sizeof(buffer), file) != nullptr) {
            folder = buffer;
            while (!folder.empty() && (folder.back() == '\n' || folder.back() == '\r')) {
                folder.pop_back();
            }
        }
        std::fclose(file);
    }
    if (DIR* const dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        closedir(dir);
        return folder;
    }
    return RootFolder;
}

void WriteLastFolder(const std::string& folder) {
    if (std::FILE* const file = std::fopen(LastFolderFile, "w")) {
        std::fputs(folder.c_str(), file);
        std::fclose(file);
    }
}

void Draw(const std::string& folder, const std::vector<Entry>& entries, int selected, int top) {
    std::printf("\x1b[2J\x1b[1;1H");
    std::printf("\x1b[1mEden\x1b[0m - choose a game\n");
    std::printf("%.*s\n\n", static_cast<int>(NameColumns + 4), folder.c_str());
    if (entries.empty()) {
        std::printf("  (no folders or games here)\n");
    }
    const int count = static_cast<int>(entries.size());
    for (int row = top; row < count && row < top + VisibleRows; ++row) {
        const Entry& entry = entries[static_cast<std::size_t>(row)];
        std::string name = entry.name.substr(0, NameColumns);
        if (entry.is_folder) {
            name += '/';
        }
        if (row == selected) {
            std::printf("\x1b[7m> %s\x1b[0m\n", name.c_str());
        } else {
            std::printf("  %s\n", name.c_str());
        }
    }
    std::printf("\x1b[43;1H");
    if (count > VisibleRows) {
        std::printf("%d-%d of %d\n", top + 1, std::min(count, top + VisibleRows), count);
    } else {
        std::printf("\n");
    }
    std::printf("Up/Down: move  A: open  B: back  X: options  Y: this game's options  +: exit");
}

void DrawOptions(const std::vector<MenuOption>& options, int selected, const std::string& title) {
    std::printf("\x1b[2J\x1b[1;1H");
    std::printf("\x1b[1mEden %s\x1b[0m - %.60s\n\n\n", EDEN_HORIZON_BUILD_ID, title.c_str());
    for (int row = 0; row < static_cast<int>(options.size()); ++row) {
        const MenuOption& option = options[static_cast<std::size_t>(row)];
        const char* const choice = option.choices[static_cast<std::size_t>(option.selected)].c_str();
        if (row == selected) {
            std::printf("\x1b[7m> %-28s < %s >\x1b[0m\n", option.label.c_str(), choice);
        } else {
            std::printf("  %-28s   %s\n", option.label.c_str(), choice);
        }
    }
    if (selected < static_cast<int>(options.size())) {
        std::printf("\x1b[22;1H%s", options[static_cast<std::size_t>(selected)].help.c_str());
    }
    std::printf("\x1b[44;1HUp/Down: move   Left/Right: change   B: back");
}

/// Runs the options screen until the user leaves it. Returns false if the applet must exit.
bool RunOptionsScreen(std::vector<MenuOption>& options, const std::string& title) {
    if (options.empty()) {
        return true;
    }
    int selected = 0;
    bool redraw = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        if (pressed & (HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y)) {
            return true;
        }
        const int count = static_cast<int>(options.size());
        if (pressed & HidNpadButton_AnyUp) {
            selected = (selected + count - 1) % count;
            redraw = true;
        }
        if (pressed & HidNpadButton_AnyDown) {
            selected = (selected + 1) % count;
            redraw = true;
        }
        MenuOption& option = options[static_cast<std::size_t>(selected)];
        const int choices = static_cast<int>(option.choices.size());
        if (pressed & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight | HidNpadButton_A)) {
            const int step = (pressed & HidNpadButton_AnyLeft) ? choices - 1 : 1;
            option.selected = (option.selected + step) % choices;
            redraw = true;
        }
        if (redraw) {
            DrawOptions(options, selected, title);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }
    return false;
}

} // namespace

bool EditOptions(std::vector<MenuOption>& options, const std::string& title) {
    return RunOptionsScreen(options, title);
}

std::string BrowseForGame(std::vector<MenuOption>& options,
                          const std::function<void(const std::string&)>& edit_game) {
    consoleInit(nullptr);
    InitializeInput();

    std::string folder = ReadLastFolder();
    std::vector<Entry> entries = ListFolder(folder);
    int selected = 0;
    int top = 0;
    bool redraw = true;
    std::string chosen;

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        const int count = static_cast<int>(entries.size());

        if (pressed & HidNpadButton_Plus) {
            break;
        }
        if (pressed & HidNpadButton_X) {
            if (!RunOptionsScreen(options, "options for every game")) {
                break;
            }
            redraw = true;
        }
        if ((pressed & HidNpadButton_Y) && count > 0 &&
            !entries[static_cast<std::size_t>(selected)].is_folder) {
            edit_game(folder + entries[static_cast<std::size_t>(selected)].name);
            redraw = true;
        }
        if (pressed & (HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft |
                       HidNpadButton_AnyRight)) {
            if (pressed & HidNpadButton_AnyUp) {
                --selected;
            }
            if (pressed & HidNpadButton_AnyDown) {
                ++selected;
            }
            if (pressed & HidNpadButton_AnyLeft) {
                selected -= VisibleRows;
            }
            if (pressed & HidNpadButton_AnyRight) {
                selected += VisibleRows;
            }
            selected = std::clamp(selected, 0, std::max(count - 1, 0));
            redraw = true;
        }

        const bool open = (pressed & HidNpadButton_A) && count > 0;
        if (open && !entries[static_cast<std::size_t>(selected)].is_folder) {
            chosen = folder + entries[static_cast<std::size_t>(selected)].name;
            WriteLastFolder(folder);
            break;
        }
        if (open || (pressed & HidNpadButton_B)) {
            folder = open ? folder + entries[static_cast<std::size_t>(selected)].name + "/"
                          : ParentFolder(folder);
            entries = ListFolder(folder);
            selected = 0;
            redraw = true;
        }

        if (redraw) {
            top = std::clamp(top, std::max(selected - VisibleRows + 1, 0), selected);
            Draw(folder, entries, selected, top);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }

    // Give the screen back before Vulkan takes the window.
    consoleExit(nullptr);
    return chosen;
}

} // namespace Platform
