// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal frontend for Horizon hosts (Switch 2 homebrew via libounce): picks a game
// from the SD card, runs it on the default NWindow with Vulkan and forwards player 1.
// Proof of concept: no configuration and no in-game UI.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <format>
#include <unwind.h>

#include "common/fs/fs.h"
#include "common/horizon/stats.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/perf_stats.h"
#include "eden_horizon/platform.h"
#include "eden_horizon/profiler.h"
#include "eden_horizon/status_overlay.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "input_common/main.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

extern "C" {
// Totals kept by dynarmic's Horizon code block.
extern unsigned long long eden_horizon_jit_publish_count;
extern unsigned long long eden_horizon_jit_publish_bytes;
extern unsigned long long eden_horizon_jit_publish_micros;
}

namespace {

class DummyContext final : public Core::Frontend::GraphicsContext {};

class EmuWindowHorizon final : public Core::Frontend::EmuWindow {
public:
    EmuWindowHorizon() {
        const Platform::WindowSize size = Platform::GetWindowSize();

        window_info.type = Core::Frontend::WindowSystemType::Horizon;
        window_info.render_surface = Platform::GetNativeWindow();
        NotifyClientAreaSizeChanged({size.width, size.height});
        UpdateCurrentFramebufferLayout(size.width, size.height);
    }

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<DummyContext>();
    }

    bool IsShown() const override {
        return true;
    }
};

void ApplyHostDefaults() {
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    // No fault handler on this host: guest memory goes through the page table.
    Settings::values.cpuopt_fastmem.SetValue(false);
    // A crash on this host loses buffered log lines; there is no other diagnostic.
    Settings::values.log_flush_line.SetValue(true);

    // Optional log filter for diagnosis, e.g. "*:Info Service.NVDRV:Debug".
    if (std::ifstream filter_file{"sdmc:/switch/eden/log_filter.txt"}) {
        std::string filter;
        std::getline(filter_file, filter);
        while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.back()))) {
            filter.pop_back();
        }
        if (!filter.empty()) {
            Settings::values.log_filter.SetValue(filter);
        }
    }

    // Player 1 is the console's own pad, fed through the virtual gamepad engine.
    auto& player = Settings::values.players.GetValue()[0];
    player.connected = true;
    player.controller_type = Settings::ControllerType::ProController;
}

constexpr const char* SettingsFile = "sdmc:/switch/eden/settings.txt";

/// The settings the picker's options screen offers, at their defaults.
std::vector<Platform::MenuOption> DefaultOptions() {
    return {
        {"resolution", "Internal resolution",
         "Scale applied to what the game renders. Below 1 is faster and blurrier.",
         {"0.5", "0.75", "1", "1.5", "2"}, 2, true},
        {"console", "Console mode",
         "What the game is told. Most games render 1080p docked and 720p handheld.",
         {"docked", "handheld"}, 1, true},
        {"show_fps", "Show FPS and speed",
         "Game frames per second and emulation speed, in the top left corner.",
         {"on", "off"}, 0},
        {"show_mode", "Show mode and resolution",
         "Console mode and internal resolution, under the FPS.",
         {"on", "off"}, 0},
        {"address_space", "Address space",
         "How much address space most games get. The emulator keeps a table that\n"
         "covers all of it in memory, so a smaller space leaves more memory for the\n"
         "game and for compiled code.\n\n"
         "compact  64 GiB, table of 128 MiB. The most free memory. A few games\n"
         "         abort when they start with it.\n"
         "game     256 GiB, table of 512 MiB. For games that fail in compact.\n"
         "full     512 GiB as on the console, table of 1 GiB. Long sessions may\n"
         "         run out of memory. Only if a game fails in the other two.\n\n"
         "Start with compact. Give a game that does not start its own value with Y.",
         {"compact", "game", "full"}, 0, true},
        {"gpu", "GPU emulation",
         "accurate follows the console's GPU closely. fast skips some of the waits\n"
         "between the game and the GPU, as Eden does on phones: quicker in heavy\n"
         "games, and it may break an effect here and there. fast also raises the\n"
         "graphics memory budget, keeping more textures cached.",
         {"accurate", "fast"}, 0, true},
        {"language", "Game language",
         "The language and region the games are told the console is set to.",
         {"en-US", "es-419", "es-ES", "fr", "de", "it", "pt-BR", "ja"}, 0},
        {"cpu_boost", "CPU boost",
         "Asks the console for its loading-screen CPU clock. Faster, at the cost of battery and heat.",
         {"off", "on"}, 1, true},
        {"jit_cache", "CPU code cache on disk",
         "Remembers the game code compiled in a session and compiles it at once on the next start, so it stutters less.",
         {"on", "off"}, 0},
        {"profiler", "Profiler (diagnostic)",
         "Logs every 10 seconds where each emulator thread spends its time. Slows the game.",
         {"off", "on"}, 0},
        {"shader_cache", "Shader cache on disk",
         "Keeps compiled shaders between sessions, so they stutter only once.",
         {"on", "off"}, 0},
    };
}

