// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#include <bit>
#include <print>

#include "dynarmic/backend/arm64/a64_address_space.h"
#include "dynarmic/backend/arm64/a64_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/devirtualize.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/stack_layout.h"
#include "dynarmic/common/cast_util.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/llvm_disassemble.h"
#include "dynarmic/interface/exclusive_monitor.h"
#include "dynarmic/ir/location_descriptor.h"
#include "dynarmic/ir/terminal.h"

#ifdef __SWITCH__
#    include <variant>
#    include <vector>

#    include "common/horizon/stats.h"
#    include "dynarmic/backend/arm64/jit_eviction.h"
#endif

namespace Dynarmic::Backend::Arm64 {

AddressSpace::AddressSpace(std::size_t code_cache_size)
    : ir_block{IR::LocationDescriptor{0}}
    , code_cache_size(code_cache_size)
    , mem(code_cache_size)
    , code(WritePtr(), mem.ptr())
    , fastmem_manager(exception_handler)
{
    ASSERT(code_cache_size <= 128 * 1024 * 1024 && "code_cache_size > 128 MiB not currently supported");

#ifndef __SWITCH__ // no fault handling on Horizon hosts
    exception_handler.Register(mem, code_cache_size);
#endif
    exception_handler.SetFastmemCallback([this](u64 host_pc) {
        return FastmemCallback(host_pc);
    });
}

#ifdef __SWITCH__
namespace {
// The address spaces alive, for eden_horizon_jit_save_caches: nothing guarantees that the
// emulator destroys its cores before the process ends.
std::mutex live_mutex;
std::vector<AddressSpace*> live_spaces;
} // namespace

AddressSpace::~AddressSpace() {
    std::scoped_lock lock{live_mutex};
    std::erase(live_spaces, this);
}

namespace {
bool HeapIsRunningLow();

std::string WarmListPath(int slot) {
    return std::string{eden_horizon_jit_cache_prefix} + "_core" + std::to_string(slot) + ".bin";
}
} // namespace

void AddressSpace::LoadWarmList(int slot) {
    warm_slot = slot;
    {
        std::scoped_lock lock{live_mutex};
        live_spaces.push_back(this);
    }
    if (eden_horizon_jit_cache_prefix[0] == 0) {
        return;
    }
    std::FILE* const file = std::fopen(WarmListPath(slot).c_str(), "rb");
    if (file == nullptr) {
        return;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    // A cache holds a few hundred thousand blocks at most; anything bigger is not a list.
    if (size > 0 && size <= 16 * 1024 * 1024) {
        warm_list.resize(static_cast<std::size_t>(size) / sizeof(u64));
        if (std::fread(warm_list.data(), sizeof(u64), warm_list.size(), file) != warm_list.size()) {
            warm_list.clear();
        }
    }
    std::fclose(file);
}

std::size_t AddressSpace::SaveWarmList() const {
    // A session that compiled next to nothing must not replace a good list.
    constexpr std::size_t MinBlocksToSave = 4096;
    if (warm_slot < 0 || eden_horizon_jit_cache_prefix[0] == 0 ||
        block_entries.size() < MinBlocksToSave) {
        return 0;
    }
    std::vector<u64> list;
    list.reserve(block_entries.size());
    for (const auto& [descriptor, entry_point] : block_entries) {
        list.push_back(descriptor.Value());
    }
    // In address order, so that warming up reads guest code and fills the cache in order.
    std::ranges::sort(list);
    std::FILE* const file = std::fopen(WarmListPath(warm_slot).c_str(), "wb");
    if (file == nullptr) {
        return 0;
    }
    const std::size_t written = std::fwrite(list.data(), sizeof(u64), list.size(), file);
    std::fclose(file);
    return written;
}

void AddressSpace::CompileWarmList() {
    const std::vector<u64> list = std::move(warm_list);
    warm_list.clear();
    if (list.empty()) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    // Leave room for what this session adds, so that warming up never ends in a clear.
    const std::size_t min_space = code_cache_size / 5;
    // One publication per batch: see HorizonCodeBlock::unprotect.
    constexpr std::size_t BatchSize = 512;
    std::size_t compiled = 0;
    mem.unprotect();
    for (const u64 value : list) {
        const IR::LocationDescriptor descriptor{value};
        if (GetRemainingSize() < min_space || HeapIsRunningLow()) {
            break;
        }
        if (Get(descriptor) || !IsCodeReadable(descriptor)) {
            continue;
        }
        GenerateIR(ir_block, descriptor);
        try {
            Emit(std::move(ir_block));
        } catch (const CodeBufferExhausted&) {
            break;
        }
        if (++compiled % BatchSize == 0) {
            mem.protect();
            mem.unprotect();
        }
    }
    mem.protect();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    __atomic_fetch_add(&eden_horizon_jit_warm_blocks, compiled, __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &eden_horizon_jit_warm_micros,
        static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()),
        __ATOMIC_RELAXED);
}
#else
AddressSpace::~AddressSpace() = default;
#endif

CodePtr AddressSpace::Get(IR::LocationDescriptor descriptor) {
#ifdef __SWITCH__
    LookupEntry& cached = lookup_cache[LookupIndex(descriptor.Value())];
    if (cached.entry_point != nullptr && cached.descriptor == descriptor.Value()) {
        return cached.entry_point;
    }
    if (const auto iter = block_entries.find(descriptor); iter != block_entries.end()) {
        cached = {descriptor.Value(), iter->second};
        return iter->second;
    }
    return nullptr;
#else
    if (const auto iter = block_entries.find(descriptor); iter != block_entries.end()) {
        return iter->second;
    }
    return nullptr;
#endif
}

std::optional<IR::LocationDescriptor> AddressSpace::ReverseGetLocation(CodePtr host_pc) {
    if (auto iter = reverse_block_entries.upper_bound(host_pc); iter != reverse_block_entries.begin()) {
        // upper_bound locates the first value greater than host_pc, so we need to decrement
        --iter;
        return iter->second;
    }
    return std::nullopt;
}

CodePtr AddressSpace::ReverseGetEntryPoint(CodePtr host_pc) {
    if (auto iter = reverse_block_entries.upper_bound(host_pc); iter != reverse_block_entries.begin()) {
        // upper_bound locates the first value greater than host_pc, so we need to decrement
        --iter;
        return iter->first;
    }
    return nullptr;
}

#ifdef __SWITCH__
// libounce's allocator figures (switch2/memory/heap.h).
extern "C" {
std::uint64_t heapGetTotalSize(void);
std::uint64_t heapGetFreeSize(void);
}

namespace {

/// The heap stops serving blocks of a few MiB well before it is full, and the JIT's
/// per-block bookkeeping is the largest thing in it that can be thrown away.
/// Adds the time since `start` to the frontend's compile time figure.
struct CompileTimer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~CompileTimer() {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        __atomic_fetch_add(
            &eden_horizon_jit_compile_micros,
            static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()),
            __ATOMIC_RELAXED);
    }
};

