// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Startup {

// Retry once after a rejected waiting card, then let the guest start even if it was rejected again.
inline bool PresentWaitingCard(const auto& present_once) {
    return present_once() || present_once();
}

} // namespace Startup
