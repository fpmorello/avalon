# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "perf_summary.py"


def log(*messages):
    return "".join(
        f"[  {i:2}.000000] Frontend <Info> main.cpp:123:Sample: {message}\n"
        for i, message in enumerate(messages)
    )


def performance(fps, mean, worst, busy, fallbacks):
    return (
        f"Performance: speed 95%, game {fps} fps, frame {mean} ms (worst {worst} ms); "
        "JIT published 10 times, 5 KiB, 1 ms; "
        f"JIT busy {busy} ms; JIT mem fallbacks {fallbacks}"
    )


class PerfSummaryTest(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, TOOL, *args], text=True, capture_output=True)

    def test_one_row_per_log_without_inventing_frame_values(self):
        with tempfile.TemporaryDirectory() as directory:
            known = Path(directory) / "known.log"
            waiting = Path(directory) / "waiting.log"
            known.write_text(log(
                "Avalon (eden switch2 fork) b10 (2026-11-22)",
                "Stage: running (memory 200 / 9000 MiB)",
                "First guest frame accepted by Vulkan after 650 ms",
                performance("20.0", "28.0", "60.0", 40, 2),
                performance("30.0", "20.0", "50.0", 14, 1),
                "Stage: playing (memory 250 / 9000 MiB)",
            ), encoding="utf-8")
            waiting.write_text(log(
                "Avalon (eden switch2 fork) b10 (2026-11-22)",
                "Still waiting for first guest frame after 8 seconds",
                performance("0.0", "0.0", "0.0", 8, 1),
            ), encoding="utf-8")
            result = self.run_tool(known, waiting)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertEqual(list(csv.DictReader(io.StringIO(result.stdout))), [
                {"log": "known.log", "build": "b10", "windows": "2", "first_accepted_frame_ms": "650",
                 "median_window_fps": "25.0", "median_window_frame_ms": "24.0", "worst_frame_ms": "60.0",
                 "jit_busy_ms": "54", "jit_mem_fallbacks": "3", "max_logged_heap_mib": "250"},
                {"log": "waiting.log", "build": "b10", "windows": "1", "first_accepted_frame_ms": "",
                 "median_window_fps": "0.0", "median_window_frame_ms": "", "worst_frame_ms": "",
                 "jit_busy_ms": "8", "jit_mem_fallbacks": "1", "max_logged_heap_mib": ""},
            ])

    def test_empty_run_and_missing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            empty = Path(directory) / "empty.log"
            empty.touch()
            result = self.run_tool(empty)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertEqual(list(csv.DictReader(io.StringIO(result.stdout))), [
                {"log": "empty.log", "build": "", "windows": "0", "first_accepted_frame_ms": "",
                 "median_window_fps": "", "median_window_frame_ms": "", "worst_frame_ms": "",
                 "jit_busy_ms": "", "jit_mem_fallbacks": "", "max_logged_heap_mib": ""},
            ])
            missing = Path(directory) / "missing.log"
            result = self.run_tool(empty, missing)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, f"cannot read log: {missing}\n")