const std::string& OptionValue(const std::vector<Platform::MenuOption>& options,
                               std::string_view key) {
    static const std::string none;
    const auto it = std::ranges::find(options, key, &Platform::MenuOption::key);
    return it != options.end() ? it->choices[static_cast<std::size_t>(it->selected)] : none;
}

bool OptionIs(const std::vector<Platform::MenuOption>& options, std::string_view key,
              std::string_view value) {
    return OptionValue(options, key) == value;
}

/// The mode shown on screen, in the status font's capitals: "DOCKED 1X".
std::string StatusLine(const std::vector<Platform::MenuOption>& options) {
    std::string line = OptionValue(options, "console") + " " + OptionValue(options, "resolution") + "X";
    std::ranges::transform(line, line.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return line;
}

/// The game's file name without folder, extension or anything unfit for a file name.
std::string GameName(const std::string& game_path) {
    std::string name = game_path.substr(game_path.find_last_of('/') + 1);
    name = name.substr(0, name.find_last_of('.'));
    std::erase_if(name, [](unsigned char c) { return std::isalnum(c) == 0 && c != '_'; });
    return name.substr(0, 60);
}

std::string GameSettingsFile(const std::string& game_path) {
    return "sdmc:/switch/eden/games/" + GameName(game_path) + ".txt";
}

/// Reads "name=value" lines over the values `options` has.
void LoadOptions(std::vector<Platform::MenuOption>& options, const std::string& path = SettingsFile) {
    std::ifstream file{path};
    for (std::string line; std::getline(file, line);) {
        std::erase_if(line, [](unsigned char c) { return std::isspace(c) != 0; });
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        for (auto& option : options) {
            if (line.substr(0, equals) != option.key) {
                continue;
            }
            const auto choice = std::ranges::find(option.choices, line.substr(equals + 1));
            if (choice != option.choices.end()) {
                option.selected = static_cast<int>(choice - option.choices.begin());
            }
        }
    }
}

void SaveOptions(const std::vector<Platform::MenuOption>& options) {
    std::ofstream file{SettingsFile, std::ios::trunc};
    for (const auto& option : options) {
        file << option.key << '=' << option.choices[static_cast<std::size_t>(option.selected)]
             << std::endl;
    }
}

/// What a game inherits when it has no value of its own for an option.
constexpr const char* Inherit = "general";

/// The options a game can have of its own, each with "general" as its first choice and
/// set to what the game's file says.
std::vector<Platform::MenuOption> GameOptions(const std::vector<Platform::MenuOption>& general,
                                              const std::string& game_path) {
    std::vector<Platform::MenuOption> options;
    for (const auto& option : general) {
        if (option.per_game) {
            options.push_back(option);
            options.back().choices.insert(options.back().choices.begin(), Inherit);
            options.back().selected = 0;
        }
    }
    LoadOptions(options, GameSettingsFile(game_path));
    return options;
}

/// Writes only what the game does not inherit.
void SaveGameOptions(const std::vector<Platform::MenuOption>& options,
                     const std::string& game_path) {
    mkdir("sdmc:/switch/eden/games", 0777);
    const std::string path = GameSettingsFile(game_path);
    if (std::ranges::all_of(options, [](const auto& option) { return option.selected == 0; })) {
        std::remove(path.c_str());
        return;
    }
    std::ofstream file{path, std::ios::trunc};
    for (const auto& option : options) {
        if (option.selected != 0) {
            file << option.key << '=' << option.choices[static_cast<std::size_t>(option.selected)]
                 << std::endl;
        }
    }
}

/// The picker's screen for one game's options.
void EditGameOptions(const std::vector<Platform::MenuOption>& general,
                     const std::string& game_path) {
    std::vector<Platform::MenuOption> options = GameOptions(general, game_path);
    // Show what "general" stands for right now.
    for (auto& option : options) {
        option.choices[0] = std::string{Inherit} + " (" + OptionValue(general, option.key) + ")";
    }
    Platform::EditOptions(options, "options for " + GameName(game_path) + " only");
    for (auto& option : options) {
        option.choices[0] = Inherit;
    }
    SaveGameOptions(options, game_path);
}

/// The general options with the game's own values over them.
std::vector<Platform::MenuOption> EffectiveOptions(std::vector<Platform::MenuOption> general,
                                                   const std::string& game_path) {
    for (const auto& own : GameOptions(general, game_path)) {
        if (own.selected == 0) {
            continue;
        }
        const auto option = std::ranges::find(general, own.key, &Platform::MenuOption::key);
        option->selected = own.selected - 1;
        LOG_INFO(Frontend, "Option {} is this game's own", own.key);
    }
    return general;
}

void ApplyOptions(const std::vector<Platform::MenuOption>& options) {
    static constexpr std::pair<std::string_view, Settings::ResolutionSetup> resolutions[]{
        {"0.5", Settings::ResolutionSetup::Res1_2X}, {"0.75", Settings::ResolutionSetup::Res3_4X},
        {"1", Settings::ResolutionSetup::Res1X},     {"1.5", Settings::ResolutionSetup::Res3_2X},
        {"2", Settings::ResolutionSetup::Res2X},
    };
    for (const auto& option : options) {
        const std::string& choice = option.choices[static_cast<std::size_t>(option.selected)];
        if (option.key == "resolution") {
            for (const auto& [text, setup] : resolutions) {
                if (choice == text) {
                    Settings::values.resolution_setup.SetValue(setup);
                }
            }
        } else if (option.key == "console") {
            Settings::values.use_docked_mode.SetValue(choice == "handheld"
                                                          ? Settings::ConsoleMode::Handheld
                                                          : Settings::ConsoleMode::Docked);
        } else if (option.key == "address_space") {
            eden_horizon_compact_address_space = choice == "compact" ? 1 : choice == "game" ? 2 : 0;
        } else if (option.key == "gpu") {
            const bool fast = choice == "fast";
            Settings::values.gpu_accuracy.SetValue(fast ? Settings::GpuAccuracy::Low
                                                         : Settings::GpuAccuracy::High);
            Settings::values.use_reactive_flushing.SetValue(!fast);
            Settings::values.vram_usage_mode.SetValue(
                fast ? Settings::VramUsageMode::Aggressive
                     : Settings::VramUsageMode::Conservative);
        } else if (option.key == "language") {
            using Settings::Language, Settings::Region;
            static constexpr std::tuple<std::string_view, Language, Region> languages[]{
                {"en-US", Language::EnglishAmerican, Region::Usa},
                {"es-419", Language::SpanishLatin, Region::Usa},
                {"es-ES", Language::Spanish, Region::Europe},
                {"fr", Language::French, Region::Europe},
                {"de", Language::German, Region::Europe},
                {"it", Language::Italian, Region::Europe},
                {"pt-BR", Language::PortugueseBrazilian, Region::Usa},
                {"ja", Language::Japanese, Region::Japan},
            };
            for (const auto& [text, language, region] : languages) {
                if (choice == text) {
                    Settings::values.language_index.SetValue(language);
                    Settings::values.region_index.SetValue(region);
                }
            }
        } else if (option.key == "cpu_boost" && choice == "on") {
            LOG_INFO(Frontend, "CPU boost requested, result {:#x}", Platform::SetCpuBoost(true));
        } else if (option.key == "shader_cache") {
            Settings::values.use_disk_shader_cache.SetValue(choice == "on");
        }
        LOG_INFO(Frontend, "Option {}: {}", option.key, choice);
    }
    Settings::UpdateRescalingInfo();
}

/// Passes the console's pad to the emulated player 1.
void ForwardPad(InputCommon::VirtualGamepad& gamepad, const Platform::PadSnapshot& pad,
                Platform::PadSnapshot& previous) {
    using InputCommon::VirtualGamepad;
    const std::uint32_t changed = pad.buttons ^ previous.buttons;
    for (int button = 0; button < 16; ++button) {
        if (changed & (1U << button)) {
            gamepad.SetButtonState(0, button, (pad.buttons & (1U << button)) != 0);
        }
    }
    gamepad.SetStickPosition(0, VirtualGamepad::VirtualStick::Left, pad.left_x, pad.left_y);
    gamepad.SetStickPosition(0, VirtualGamepad::VirtualStick::Right, pad.right_x, pad.right_y);
    if (pad.has_motion) {
        gamepad.SetMotionState(0, pad.motion_delta_us, pad.gyro[0], pad.gyro[1], pad.gyro[2],
                               pad.accel[0], pad.accel[1], pad.accel[2]);
    }
    previous = pad;
}

Platform::Vibration ToPlatform(const Core::HID::VibrationValue& value) {
    return {value.low_amplitude, value.low_frequency, value.high_amplitude, value.high_frequency};
}

/// Passes the rumble the game last asked of player 1 to the console's controller.
class VibrationForwarder {
public:
    explicit VibrationForwarder(Core::HID::HIDCore& hid_core)
        : controller{hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1)} {}

    ~VibrationForwarder() {
        Platform::SetVibration(ToPlatform(Core::HID::DEFAULT_VIBRATION_VALUE),
                               ToPlatform(Core::HID::DEFAULT_VIBRATION_VALUE));
    }

    void Update() {
        auto left = controller->GetActualVibrationValue(Core::HID::DeviceIndex::Left);
        auto right = controller->GetActualVibrationValue(Core::HID::DeviceIndex::Right);
        if (left == last_left && right == last_right) {
            return;
        }
        Platform::SetVibration(ToPlatform(left), ToPlatform(right));
        last_left = left;
        last_right = right;
    }

private:
    Core::HID::EmulatedController* controller;
    Core::HID::VibrationValue last_left{Core::HID::DEFAULT_VIBRATION_VALUE};
    Core::HID::VibrationValue last_right{Core::HID::DEFAULT_VIBRATION_VALUE};
};