bool HeapIsRunningLow() {
    constexpr std::uint64_t MinFree = 1536ULL * 1024 * 1024;
    return heapGetFreeSize() < MinFree;
}

void CollectLeaf(const IR::Term::LeafTerminal& leaf, std::vector<IR::LocationDescriptor>& out) {
    if (const auto* link = std::get_if<IR::Term::LinkBlock>(&leaf)) {
        out.push_back(link->next);
    } else if (const auto* fast = std::get_if<IR::Term::LinkBlockFast>(&leaf)) {
        out.push_back(fast->next);
    }
}

/// The blocks a terminal can jump straight to.
void CollectSuccessors(const IR::Term::Terminal& terminal, std::vector<IR::LocationDescriptor>& out) {
    if (const auto* leaf = std::get_if<IR::Term::LeafTerminal>(&terminal)) {
        CollectLeaf(*leaf, out);
    } else if (const auto* if_ = std::get_if<IR::Term::If>(&terminal)) {
        CollectLeaf(if_->then_, out);
        CollectLeaf(if_->else_, out);
    } else if (const auto* check_bit = std::get_if<IR::Term::CheckBit>(&terminal)) {
        CollectLeaf(check_bit->then_, out);
        CollectLeaf(check_bit->else_, out);
    } else if (const auto* check_halt = std::get_if<IR::Term::CheckHalt>(&terminal)) {
        CollectLeaf(check_halt->else_, out);
    }
}

} // namespace
#endif

