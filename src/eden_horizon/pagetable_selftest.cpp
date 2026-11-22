// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On-device check of dynarmic's two-level page table lookup (odd/tasks/two-level-page-table.md,
// T4). The host that builds the NRO is x86_64 and cannot run the arm64 JIT, so this is the
// runtime proof. Each JIT gets the same page table configuration as core/arm/dynarmic, and a
// guest program touches four kinds of addresses:
//   - mapped pages in two different chunks: must be served inline, never by a callback;
//   - an unmapped page inside a materialized chunk: entry is zero, must fall back;
//   - an address in a never-touched chunk (shared zero chunk): must fall back, no allocation;
//   - (A64) an address beyond the page table's bits: must fall back.
// The callbacks count every access they serve, so an inline access that wrongly fell back,
// or a fallback that was wrongly served inline, both fail.

#include "eden_horizon/pagetable_selftest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "common/logging.h"
#include "common/page_table.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"

namespace PageTableSelfTest {
namespace {

constexpr const char* RequestFile = "sdmc:/switch/avalon/test_pagetable.txt";
constexpr const char* ResultFile = "sdmc:/switch/avalon/test_pagetable_result.txt";

constexpr std::uint64_t PageSize = 0x1000;
constexpr std::uint64_t CodeAddress = 0x0010'0000;
constexpr std::uint64_t StoredValue = 0x1122'3344'5566'7788ULL;
/// What the callbacks return for a read, so a fallback read is recognizable.
constexpr std::uint64_t FallbackTag = 0xFA11'BAC0'5CA1'0F1EULL;

class Report {
public:
    explicit Report(const char* name) : name{name} {}

    void Check(bool ok, const char* what) {
        lines += std::string{name} + (ok ? " PASS " : " FAIL ") + what + "\n";
        failed = failed || !ok;
    }
    bool Failed() const {
        return failed;
    }
    const std::string& Lines() const {
        return lines;
    }

private:
    const char* name;
    std::string lines;
    bool failed = false;
};

/// Guest pages backed by host memory, mapped the way core/memory.cpp MapPages does.
class GuestMemory {
public:
    explicit GuestMemory(std::size_t address_space_bits) {
        table.Resize(address_space_bits, 12);
    }
    ~GuestMemory() {
        for (void* page : pages) {
            std::free(page);
        }
    }

    /// Maps one zeroed host page at guest address va; returns the host page.
    std::uint8_t* Map(std::uint64_t va) {
        auto* const host = static_cast<std::uint8_t*>(std::aligned_alloc(PageSize, PageSize));
        std::memset(host, 0, PageSize);
        pages.push_back(host);
        const std::uint64_t page = va / PageSize;
        // Absolute-offset entry: the JIT adds the full guest address to it.
        const auto host_ptr = reinterpret_cast<std::uintptr_t>(host) - page * PageSize;
        table.entries.GetUnchecked(page).Store(false, Common::PageType::Memory, 0, host_ptr);
        return host;
    }

    const Common::PageTable::PageEntryData* const* Directory() const {
        return table.entries.ChunkDirectory();
    }
    std::uint64_t Slot(std::uint64_t va) const {
        return (va / PageSize) >> table.entries.ChunkIndexBits();
    }

    Common::PageTable table;

private:
    std::vector<void*> pages;
};

/// Fallback accounting shared by both JIT callback sets.
struct Fallbacks {
    unsigned reads = 0;
    unsigned writes = 0;
    std::uint64_t last_write_address = 0;
    std::uint64_t last_write_value = 0;
};

template <typename Jit>
struct HaltOnSvc {
    Jit* jit = nullptr;
    void Halt() {
        jit->HaltExecution();
    }
};

class A64Callbacks final : public Dynarmic::A64::UserCallbacks {
public:
    A64Callbacks(const std::vector<std::uint32_t>& code_, Fallbacks& fb_) : code{code_}, fb{fb_} {}