/// Logs the call stack as offsets into eden-horizon.elf (resolve with addr2line).
void LogBacktrace() {
    struct State {
        std::array<std::uintptr_t, 48> frames;
        std::size_t count;
    } state{};
    _Unwind_Backtrace(
        [](_Unwind_Context* context, void* arg) -> _Unwind_Reason_Code {
            auto& s = *static_cast<State*>(arg);
            if (s.count == s.frames.size()) {
                return _URC_END_OF_STACK;
            }
            s.frames[s.count++] = _Unwind_GetIP(context);
            return _URC_NO_REASON;
        },
        &state);
    const std::uintptr_t base = Platform::GetModuleBase();
    std::string line;
    for (std::size_t i = 0; i < state.count; ++i) {
        line += std::format(" {:#x}", state.frames[i] - base);
    }
    LOG_CRITICAL(Frontend, "Backtrace (ELF offsets):{}", line);
}

/// Uncaught exceptions end the process silently on this host; log them first.
[[noreturn]] void OnTerminate() {
    try {
        if (const std::exception_ptr current = std::current_exception()) {
            std::rethrow_exception(current);
        }
        LOG_CRITICAL(Frontend, "std::terminate called without an active exception");
    } catch (const std::exception& e) {
        LOG_CRITICAL(Frontend, "Uncaught exception: {}", e.what());
    } catch (...) {
        LOG_CRITICAL(Frontend, "Uncaught exception of unknown type");
    }
    LogBacktrace();
    Common::Log::Stop();
    Platform::ExitProcess();
}