CodePtr AddressSpace::GetOrEmit(IR::LocationDescriptor descriptor) {
    if (CodePtr block_entry = Get(descriptor)) {
        return block_entry;
    }
#ifdef __SWITCH__
    if (!warm_list.empty()) {
        CompileWarmList();
        if (CodePtr block_entry = Get(descriptor)) {
            return block_entry;
        }
    }
    const CompileTimer timer;
    // Checked here and not in Emit(), which the loop below calls with a block it must keep.
    constexpr std::size_t MinBlocksToDrop = 16384;
    if ((++emits_since_heap_check & 0x3FF) == 0) {
        if (!HeapIsRunningLow()) {
            // The pressure is gone: later evictions start counting from zero again, so
            // the full clear stays the answer to evictions that did not help.
            evictions_since_clear = 0;
        } else if (block_infos.size() >= MinBlocksToDrop) {
            // Throwing the whole cache away recompiles everything the game runs next; drop
            // the oldest quarter first and keep the full clear as the fallback.
            constexpr unsigned MaxEvictionsBeforeClear = 2;
            if (evictions_since_clear >= MaxEvictionsBeforeClear ||
                EvictOldestBlocks(EvictBlockCount(block_infos.size())) == 0) {
                __atomic_fetch_add(&eden_horizon_jit_heap_clears, 1, __ATOMIC_RELAXED);
                ClearCache();
            }
        }
    }
#endif
    GenerateIR(ir_block, descriptor);
#ifdef __SWITCH__
    // Publishing code is a round trip to another process, far dearer than compiling a
    // block. Compile the blocks this one leads straight into as well and publish them
    // together: new code mostly runs on into its successors anyway.
    std::vector<IR::LocationDescriptor> ahead;
    CollectSuccessors(ir_block.GetTerminal(), ahead);
    mem.unprotect();
#endif
    CodePtr entry_point;
    try {
        entry_point = Emit(std::move(ir_block)).entry_point;
    } catch (const CodeBufferExhausted&) {
        // Very large blocks may not fit in what is left of the cache. Emission mutates the IR,
        // so regenerate it and retry with an empty cache.
        ProtectCodeMemory();
        ClearCache();
        GenerateIR(ir_block, descriptor);
        entry_point = Emit(std::move(ir_block)).entry_point;
    }
#ifdef __SWITCH__
    const std::size_t MaxBlocksAhead = static_cast<std::size_t>(eden_horizon_jit_blocks_ahead);
    // Emit() clears the cache when it runs low, which would drop the block just made.
    constexpr std::size_t MinSpaceAhead = 2 * 1024 * 1024;
    std::size_t emitted_ahead = 0;
    for (std::size_t i = 0; i < ahead.size() && emitted_ahead < MaxBlocksAhead; ++i) {
        const IR::LocationDescriptor next = ahead[i];
        if (Get(next) || GetRemainingSize() < MinSpaceAhead) {
            continue;
        }
        GenerateIR(ir_block, next);
        CollectSuccessors(ir_block.GetTerminal(), ahead);
        try {
            Emit(std::move(ir_block));
        } catch (const CodeBufferExhausted&) {
            break;
        }
        ++emitted_ahead;
    }
    mem.protect();
#endif
    return entry_point;
}

void AddressSpace::InvalidateBasicBlocks(const ::Common::unordered_set<IR::LocationDescriptor>& descriptors) {
    UnprotectCodeMemory();

    for (const auto& descriptor : descriptors) {
        const auto iter = block_entries.find(descriptor);
        if (iter == block_entries.end()) {
            continue;
        }

        // Unlink before removal because InvalidateBasicBlocks can be called within a fastmem callback,
        // and the currently executing block may have references to itself which need to be unlinked.
        RelinkForDescriptor(descriptor, nullptr);

        block_entries.erase(iter);
#ifdef __SWITCH__
        lookup_cache[LookupIndex(descriptor.Value())] = {};
#endif
    }

    ProtectCodeMemory();
}

