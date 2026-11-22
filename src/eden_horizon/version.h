// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fork build identification for the Switch 2 port. Bump EDEN_HORIZON_BUILD with every
// released NRO and describe the change in CHANGELOG.md; tag the commit sw2-fork-<build>
// so `git describe` names the exact source of a binary.

#pragma once

#define EDEN_HORIZON_BUILD "b7"
#define EDEN_HORIZON_BUILD_DATE "2026-11-21"

/// Short form for screen headers, where width is scarce.
#define EDEN_HORIZON_BUILD_ID "b7"

/// Full form for logs and crash reports.
#define EDEN_HORIZON_VERSION \
    "Avalon (eden switch2 fork) " EDEN_HORIZON_BUILD " (" EDEN_HORIZON_BUILD_DATE ")"