/// Marks progress in the log with the process memory use, to locate startup failures.
void LogStage(const char* stage) {
    const Platform::MemoryStats memory = Platform::GetMemoryStats();
    LOG_INFO(Frontend, "Stage: {} (memory {} / {} MiB)", stage, memory.used >> 20,
             memory.total >> 20);
}

/// Samples the game's speed about once a second for the on-screen status, and logs
/// the average with what the JIT cost about every ten seconds.
class PerformanceMonitor {
public:
    PerformanceMonitor(Core::System& system_, bool show_fps_, std::string mode_line_)
        : system{system_}, show_fps{show_fps_}, mode_line{std::move(mode_line_)} {
        UpdateOverlay(0.0, 0.0);
    }

    ~PerformanceMonitor() {
        StatusOverlay::Show({});
    }

    void Sample() {
        const auto stats = system.GetAndResetPerfStats();
        UpdateOverlay(stats.average_game_fps, stats.emulation_speed);
        speed_sum += stats.emulation_speed;
        fps_sum += stats.average_game_fps;
        frametime_sum += stats.frametime;
        if (++samples < 10) {
            return;
        }
        const unsigned long long count = eden_horizon_jit_publish_count;
        const unsigned long long bytes = eden_horizon_jit_publish_bytes;
        const unsigned long long micros = eden_horizon_jit_publish_micros;
        const unsigned long long compile = eden_horizon_jit_compile_micros;
        LOG_INFO(Frontend,
                 "Performance: speed {:.0f}%, game {:.1f} fps, frame {:.1f} ms; JIT published {} "
                 "times, {} KiB, {} ms; JIT busy {} ms",
                 speed_sum * 100.0 / samples, fps_sum / samples, frametime_sum * 1000.0 / samples,
                 count - last_count, (bytes - last_bytes) >> 10, (micros - last_micros) / 1000,
                 (compile - last_compile) / 1000);
        LOG_INFO(Frontend,
                 "Memory detail: textures {} MiB, buffers {} MiB, JIT blocks {}, cache clears {} ({} "
                 "for heap, {} blocks evicted first), warmed {} blocks in {} ms, {} textures evicted",
                 eden_horizon_texture_bytes >> 20, eden_horizon_buffer_bytes >> 20,
                 eden_horizon_jit_blocks, eden_horizon_jit_cache_clears,
                 eden_horizon_jit_heap_clears, eden_horizon_jit_partial_evictions,
                 eden_horizon_jit_warm_blocks, eden_horizon_jit_warm_micros / 1000,
                 eden_horizon_texture_evictions);
        last_count = count;
        last_bytes = bytes;
        last_micros = micros;
        last_compile = compile;
        speed_sum = fps_sum = frametime_sum = 0.0;
        samples = 0;
    }

private:
    void UpdateOverlay(double fps, double speed) {
        std::string text;
        if (show_fps) {
            text = std::format("{:.0f} FPS {:.0f}%", fps, speed * 100.0);
        }
        if (!mode_line.empty()) {
            text += text.empty() ? "" : std::string(1, char{10});
            text += mode_line;
        }
        StatusOverlay::Show(text);
    }

