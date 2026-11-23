// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace Dynarmic::Backend::Arm64 {

// Oaknut does not expose the scalar half-precision FMADD form.
constexpr std::uint32_t EncodeFMADD16(unsigned rd, unsigned rn, unsigned rm, unsigned ra) {
    return 0x1fc00000 | (rm << 16) | (ra << 10) | (rn << 5) | rd;
}

} // namespace Dynarmic::Backend::Arm64
