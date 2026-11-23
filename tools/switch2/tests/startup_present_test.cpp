// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>

#include "eden_horizon/startup_present.h"

int main() {
    int attempts = 0;
    assert(Startup::PresentWaitingCard([&] { return ++attempts == 1; }));
    assert(attempts == 1);
    attempts = 0;
    assert(Startup::PresentWaitingCard([&] { return ++attempts == 2; }));
    assert(attempts == 2);
    attempts = 0;
    assert(!Startup::PresentWaitingCard([&] { ++attempts; return false; }));
    assert(attempts == 2);
}