    Core::System& system;
    bool show_fps;
    std::string mode_line;
    double speed_sum{}, fps_sum{}, frametime_sum{};
    int samples{};
    unsigned long long last_count{}, last_bytes{}, last_micros{}, last_compile{};
};

/// Writes the profiler's busiest places of the last period, if it is running.
void LogProfile() {
    std::array<Profiler::Entry, 40> entries;
    const std::size_t count = Profiler::TakeReport(entries);
    for (std::size_t i = 0; i < count; ++i) {
        const Profiler::Entry& entry = entries[i];
        LOG_INFO(Frontend, "Profile {:<14} {:>5}/{:<5} {} {:#x} lr {:#x}", entry.thread,
                 entry.samples, entry.thread_samples, entry.in_module ? "elf" : "abs", entry.pc,
                 entry.lr);
    }
}

/// Largest motion readings since the last report, to check the sensor's axes and units.
struct MotionPeaks {
    std::array<float, 3> gyro{};
    std::array<float, 3> accel{};

    void Add(const Platform::PadSnapshot& pad) {
        if (!pad.has_motion) {
            return;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            gyro[i] = std::max(gyro[i], std::abs(pad.gyro[i]));
            accel[i] = std::max(accel[i], std::abs(pad.accel[i]));
        }
        seen = true;
    }

