// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <ratio>
#include <switch.h>

#include "eden_horizon/clock_probe.h"

std::int64_t ClockProbe::ReadCandidate() {
    using GuestTicksPerNanosecond = std::ratio<GuestTicksPerSecond, std::nano::den>;
    const auto ns = armTicksToNs(armGetSystemTick());
    return ns * GuestTicksPerNanosecond::num / GuestTicksPerNanosecond::den;
}

namespace {

constexpr auto ReportPath = "sdmc:/switch/avalon-clock-probe.txt";

bool SaveReport(const std::string& report) {
    auto* file = std::fopen(ReportPath, "wb");
    if (!file) {
        return false;
    }
    const bool written = std::fwrite(report.data(), 1, report.size(), file) == report.size();
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}

} // namespace

int main() {
    consoleInit(nullptr);
    const auto report = ClockProbe::RunProbe();
    std::fputs(report.c_str(), stdout);
    std::printf("\n%s %s\nPress + to exit.\n", SaveReport(report) ? "Saved:" : "Could not save:",
                ReportPath);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
