// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
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

/// Whether an entry of the emission queue still names the block that is alive at its
/// location. A block that was invalidated, or invalidated and compiled again, leaves its
/// old entry behind in the queue: evicting through that entry would drop the newest copy
/// of the block as if it were the oldest. `block_entries` maps location -> entry point.
template<typename BlockEntries, typename Location, typename EntryPoint>
bool IsLiveEmission(const BlockEntries& block_entries, const Location& location, const EntryPoint& entry_point) {
    const auto iter = block_entries.find(location);
    return iter != block_entries.end() && iter->second == entry_point;
}

/// Drops from a block's list of referrers (the blocks that jump to it) the ones that no
/// longer exist, and returns how many were dropped. The live ones are kept in order: they
/// are what gets linked to the block again if it is evicted and later recompiled.
template<typename Referrers, typename IsAlive>
std::size_t PruneDeadReferrers(Referrers& referrers, IsAlive&& is_alive) {
    const auto first_dead = std::remove_if(referrers.begin(), referrers.end(),
                                           [&](const auto& entry_point) { return !is_alive(entry_point); });
    const std::size_t dropped = static_cast<std::size_t>(referrers.end() - first_dead);
    referrers.erase(first_dead, referrers.end());
    return dropped;
}

} // namespace Dynarmic::Backend::Arm64
