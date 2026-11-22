// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "common/common_types.h"
#include <oaknut/code_block.hpp>
#include <oaknut/oaknut.hpp>
#include "common/container/unordered_map.h"
#include "common/container/unordered_set.h"

#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/fastmem.h"
#ifdef __SWITCH__
#    include "dynarmic/backend/arm64/horizon_code_block.h"
#endif
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::Arm64 {

class AddressSpace {
public:
    explicit AddressSpace(std::size_t code_cache_size);
    virtual ~AddressSpace();

    virtual void GenerateIR(IR::Block& ir_block, IR::LocationDescriptor) const = 0;

    CodePtr Get(IR::LocationDescriptor descriptor);

    // Returns "most likely" LocationDescriptor assocated with the emitted code at that location
    std::optional<IR::LocationDescriptor> ReverseGetLocation(CodePtr host_pc);

    // Returns "most likely" entry_point associated with the emitted code at that location
    CodePtr ReverseGetEntryPoint(CodePtr host_pc);

    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);

    void InvalidateBasicBlocks(const ::Common::unordered_set<IR::LocationDescriptor>& descriptors);

    void ClearCache();
#ifdef __SWITCH__
    /// Writes the JIT cache list of this core. Returns the blocks written.
    std::size_t SaveWarmList() const;
    /// Evicts the count oldest blocks from the bookkeeping maps. The code itself stays
    /// valid (and executing) until the next full clear; only the dispatcher stops
    /// finding the evicted blocks. Returns how many were evicted.
    std::size_t EvictOldestBlocks(std::size_t count);
#endif
protected:
#ifdef __SWITCH__
    /// JIT cache (common/horizon/stats.h). `slot` tells this core's list from the others'.
    void LoadWarmList(int slot);
    void CompileWarmList();
    /// Whether guest code can be read at the block's address right now.
    virtual bool IsCodeReadable(IR::LocationDescriptor) const { return true; }

    int warm_slot = -1;
    std::vector<u64> warm_list;

    /// Direct-mapped cache in front of block_entries. The dispatcher looks a block up on
    /// every jump it cannot link, and a map of several hundred thousand blocks misses the
    /// CPU's caches on nearly every one of them.
    struct LookupEntry {
        u64 descriptor;
        CodePtr entry_point; ///< Null when the entry is empty
    };
    static constexpr std::size_t LookupCacheBits = 16;
    std::vector<LookupEntry> lookup_cache = std::vector<LookupEntry>(std::size_t{1} << LookupCacheBits);
    static std::size_t LookupIndex(u64 descriptor) {
        return static_cast<std::size_t>((descriptor * 0x9E3779B97F4A7C15ULL) >> (64 - LookupCacheBits));
    }
    void ClearLookupCache() {
        std::fill(lookup_cache.begin(), lookup_cache.end(), LookupEntry{});
    }
#endif
    virtual EmitConfig GetEmitConfig() = 0;
    virtual void RegisterNewBasicBlock(const IR::Block& block, const EmittedBlockInfo& block_info) = 0;

    void ProtectCodeMemory() {
#if defined(DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__SWITCH__)
        mem.protect();
#endif
    }

    void UnprotectCodeMemory() {
#if defined(DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT) || defined(__APPLE__) || defined(__OpenBSD__) || defined(__SWITCH__)
        mem.unprotect();
#endif
    }

    std::size_t GetRemainingSize();
    EmittedBlockInfo Emit(IR::Block ir_block);
    void Link(EmittedBlockInfo& block);
    void LinkBlockLinks(const CodePtr entry_point, const CodePtr target_ptr, const std::vector<BlockRelocation>& block_relocations_list);
    void RelinkForDescriptor(IR::LocationDescriptor target_descriptor, CodePtr target_ptr);

    FakeCall FastmemCallback(u64 host_pc);

    IR::Block ir_block;
#ifdef __SWITCH__
    std::size_t emits_since_heap_check = 0;
    /// Blocks in emission order, oldest first, for EvictOldestBlocks. The entry point
    /// tells a block that is still alive from one invalidated or compiled again since.
    std::deque<std::pair<IR::LocationDescriptor, CodePtr>> emission_order;
    /// Evictions since the last full clear; the clear is the fallback when they do not
    /// relieve the heap.
    unsigned evictions_since_clear = 0;
#endif
    const std::size_t code_cache_size;
#ifdef __SWITCH__
    HorizonCodeBlock mem;
    /// Where code is written; it runs at mem.ptr() once published by mem.invalidate()
    u32* WritePtr() const { return mem.wptr(); }
#else
    oaknut::CodeBlock mem;
    u32* WritePtr() const { return mem.ptr(); }
#endif
    oaknut::CodeGenerator code;

    // A IR::LocationDescriptor will have one current CodePtr.
    // However, there can be multiple other CodePtrs which are older, previously invalidated blocks.
    std::map<CodePtr, IR::LocationDescriptor> reverse_block_entries;
    ::Common::unordered_map<IR::LocationDescriptor, CodePtr> block_entries;
    ::Common::unordered_map<CodePtr, EmittedBlockInfo> block_infos;
    // A block is added to each of its targets once, when it is emitted: no set is needed.
    ::Common::unordered_map<IR::LocationDescriptor, std::vector<CodePtr>> block_references;

    ExceptionHandler exception_handler;
    FastmemManager fastmem_manager;

    struct PreludeInfo {
        std::ptrdiff_t end_of_prelude;

        using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, void* jit_state, volatile u32* halt_reason);
        RunCodeFuncType run_code;
        RunCodeFuncType step_code;
        void* return_to_dispatcher;
        void* return_from_run_code;

        void* read_memory_8;
        void* read_memory_16;
        void* read_memory_32;
        void* read_memory_64;
        void* read_memory_128;
        void* wrapped_read_memory_8;
        void* wrapped_read_memory_16;
        void* wrapped_read_memory_32;
        void* wrapped_read_memory_64;
        void* wrapped_read_memory_128;
        void* exclusive_read_memory_8;
        void* exclusive_read_memory_16;
        void* exclusive_read_memory_32;
        void* exclusive_read_memory_64;
        void* exclusive_read_memory_128;
        void* write_memory_8;
        void* write_memory_16;
        void* write_memory_32;
        void* write_memory_64;
        void* write_memory_128;
        void* wrapped_write_memory_8;
        void* wrapped_write_memory_16;
        void* wrapped_write_memory_32;
        void* wrapped_write_memory_64;
        void* wrapped_write_memory_128;
        void* exclusive_write_memory_8;
        void* exclusive_write_memory_16;
        void* exclusive_write_memory_32;
        void* exclusive_write_memory_64;
        void* exclusive_write_memory_128;

        void* call_svc;
        void* exception_raised;
        void* dc_raised;
        void* ic_raised;
        void* isb_raised;

        void* get_cntpct;
        void* add_ticks;
        void* get_ticks_remaining;
    } prelude_info;
};

}  // namespace Dynarmic::Backend::Arm64
