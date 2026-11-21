// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PageTableSelfTest {

/// Diagnostic, run when sdmc:/switch/avalon/test_pagetable.txt exists (the file is removed
/// first): runs dynarmic's A64 and A32 JITs on this console against the chunked guest page
/// table through the two-level lookup, and writes PASS/FAIL per check to
/// sdmc:/switch/avalon/test_pagetable_result.txt. Startup continues afterwards. If the
/// result file ends at "running", the test crashed the process (see crash.txt).
void RunIfRequested();

} // namespace PageTableSelfTest
