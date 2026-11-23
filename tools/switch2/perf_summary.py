#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import csv
from pathlib import Path
import re
from statistics import median
import sys


FIELDS = (
    "log", "build", "windows", "first_accepted_frame_ms", "median_window_fps",
    "median_window_frame_ms", "worst_frame_ms", "jit_busy_ms", "jit_mem_fallbacks",
    "max_logged_heap_mib",
)
FRONTEND = re.compile(r"\[\s*\d+\.\d+\] Frontend <Info> .*?:\d+:[^:]*: (.*)")
BUILD = re.compile(r"Avalon \(eden switch2 fork\) (b\d+) \(\d{4}-\d\d-\d\d\)")
FIRST_FRAME = re.compile(r"First guest frame accepted by Vulkan after (\d+) ms")
HEAP = re.compile(r"Stage: .* \(memory (\d+) / \d+ MiB\)")
NUMBER = r"\d+(?:\.\d+)?"
PERFORMANCE = re.compile(
    rf"Performance: speed {NUMBER}%, game ({NUMBER}) fps, frame ({NUMBER}) ms "
    rf"\(worst ({NUMBER}) ms\); JIT published \d+ times, \d+ KiB, \d+ ms; "
    r"JIT busy (\d+) ms; JIT mem fallbacks (\d+)"
)


def summarize(path):
    build = first_frame = max_heap = None
    fps, frame_ms = [], []
    worst = None
    jit_busy = fallbacks = 0
    with path.open(encoding="utf-8", errors="replace") as log:
        for line in log:
            prefix = FRONTEND.fullmatch(line.rstrip("\r\n"))
            if not prefix:
                continue
            message = prefix[1]
            if match := PERFORMANCE.fullmatch(message):
                fps.append(float(match[1]))
                if (mean_ms := float(match[2])) > 0:
                    frame_ms.append(mean_ms)
                if (worst_ms := float(match[3])) > 0:
                    worst = max(worst or 0, worst_ms)
                jit_busy += int(match[4])
                fallbacks += int(match[5])
            elif match := BUILD.fullmatch(message):
                build = match[1]
            elif match := FIRST_FRAME.fullmatch(message):
                first_frame = int(match[1])
            elif match := HEAP.fullmatch(message):
                max_heap = max(max_heap or 0, int(match[1]))
    return {
        "log": path.name,
        "build": build,
        "windows": len(fps),
        "first_accepted_frame_ms": first_frame,
        "median_window_fps": median(fps) if fps else None,
        "median_window_frame_ms": median(frame_ms) if frame_ms else None,
        "worst_frame_ms": worst,
        "jit_busy_ms": jit_busy if fps else None,
        "jit_mem_fallbacks": fallbacks if fps else None,
        "max_logged_heap_mib": max_heap,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Summarize one Avalon session per input log as CSV on stdout."
    )
    parser.add_argument("logs", type=Path, nargs="+", metavar="LOG", help="Avalon log files")
    rows = []
    for path in parser.parse_args().logs:
        try:
            rows.append(summarize(path))
        except OSError:
            parser.exit(1, f"cannot read log: {path}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)


if __name__ == "__main__":
    main()