void AddressSpace::ClearCache() {
#ifdef __SWITCH__
    __atomic_fetch_sub(&eden_horizon_jit_blocks, block_infos.size(), __ATOMIC_RELAXED);
    __atomic_fetch_add(&eden_horizon_jit_cache_clears, 1, __ATOMIC_RELAXED);
    ClearLookupCache();
    emission_order.clear();
    evictions_since_clear = 0;
#endif
    block_entries.clear();
    reverse_block_entries.clear();
    block_infos.clear();
    block_references.clear();
    code.set_offset(prelude_info.end_of_prelude);
}

#ifdef __SWITCH__
std::size_t AddressSpace::EvictOldestBlocks(std::size_t count) {
    if (count == 0) {
        return 0;
    }
    UnprotectCodeMemory();
    std::size_t evicted = 0;
    while (evicted < count && !emission_order.empty()) {
        const auto [descriptor, entry_point] = emission_order.front();
        emission_order.pop_front();
        if (!IsLiveEmission(block_entries, descriptor, entry_point)) {
            // Invalidated since, and maybe compiled again at another address: that newer
            // block has its own, younger entry in the queue. InvalidateBasicBlocks left
            // this one's per-block maps behind; release them now. Nothing links to it.
            block_infos.erase(entry_point);
            reverse_block_entries.erase(entry_point);
            continue;
        }
        // Same teardown as InvalidateBasicBlocks — unlink the referrers to the
        // dispatcher first — plus the per-block maps a full clear frees: they are the
        // memory this eviction exists to release. The emitted code itself is left
        // alone (the cursor is not rewound), so a core executing it runs on undisturbed.
        // The list of referrers stays (RelinkForDescriptor drops the dead ones): if the
        // block is compiled again, Emit links them straight to it instead of leaving
        // them on the dispatcher for good.
        RelinkForDescriptor(descriptor, nullptr);
        block_infos.erase(entry_point);
        reverse_block_entries.erase(entry_point);
        block_entries.erase(descriptor);
        lookup_cache[LookupIndex(descriptor.Value())] = {};
        ++evicted;
    }
    ProtectCodeMemory();
    if (evicted != 0) {
        ++evictions_since_clear;
        __atomic_fetch_sub(&eden_horizon_jit_blocks, evicted, __ATOMIC_RELAXED);
        __atomic_fetch_add(&eden_horizon_jit_partial_evictions, evicted, __ATOMIC_RELAXED);
    }
    return evicted;
}
#endif

std::size_t AddressSpace::GetRemainingSize() {
    return code_cache_size - static_cast<std::size_t>(code.offset());
}

EmittedBlockInfo AddressSpace::Emit(IR::Block block) {
    if (GetRemainingSize() < 1024 * 1024) {
        ClearCache();
    }

    UnprotectCodeMemory();

    EmittedBlockInfo block_info = EmitArm64(code, std::move(block), GetEmitConfig(), fastmem_manager, GetRemainingSize());

    ASSERT(block_entries.insert({block.Location(), block_info.entry_point}).second);
    ASSERT(reverse_block_entries.insert({block_info.entry_point, block.Location()}).second);
    ASSERT(block_infos.insert({block_info.entry_point, block_info}).second);
#ifdef __SWITCH__
    __atomic_fetch_add(&eden_horizon_jit_blocks, 1, __ATOMIC_RELAXED);
    emission_order.emplace_back(block.Location(), block_info.entry_point);
#endif

    Link(block_info);
    RelinkForDescriptor(block.Location(), block_info.entry_point);

    mem.invalidate(reinterpret_cast<u32*>(block_info.entry_point), block_info.size);
    ProtectCodeMemory();

    RegisterNewBasicBlock(block, block_info);

    return block_info;
}

