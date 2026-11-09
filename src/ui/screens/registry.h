/**
 * TailZero - screen registry contract (implemented in src/ui/screens).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <memory>
#include <vector>

#include "ui/screen.h"

namespace tailzero::ui {

/**
 * Create all application screens, indexed by Page order:
 *   0 Home, 1 Devices, 2 Details, 3 Ping, 4 Netcheck.
 * The returned vector must contain exactly one screen per Page value.
 */
std::vector<std::unique_ptr<Screen>> create_all_screens(const Actions& actions, UiModel& ui);

/** Number of pages (must match the Page enum). */
constexpr std::size_t kPageCount = 5;

std::size_t page_index(Page page);

}  // namespace tailzero::ui
