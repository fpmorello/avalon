// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <cstddef>

namespace Dynarmic::Backend::Arm64 {

/// Number of oldest blocks to evict from the JIT bookkeeping when the host heap runs
/// low (Switch 2: the heap is shared by the emulator, the guest and the GPU).
///
/// Evicts a quarter of the live blocks — enough bookkeeping (per-block map nodes and
/// reference vectors) to relieve real pressure, small enough that warm code mostly
/// survives — but never less than a floor that actually frees something: a quarter of
/// the trigger floor (16384 blocks) would be a pointless 4096. The full cache clear
/// remains the fallback if evictions do not relieve the heap (see AddressSpace).
constexpr std::size_t EvictBlockCount(std::size_t live_blocks) noexcept {
    /// The heap-pressure trigger only fires with at least this many live blocks.
    constexpr std::size_t TriggerFloor = 16384;
    /// Smallest eviction that relieves meaningful pressure.
    constexpr std::size_t MinToEvict = 8192;
    if (live_blocks < TriggerFloor) {
        return 0;
    }
    const std::size_t quarter = live_blocks / 4;
    return quarter < MinToEvict ? MinToEvict : quarter;
}

} // namespace Dynarmic::Backend::Arm64