    void LogAndReset() {
        if (seen) {
            LOG_INFO(Frontend, "Motion peaks: gyro {:.2f} {:.2f} {:.2f}, accel {:.2f} {:.2f} {:.2f}",
                     gyro[0], gyro[1], gyro[2], accel[0], accel[1], accel[2]);
        } else {
            LOG_INFO(Frontend, "Motion: no sensor readings");
        }
        *this = {};
    }

    bool seen{};
};

} // namespace

int main(int argc, char** argv) {
    // Eden derives its data, config and cache directories from $HOME.
    setenv("HOME", "sdmc:/switch/eden", 0);

    ApplyHostDefaults();

    void(Common::FS::CreateDirs(Common::FS::GetEdenPath(Common::FS::EdenPath::PlayTimeDir)));
    // The log backend creates only the last directory of the log path.
    void(Common::FS::CreateDirs(Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir)));

    Common::Log::Initialize();
    Common::Log::Start();
    std::set_terminate(OnTerminate);
    LOG_INFO(Frontend, "CPU cores available to this process: mask {:#x}", Platform::GetCoreMask());
    Common::Log::Stop(); // flush before the optional fault below
    Platform::RunExceptionTestIfRequested();
    LOG_INFO(Frontend, "Eden {} | {}-{} (Horizon host)", std::string_view{Common::g_build_name},
             std::string_view{Common::g_scm_branch}, std::string_view{Common::g_scm_desc});

    std::vector<Platform::MenuOption> options = DefaultOptions();
    LoadOptions(options);
    const std::string game_path =
        argc > 1 ? argv[1]
                 : Platform::BrowseForGame(options, [&options](const std::string& path) {
                       EditGameOptions(options, path);
                   });
    SaveOptions(options);
    if (!game_path.empty()) {
        options = EffectiveOptions(std::move(options), game_path);
    }
    ApplyOptions(options);
    if (game_path.empty()) {
        LOG_INFO(Frontend, "No game chosen");
        Common::Log::Stop();
        return 0;
    }
    LOG_INFO(Frontend, "Booting {}", game_path);
    if (OptionIs(options, "jit_cache", "on")) {
        // Named after the game file: the cores' JITs are made before the title is known.
        const std::string name = GameName(game_path);
        mkdir("sdmc:/switch/eden/jit", 0777);
        std::snprintf(eden_horizon_jit_cache_prefix, sizeof(eden_horizon_jit_cache_prefix),
                      "sdmc:/switch/eden/jit/%.60s_%s", name.c_str(),
                      OptionValue(options, "address_space").c_str());
    }
    // Tuning aid: a number in this file replaces how far the JIT compiles ahead.
    if (std::ifstream ahead{"sdmc:/switch/eden/jit_ahead.txt"}; ahead) {
        int blocks = 0;
        if (ahead >> blocks && blocks >= 0 && blocks <= 64) {
            eden_horizon_jit_blocks_ahead = blocks;
        }
    }
    LOG_INFO(Frontend, "JIT cache '{}', {} blocks ahead", eden_horizon_jit_cache_prefix,
             eden_horizon_jit_blocks_ahead);
    LogStage("create system");

    auto system = std::make_unique<Core::System>();
    auto input_subsystem = std::make_unique<InputCommon::InputSubsystem>();
    LogStage("initialize system");
    system->Initialize();
    system->ApplySettings();
    LogStage("initialize input");
    input_subsystem->Initialize();
    system->HIDCore().ReloadInputDevices();
    LogStage("create filesystem");

    auto emu_window = std::make_unique<EmuWindowHorizon>();

    system->SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system->SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system->GetFileSystemController().CreateFactories(*system->GetFilesystem());
    system->GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    LogStage("load game");
    const Core::SystemResultStatus load_result =
        system->Load(*emu_window, game_path, load_parameters);
    if (load_result != Core::SystemResultStatus::Success) {
        LOG_CRITICAL(Frontend, "Failed to load {}: status {}", game_path,
                     static_cast<u32>(load_result));
        Common::Log::Stop();
        // The system already started host threads; see Platform::ExitProcess.
        Platform::ExitProcess();
    }

    LogStage("start GPU");
    system->GPU().Start();
    system->GetCpuManager().OnGpuReady();
    if (Settings::values.use_disk_shader_cache.GetValue()) {
        LogStage("load shader cache");
        system->Renderer().ReadRasterizer()->LoadDiskResources(
            system->GetApplicationProcessProgramID(), std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t, size_t) {});
    }
    system->RegisterExitCallback([] {});
    LogStage("run");
    {
        std::string sizes;
        const int count = std::min(eden_horizon_large_mmap_count, 64);
        for (int i = 0; i < count; ++i) {
            sizes += std::format(" {}", eden_horizon_large_mmaps[i] >> 20);
        }
        LOG_INFO(Frontend, "Large mappings so far, in MiB:{}", sizes);
    }
    void(system->Run());
    LogStage("running");

    Platform::InitializeInput();
    Platform::PadSnapshot pad{};
    Platform::PadSnapshot previous_pad{};
    {
        VibrationForwarder vibration{system->HIDCore()};
        MotionPeaks motion_peaks;
        PerformanceMonitor performance{*system, OptionIs(options, "show_fps", "on"),
                                       OptionIs(options, "show_mode", "on")
                                           ? StatusLine(options)
                                           : std::string{}};
        if (OptionIs(options, "profiler", "on")) {
            Profiler::Start();
        }
        u32 iterations = 0;
        while (Platform::PumpEvents(pad)) {
            ForwardPad(*input_subsystem->GetVirtualGamepad(), pad, previous_pad);
            vibration.Update();
            motion_peaks.Add(pad);
            // Roughly every 10 seconds: memory is the scarce resource on this host.
            ++iterations;
            if (iterations % 250 == 0) {
                performance.Sample();
            }
            if (iterations % 2500 == 0) {
                LogStage("playing");
                motion_peaks.LogAndReset();
                LogProfile();
            }
        }
    }

    Profiler::Stop();
    LogStage("shut down");
    Platform::SetCpuBoost(false);
    system->Pause();
    LOG_INFO(Frontend, "JIT cache: saved {} blocks", eden_horizon_jit_save_caches());
    system->ShutdownMainProcess();
    // The emulated controllers hold callbacks into the input engines: release them first.
    system->HIDCore().UnloadInputDevices();
    emu_window.reset();
    system.reset();
    input_subsystem->Shutdown();
    LOG_INFO(Frontend, "Shut down cleanly");
    Common::Log::Stop();
    return 0;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
