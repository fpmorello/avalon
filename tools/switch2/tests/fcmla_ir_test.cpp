// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "dynarmic/backend/arm64/fp16_encoding.h"
#include "dynarmic/frontend/A64/translate/impl/impl.h"
#include "dynarmic/ir/opcodes.h"

namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*, std::string_view format,
                       const std::format_args& args) {
    std::fprintf(stderr, "%s\n", std::vformat(format, args).c_str());
}
} // namespace Common::Log

void AssertFailSoftImpl() {
    std::exit(1);
}

[[noreturn]] void AssertFatalImpl() {
    std::exit(1);
}

using namespace Dynarmic;

int main() {
    using Backend::Arm64::EncodeFMADD16;
    // Reference words independently assembled for Cortex-A78C.
    static_assert(EncodeFMADD16(0, 1, 2, 3) == 0x1fc20c20);
    static_assert(EncodeFMADD16(19, 25, 30, 2) == 0x1fde0b33);

    const A64::UserConfig config{};
    const A64::LocationDescriptor location{0, {}};
    const auto check_half = [&](bool q, unsigned index, unsigned rotation, A64::Vec vd) {
        IR::Block block{location};
        A64::TranslatorVisitor translator{block, location, config};
        assert(translator.FCMLA_elt(q, Imm<2>{1}, Imm<1>{index & 1}, Imm<1>{1}, Imm<4>{2},
                                    Imm<2>{rotation}, Imm<1>{index >> 1}, A64::Vec::V1, vd));
        unsigned count = 0;
        unsigned negations = 0;
        for (const auto& inst : block.Instructions()) {
            assert(inst.GetOpcode() != IR::Opcode::FPMulAdd32);
            negations += inst.GetOpcode() == IR::Opcode::FPNeg16;
            if (inst.GetOpcode() != IR::Opcode::FPMulAdd16) {
                continue;
            }
            const auto* addend = inst.GetArg(0).GetInst();
            const auto* lhs = inst.GetArg(1).GetInst();
            const auto* rhs = inst.GetArg(2).GetInst();
            const bool imaginary = (count & 1) != 0;
            const bool negated = rotation == 2 || (rotation == 1 && !imaginary) ||
                                 (rotation == 3 && imaginary);
            assert((rhs->GetOpcode() == IR::Opcode::FPNeg16) == negated);
            if (negated) {
                rhs = rhs->GetArg(0).GetInst();
            }
            assert(addend->GetArg(0).GetInst()->GetArg(0).GetA64VecRef() == vd);
            assert(lhs->GetArg(0).GetInst()->GetArg(0).GetA64VecRef() == A64::Vec::V1);
            assert(rhs->GetArg(0).GetInst()->GetArg(0).GetA64VecRef() == A64::Vec::V18);
            assert(addend->GetArg(1).GetU8() == count);
            assert(lhs->GetArg(1).GetU8() == ((count & ~1U) | (rotation & 1)));
            assert(rhs->GetArg(1).GetU8() == (index * 2 + (imaginary != bool(rotation & 1))));
            assert(IR::ReadsFromAndWritesToFPSRCumulativeExceptionBits(inst.GetOpcode()));
            ++count;
        }
        assert(count == (q ? 8 : 4));
        assert(negations == count * (rotation == 2 ? 2 : (rotation & 1)) / 2);
    };
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        check_half(false, 1, rotation, A64::Vec::V0);
        check_half(true, 3, rotation, A64::Vec::V18);
    }

    IR::Block single{location};
    A64::TranslatorVisitor translator{single, location, config};
    assert(translator.FCMLA_elt(true, Imm<2>{2}, Imm<1>{0}, Imm<1>{0}, Imm<4>{2},
                                Imm<2>{1}, Imm<1>{1}, A64::Vec::V1, A64::Vec::V0));
    unsigned fma_count = 0;
    for (const auto& inst : single.Instructions()) {
        fma_count += inst.GetOpcode() == IR::Opcode::FPMulAdd32;
    }
    assert(fma_count == 4);
}
