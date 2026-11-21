// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// libnx access for the frontend. <switch.h> declares global names that collide with
// Eden's (Service, Result, Event...), so it is only included in platform.cpp.
namespace Platform {

struct WindowSize {
    std::uint32_t width;
    std::uint32_t height;
};

/// The default NWindow*, for VK_NN_vi_surface.
void* GetNativeWindow();
WindowSize GetWindowSize();

struct MemoryStats {
    std::uint64_t total; ///< Bytes of heap the allocator manages
    std::uint64_t used;  ///< Bytes of it in use (an upper bound)
};

MemoryStats GetMemoryStats();

/// Bit N set: this process may run threads on CPU core N.
std::uint64_t GetCoreMask();

/// Diagnostic, run when sdmc:/switch/avalon/test_exception.txt exists (the file is
/// removed first): faults on purpose to learn whether the exception handler above
/// receives CPU exceptions in this environment. It writes crash.txt if it does.
/// Does not return when the test runs.
void RunExceptionTestIfRequested();

/// Load address of this program, to turn code addresses into ELF offsets.
std::uintptr_t GetModuleBase();

/// Ends the whole process at once. Returning to the homebrew loader after a failure
/// would leave emulation threads running inside the process it reuses for the next
/// program, which then crashes; this goes back to the HOME menu instead.
[[noreturn]] void ExitProcess();

/// Asks for the CPU clock games get on loading screens (am's boost mode), or the normal one.
/// Returns the system's result code, zero for success.
std::uint32_t SetCpuBoost(bool enable);

/// Player 1's pad. Bit N of `buttons` is InputCommon::VirtualGamepad::VirtualButton N
/// (A, B, X, Y, stick L, stick R, L, R, ZL, ZR, +, -, left, up, right, down).
struct PadSnapshot {
    std::uint32_t buttons;
    float left_x, left_y;   ///< -1..1, up and right positive
    float right_x, right_y;

    /// Motion sensor, in the console's own units, which are the emulated console's too:
    /// angular velocity in turns per second and acceleration in G.
    bool has_motion;
    std::uint64_t motion_delta_us; ///< Time since the previous reading
    float gyro[3];
    float accel[3];
};

/// One rumble motor's state: amplitudes 0..1, frequencies in Hz.
struct Vibration {
    float low_amplitude, low_frequency;
    float high_amplitude, high_frequency;
};

void InitializeInput();

/// Drives the left and right rumble motors of player 1's controller.
void SetVibration(const Vibration& left, const Vibration& right);

/// Pumps applet messages and reads the pad, then waits a few milliseconds.
/// Returns false when the applet must exit or the user asked to quit (+ and - held).
bool PumpEvents(PadSnapshot& pad);

/// A setting shown on the picker's options screen.
struct MenuOption {
    std::string key;                  ///< Name in the settings file
    std::string label;
    std::string help;                 ///< Shown while the option is selected
    std::vector<std::string> choices; ///< Values, as written to the settings file
    int selected;
    bool per_game = false;            ///< Whether a game can have its own value
};

/// Shows `options` under `title` until the user leaves the screen, changing them in place.
/// Returns false if the applet must exit.
bool EditOptions(std::vector<MenuOption>& options, const std::string& title);

/// Text-mode file picker over the SD card, shown before the emulator owns the screen.
/// Lists folders and game files, starting in the folder last used.
/// Returns the chosen file, or an empty string if the user cancelled.
/// X opens a screen to change `options`; the caller reads the choices back afterwards.
/// Y on a game calls `edit_game` with its path, for the caller to show that game's settings.
std::string BrowseForGame(std::vector<MenuOption>& options,
                          const std::function<void(const std::string&)>& edit_game);

} // namespace Platform
