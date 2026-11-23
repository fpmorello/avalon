// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <format>

#include "common/cpu_features.h"
#include "eden_horizon/clock_probe.h"

namespace ClockProbe {

namespace {

constexpr int ReadsPerBatch = 100'000;
constexpr std::size_t Batches = 9;
static_assert(GuestTicksPerSecond == Common::WallClock::CNTFRQ);

struct Batch {
    double ns_per_read;
    u64 checksum;
};

template <typename Read>
Batch Measure(Read read) {
    constexpr auto now = std::chrono::steady_clock::now;
    u64 checksum = 0;
    const auto start = now();
    for (int i = 0; i < ReadsPerBatch; ++i) {
        checksum += static_cast<u64>(read());
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(now() - start);
    return {elapsed.count() / ReadsPerBatch, checksum};
}

} // namespace

std::string RunProbe() {
    const auto reference = [] { return Common::g_wall_clock.GetCNTPCT(); };
    ClockProbe::Agreement agreement;
    const auto check_agreement = [&] {
        for (int i = 0; i < 128; ++i) {
            const auto before = reference();
            const auto candidate = ReadCandidate();
            const auto after = reference();
            agreement.Add(before, candidate, after);
        }
    };
    check_agreement();
    std::array<double, Batches> reference_ns{}, candidate_ns{};
    u64 checksum = 0;
    for (std::size_t i = 0; i < Batches; ++i) {
        Batch base{}, direct{};
        if (i % 2 == 0) {
            base = Measure(reference);
            direct = Measure(ReadCandidate);
        } else {
            direct = Measure(ReadCandidate);
            base = Measure(reference);
        }
        reference_ns[i] = base.ns_per_read;
        candidate_ns[i] = direct.ns_per_read;
        checksum += base.checksum + direct.checksum;
        check_agreement();
    }

    auto report = std::format(
        "Avalon clock probe 1\n"
        "{} paired batches of {} reads (alternating order)\n"
        "Median current:   {:.2f} ns/read\n"
        "Median candidate: {:.2f} ns/read\n"
        "Candidate outside current bounds: {}/{}\n"
        "Backward steps: current {}, candidate {}\n"
        "Elapsed guest ticks: current {}, candidate {}\n"
        "Checksum: {:016x}\n\n"
        "Batch  Current ns/read  Candidate ns/read\n",
        Batches, ReadsPerBatch, ClockProbe::Median(reference_ns), ClockProbe::Median(candidate_ns),
        agreement.outside, agreement.samples, agreement.reversed_reference,
        agreement.reversed_candidate, agreement.reference_delta, agreement.candidate_delta,
        checksum);
    for (std::size_t i = 0; i < Batches; ++i) {
        report += std::format("{:5}  {:15.2f}  {:17.2f}\n", i + 1, reference_ns[i], candidate_ns[i]);
    }
    return report;
}

} // namespace ClockProbe