    std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A64::VAddr vaddr) override {
        const std::uint64_t index = (vaddr - CodeAddress) / 4;
        if (vaddr < CodeAddress || index >= code.size()) {
            return 0xD4000001; // SVC #0: stop on a stray fetch
        }
        return code[index];
    }
    std::uint64_t MemoryRead(Dynarmic::A64::VAddr vaddr, std::size_t) override {
        ++fb.reads;
        return FallbackTag | (vaddr & 0xFFFF'FFFF);
    }
    Dynarmic::A64::Vector MemoryRead128(Dynarmic::A64::VAddr vaddr) override {
        ++fb.reads;
        return {FallbackTag | vaddr, 0};
    }
    void MemoryWrite(Dynarmic::A64::VAddr vaddr, std::uint64_t value, std::size_t) override {
        ++fb.writes;
        fb.last_write_address = vaddr;
        fb.last_write_value = value;
    }
    void MemoryWrite128(Dynarmic::A64::VAddr vaddr, Dynarmic::A64::Vector value) override {
        MemoryWrite(vaddr, value[0], 16);
    }
    void CallSVC(std::uint32_t) override {
        halt.Halt();
    }
    void ExceptionRaised(Dynarmic::A64::VAddr, Dynarmic::A64::Exception) override {
        exception = true;
        halt.Halt();
    }
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override {
        return 1000;
    }
    std::uint64_t GetCNTPCT() override {
        return 0;
    }

    HaltOnSvc<Dynarmic::A64::Jit> halt;
    bool exception = false;

private:
    const std::vector<std::uint32_t>& code;
    Fallbacks& fb;
};

class A32Callbacks final : public Dynarmic::A32::UserCallbacks {
public:
    A32Callbacks(const std::vector<std::uint32_t>& code_, Fallbacks& fb_) : code{code_}, fb{fb_} {}

    std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A32::VAddr vaddr) override {
        const std::uint64_t index = (vaddr - CodeAddress) / 4;
        if (vaddr < CodeAddress || index >= code.size()) {
            return 0xEF000000; // SVC #0
        }
        return code[index];
    }
    std::uint64_t MemoryRead(Dynarmic::A32::VAddr vaddr, std::size_t) override {
        ++fb.reads;
        return (FallbackTag | vaddr) & 0xFFFF'FFFF'FFFF'FFFFULL;
    }
    void MemoryWrite(Dynarmic::A32::VAddr vaddr, std::uint64_t value, std::size_t) override {
        ++fb.writes;
        fb.last_write_address = vaddr;
        fb.last_write_value = value;
    }
    void CallSVC(std::uint32_t) override {
        halt.Halt();
    }
    void ExceptionRaised(Dynarmic::A32::VAddr, Dynarmic::A32::Exception) override {
        exception = true;
        halt.Halt();
    }
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override {
        return 1000;
    }

