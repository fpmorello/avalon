// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include "eden_horizon/clock_probe.h"

int main() {
    ClockProbe::Agreement agreement;
    agreement.Add(100, 101, 102);
    agreement.Add(130, 131, 132);
    assert(agreement.samples == 2 && agreement.outside == 0);
    assert(agreement.reversed_reference == 0 && agreement.reversed_candidate == 0);
    assert(agreement.reference_delta == 30 && agreement.candidate_delta == 30);
    agreement.Add(140, 120, 141);
    agreement.Add(150, 190, 149);
    assert(agreement.samples == 4 && agreement.outside == 2);
    assert(agreement.reversed_reference == 1 && agreement.reversed_candidate == 1);
    assert(agreement.reference_delta == 50 && agreement.candidate_delta == 89);
    assert(ClockProbe::Median(std::array{9, 4, 1, 7, 2}) == 4);
}