void AddressSpace::Link(EmittedBlockInfo& block_info) {
    using namespace oaknut;
    using namespace oaknut::util;

    for (auto [ptr_offset, target] : block_info.relocations) {
        CodeGenerator c{WritePtr(), mem.ptr()};
        c.set_xptr(reinterpret_cast<u32*>(block_info.entry_point + ptr_offset));

        switch (target) {
        case LinkTarget::ReturnToDispatcher:
            c.B(prelude_info.return_to_dispatcher);
            break;
        case LinkTarget::ReturnFromRunCode:
            c.B(prelude_info.return_from_run_code);
            break;
        // { this, vaddr, size }
        case LinkTarget::ReadMemory8:
            c.BL(prelude_info.read_memory_8);
            break;
        case LinkTarget::ReadMemory16:
            c.BL(prelude_info.read_memory_16);
            break;
        case LinkTarget::ReadMemory32:
            c.BL(prelude_info.read_memory_32);
            break;
        case LinkTarget::ReadMemory64:
            c.BL(prelude_info.read_memory_64);
            break;
        case LinkTarget::ReadMemory128:
            c.BL(prelude_info.read_memory_128);
            break;
        // { this, vaddr, size }
        case LinkTarget::WrappedReadMemory8:
            c.BL(prelude_info.wrapped_read_memory_8);
            break;
        case LinkTarget::WrappedReadMemory16:
            c.BL(prelude_info.wrapped_read_memory_16);
            break;
        case LinkTarget::WrappedReadMemory32:
            c.BL(prelude_info.wrapped_read_memory_32);
            break;
        case LinkTarget::WrappedReadMemory64:
            c.BL(prelude_info.wrapped_read_memory_64);
            break;
        case LinkTarget::WrappedReadMemory128:
            c.BL(prelude_info.wrapped_read_memory_128);
            break;
        //
        case LinkTarget::ExclusiveReadMemory8:
            c.BL(prelude_info.exclusive_read_memory_8);
            break;
        case LinkTarget::ExclusiveReadMemory16:
            c.BL(prelude_info.exclusive_read_memory_16);
            break;
        case LinkTarget::ExclusiveReadMemory32:
            c.BL(prelude_info.exclusive_read_memory_32);
            break;
        case LinkTarget::ExclusiveReadMemory64:
            c.BL(prelude_info.exclusive_read_memory_64);
            break;
        case LinkTarget::ExclusiveReadMemory128:
            c.BL(prelude_info.exclusive_read_memory_128);
            break;
        // { this, vaddr, value, size }
        case LinkTarget::WriteMemory8:
            c.BL(prelude_info.write_memory_8);
            break;
        case LinkTarget::WriteMemory16:
            c.BL(prelude_info.write_memory_16);
            break;
        case LinkTarget::WriteMemory32:
            c.BL(prelude_info.write_memory_32);
            break;
        case LinkTarget::WriteMemory64:
            c.BL(prelude_info.write_memory_64);
            break;
        case LinkTarget::WriteMemory128:
            c.BL(prelude_info.write_memory_128);
            break;
        //
        case LinkTarget::WrappedWriteMemory8:
            c.BL(prelude_info.wrapped_write_memory_8);
            break;
        case LinkTarget::WrappedWriteMemory16:
            c.BL(prelude_info.wrapped_write_memory_16);
            break;
        case LinkTarget::WrappedWriteMemory32:
            c.BL(prelude_info.wrapped_write_memory_32);
            break;
        case LinkTarget::WrappedWriteMemory64:
            c.BL(prelude_info.wrapped_write_memory_64);
            break;
        case LinkTarget::WrappedWriteMemory128:
            c.BL(prelude_info.wrapped_write_memory_128);
            break;
        //
        case LinkTarget::ExclusiveWriteMemory8:
            c.BL(prelude_info.exclusive_write_memory_8);
            break;
        case LinkTarget::ExclusiveWriteMemory16:
            c.BL(prelude_info.exclusive_write_memory_16);
            break;
        case LinkTarget::ExclusiveWriteMemory32:
            c.BL(prelude_info.exclusive_write_memory_32);
            break;
        case LinkTarget::ExclusiveWriteMemory64:
            c.BL(prelude_info.exclusive_write_memory_64);
            break;
        case LinkTarget::ExclusiveWriteMemory128:
            c.BL(prelude_info.exclusive_write_memory_128);
            break;
        case LinkTarget::CallSVC:
            c.BL(prelude_info.call_svc);
            break;
        case LinkTarget::ExceptionRaised:
            c.BL(prelude_info.exception_raised);
            break;
        case LinkTarget::InstructionSynchronizationBarrierRaised:
            c.BL(prelude_info.isb_raised);
            break;
        case LinkTarget::InstructionCacheOperationRaised:
            c.BL(prelude_info.ic_raised);
            break;
        case LinkTarget::DataCacheOperationRaised:
            c.BL(prelude_info.dc_raised);
            break;
        case LinkTarget::GetCNTPCT:
            c.BL(prelude_info.get_cntpct);
            break;
        case LinkTarget::AddTicks:
            c.BL(prelude_info.add_ticks);
            break;
        case LinkTarget::GetTicksRemaining:
            c.BL(prelude_info.get_ticks_remaining);
            break;
        default:
            UNREACHABLE();
        }
    }

    for (const auto& [target_descriptor, list] : block_info.block_relocations) {
        block_references[target_descriptor].push_back(block_info.entry_point);
        LinkBlockLinks(block_info.entry_point, Get(target_descriptor), list);
    }
}