    HaltOnSvc<Dynarmic::A32::Jit> halt;
    bool exception = false;

private:
    const std::vector<std::uint32_t>& code;
    Fallbacks& fb;
};

std::uint64_t Load64(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void RunA64(Report& r) {
    GuestMemory mem{36};
    // Chunk 0 and chunk 1 (one chunk covers 2^18 pages = 1 GiB of guest space).
    constexpr std::uint64_t A = 0x0000'1000;
    constexpr std::uint64_t B = 0x4000'3000;
    constexpr std::uint64_t C = 0x0000'2000;     // unmapped page in materialized chunk 0
    constexpr std::uint64_t D = 0x1'4000'0000;   // chunk 5, never touched
    constexpr std::uint64_t E = 1ULL << 37;      // beyond the 36-bit table
    std::uint8_t* const host_a = mem.Map(A);
    std::uint8_t* const host_b = mem.Map(B);
    const auto* const zero_chunk = mem.Directory()[mem.Slot(D)];

    const std::vector<std::uint32_t> code{
        0xf9000005, // str  x5, [x0]
        0xf9400006, // ldr  x6, [x0]
        0xf9000425, // str  x5, [x1, #8]
        0xf9400427, // ldr  x7, [x1, #8]
        0xf9400048, // ldr  x8, [x2]
        0xf9400069, // ldr  x9, [x3]
        0xf940008a, // ldr  x10, [x4]
        0xf9000085, // str  x5, [x4]
        0xb940040b, // ldr  w11, [x0, #4]
        0x7940102c, // ldrh w12, [x1, #8]
        0xd4000001, // svc  #0
    };
    Fallbacks fb;
    A64Callbacks cb{code, fb};

    Dynarmic::A64::UserConfig config{};
    config.callbacks = &cb;
    config.page_table = reinterpret_cast<void**>(
        const_cast<Common::PageTable::PageEntryData**>(mem.Directory()));
    config.page_table_level2_bits = std::uint8_t(mem.table.entries.ChunkIndexBits());
    config.page_table_address_space_bits = 36;
    config.page_table_pointer_mask = Common::PageTable::ATTRIBUTE_MASK;
    config.page_table_marked_bit = std::uint8_t(0);
    config.page_table_sign_extension = std::uint8_t(Common::PageTable::SIGN_BIT);
    config.silently_mirror_page_table = false;
    config.absolute_offset_page_table = true;
    config.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
    config.only_detect_misalignment_via_page_table_on_page_boundary = true;
    config.code_cache_size = 8 * 1024 * 1024;

    Dynarmic::A64::Jit jit{config};
    cb.halt.jit = &jit;
    jit.SetRegister(0, A);
    jit.SetRegister(1, B);
    jit.SetRegister(2, C);
    jit.SetRegister(3, D);
    jit.SetRegister(4, E);
    jit.SetRegister(5, StoredValue);
    jit.SetPC(CodeAddress);
    jit.Run();

    r.Check(!cb.exception, "no guest exception");
    r.Check(Load64(host_a) == StoredValue, "inline store, chunk 0");
    r.Check(jit.GetRegister(6) == StoredValue, "inline load, chunk 0");
    r.Check(Load64(host_b + 8) == StoredValue, "inline store, chunk 1");
    r.Check(jit.GetRegister(7) == StoredValue, "inline load, chunk 1");
    r.Check(jit.GetRegister(11) == (StoredValue >> 32), "inline 32-bit load");
    r.Check(jit.GetRegister(12) == (StoredValue & 0xFFFF), "inline 16-bit load");
    r.Check(jit.GetRegister(8) == (FallbackTag | C), "unmapped page falls back");
    r.Check(jit.GetRegister(9) == (FallbackTag | (D & 0xFFFF'FFFF)), "untouched chunk falls back");
    r.Check(jit.GetRegister(10) == (FallbackTag | (E & 0xFFFF'FFFF)), "beyond table falls back");
    r.Check(fb.last_write_address == E && fb.last_write_value == StoredValue,
            "out-of-table store falls back");
    r.Check(fb.reads == 3 && fb.writes == 1, "exactly 3 fallback reads and 1 fallback write");
    r.Check(mem.Directory()[mem.Slot(D)] == zero_chunk, "untouched chunk stays unallocated");
}

void RunA32(Report& r) {
    GuestMemory mem{32};
    // 32-bit space: four chunks of 1 GiB.
    constexpr std::uint32_t A = 0x0000'1000;
    constexpr std::uint32_t B = 0x4000'3000;
    constexpr std::uint32_t C = 0x0000'2000;  // unmapped page in materialized chunk 0
    constexpr std::uint32_t D = 0xC000'0000;  // chunk 3, never touched
    constexpr std::uint32_t W = 0x8000'0000;  // chunk 2, never touched (store target)
    std::uint8_t* const host_a = mem.Map(A);
    std::uint8_t* const host_b = mem.Map(B);
    const auto* const zero_chunk = mem.Directory()[mem.Slot(D)];

    const std::vector<std::uint32_t> code{
        0xe5805000, // str r5, [r0]
        0xe5906000, // ldr r6, [r0]
        0xe5815008, // str r5, [r1, #8]
        0xe5917008, // ldr r7, [r1, #8]
        0xe5928000, // ldr r8, [r2]
        0xe5939000, // ldr r9, [r3]
        0xe594a000, // ldr r10, [r4]
        0xe5845000, // str r5, [r4]
        0xef000000, // svc #0
    };
    Fallbacks fb;
    A32Callbacks cb{code, fb};

    constexpr std::size_t NumEntries = Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES;
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &cb;
    config.page_table = reinterpret_cast<std::array<std::uint8_t*, NumEntries>*>(
        const_cast<Common::PageTable::PageEntryData**>(mem.Directory()));
    config.page_table_level2_bits = std::uint8_t(mem.table.entries.ChunkIndexBits());
    config.page_table_pointer_mask = Common::PageTable::ATTRIBUTE_MASK;
    config.page_table_marked_bit = std::uint8_t(0);
    config.page_table_sign_extension = std::uint8_t(Common::PageTable::SIGN_BIT);
    config.absolute_offset_page_table = true;
    config.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
    config.only_detect_misalignment_via_page_table_on_page_boundary = true;
    config.code_cache_size = 8 * 1024 * 1024;

    Dynarmic::A32::Jit jit{config};
    cb.halt.jit = &jit;
    constexpr std::uint32_t Value = std::uint32_t(StoredValue);
    jit.Regs()[0] = A;
    jit.Regs()[1] = B;
    jit.Regs()[2] = C;
    jit.Regs()[3] = D;
    jit.Regs()[4] = W;
    jit.Regs()[5] = Value;
    jit.Regs()[15] = std::uint32_t(CodeAddress);
    jit.SetCpsr(0x000001d0); // user mode, ARM state
    jit.Run();

    std::uint32_t a_word, b_word;
    std::memcpy(&a_word, host_a, 4);
    std::memcpy(&b_word, host_b + 8, 4);
    r.Check(!cb.exception, "no guest exception");
    r.Check(a_word == Value, "inline store, chunk 0");
    r.Check(jit.Regs()[6] == Value, "inline load, chunk 0");
    r.Check(b_word == Value, "inline store, chunk 1");
    r.Check(jit.Regs()[7] == Value, "inline load, chunk 1");
    r.Check(jit.Regs()[8] == std::uint32_t(FallbackTag | C), "unmapped page falls back");
    r.Check(jit.Regs()[9] == std::uint32_t(FallbackTag | D), "untouched chunk falls back");
    r.Check(jit.Regs()[10] == std::uint32_t(FallbackTag | W), "untouched chunk 2 falls back");
    r.Check(fb.last_write_address == W && fb.last_write_value == Value,
            "store to untouched chunk falls back");
    r.Check(fb.reads == 3 && fb.writes == 1, "exactly 3 fallback reads and 1 fallback write");
    r.Check(mem.Directory()[mem.Slot(D)] == zero_chunk &&
                mem.Directory()[mem.Slot(W)] == zero_chunk,
            "untouched chunks stay unallocated");
}

void WriteResult(const std::string& text) {
    if (std::FILE* const out = std::fopen(ResultFile, "w")) {
        std::fputs(text.c_str(), out);
        std::fclose(out);
    }
}

} // namespace

void RunIfRequested() {
    std::FILE* const request = std::fopen(RequestFile, "r");
    if (request == nullptr) {
        return;
    }
    std::fclose(request);
    // Removed first, so a crash in the test does not repeat on the next start.
    std::remove(RequestFile);
    WriteResult("running\n");

    Report a64{"A64"};
    RunA64(a64);
    Report a32{"A32"};
    RunA32(a32);

    const bool failed = a64.Failed() || a32.Failed();
    WriteResult(a64.Lines() + a32.Lines() + (failed ? "RESULT FAIL\n" : "RESULT PASS\n"));
    LOG_INFO(Frontend, "Page table self-test: {}", failed ? "FAIL" : "PASS");
}

} // namespace PageTableSelfTest
