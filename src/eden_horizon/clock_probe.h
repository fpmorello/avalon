// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace ClockProbe {

constexpr std::int64_t GuestTicksPerSecond = 19'200'000;
std::int64_t ReadCandidate();
std::string RunProbe();

struct Agreement {
    void Add(std::int64_t before, std::int64_t candidate, std::int64_t after) {
        if (samples == 0) {
            first_reference = before;
            first_candidate = candidate;
        } else {
            reversed_reference += before < previous_reference;
            reversed_candidate += candidate < previous_candidate;
        }
        reversed_reference += after < before;
        outside += candidate < before || candidate > after;
        ++samples;
        previous_reference = after;
        previous_candidate = candidate;
        reference_delta = before - first_reference;
        candidate_delta = candidate - first_candidate;
    }

    unsigned samples{}, outside{}, reversed_reference{}, reversed_candidate{};
    std::int64_t reference_delta{}, candidate_delta{};

private:
    std::int64_t first_reference{}, first_candidate{}, previous_reference{}, previous_candidate{};
};

template <typename T, std::size_t N>
T Median(std::array<T, N> values) {
    static_assert(N % 2 == 1);
    std::sort(values.begin(), values.end());
    return values[N / 2];
}

} // namespace ClockProbe