void AddressSpace::LinkBlockLinks(const CodePtr entry_point, const CodePtr target_ptr, const std::vector<BlockRelocation>& block_relocations_list) {
    using namespace oaknut;
    using namespace oaknut::util;

    for (auto [ptr_offset, type] : block_relocations_list) {
        CodeGenerator c{WritePtr(), mem.ptr()};
        c.set_xptr(reinterpret_cast<u32*>(entry_point + ptr_offset));

        switch (type) {
        case BlockRelocationType::Branch:
            if (target_ptr) {
                c.B((void*)target_ptr);
            } else {
                c.NOP();
            }
            break;
        case BlockRelocationType::MoveToScratch1:
            if (target_ptr) {
                c.ADRL(Xscratch1, (void*)target_ptr);
            } else {
                c.ADRL(Xscratch1, prelude_info.return_to_dispatcher);
            }
            break;
        default:
            UNREACHABLE();
        }
    }
}

void AddressSpace::RelinkForDescriptor(IR::LocationDescriptor target_descriptor, CodePtr target_ptr) {
#ifdef __SWITCH__
    // Eviction removes blocks without visiting the lists they are registered in, so drop
    // the referrers that are gone here, where the list is walked anyway, and do not keep
    // an empty list per block.
    const auto references = block_references.find(target_descriptor);
    if (references == block_references.end()) {
        return;
    }
    PruneDeadReferrers(references->second, [this](CodePtr code_ptr) { return block_infos.contains(code_ptr); });
    if (references->second.empty()) {
        block_references.erase(references);
        return;
    }
#endif
    for (auto code_ptr : block_references[target_descriptor]) {
        if (auto block_iter = block_infos.find(code_ptr); block_iter != block_infos.end()) {
            const EmittedBlockInfo& block_info = block_iter->second;

            if (auto relocation_iter = block_info.block_relocations.find(target_descriptor); relocation_iter != block_info.block_relocations.end()) {
                LinkBlockLinks(block_info.entry_point, target_ptr, relocation_iter->second);
            }

            mem.invalidate(reinterpret_cast<u32*>(block_info.entry_point), block_info.size);
        }
    }
}

FakeCall AddressSpace::FastmemCallback(u64 host_pc) {
    {
        const auto host_ptr = std::bit_cast<CodePtr>(host_pc);

        const auto entry_point = ReverseGetEntryPoint(host_ptr);
        if (!entry_point) {
            goto fail;
        }

        const auto block_info = block_infos.find(entry_point);
        if (block_info == block_infos.end()) {
            goto fail;
        }

        const auto patch_entry = block_info->second.fastmem_patch_info.find(host_ptr - entry_point);
        if (patch_entry == block_info->second.fastmem_patch_info.end()) {
            goto fail;
        }

        const auto fc = patch_entry->second.fc;

        if (patch_entry->second.recompile) {
            const auto marker = patch_entry->second.marker;
            fastmem_manager.MarkDoNotFastmem(marker);
            InvalidateBasicBlocks({std::get<0>(marker)});
        }

        return fc;
    }

fail:
    std::print("dynarmic: Segfault happened within JITted code at host_pc = {:016x}\n"
        "Segfault wasn't at a fastmem patch location!\n", host_pc);
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::Arm64

#ifdef __SWITCH__
extern "C" unsigned long long eden_horizon_jit_save_caches(void) {
    using namespace Dynarmic::Backend::Arm64;
    std::scoped_lock lock{live_mutex};
    unsigned long long written = 0;
    for (const AddressSpace* space : live_spaces) {
        written += space->SaveWarmList();
    }
    return written;
}
#endif
