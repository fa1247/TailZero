/**
 * TailZero - screen registry.
 * SPDX-License-Identifier: MIT
 *
 * The registry is the only place that knows the concrete screen classes; the
 * ScreenManager only sees Screen*. The returned vector is indexed by Page order
 * (Home=0 .. Netcheck=4) which must match `kPageCount`.
 */
#include "ui/screens/registry.h"

#include "ui/screens/details_screen.h"
#include "ui/screens/devices_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/netcheck_screen.h"
#include "ui/screens/ping_screen.h"

namespace tailzero::ui {

std::size_t page_index(Page page) {
    switch (page) {
        case Page::Home:
            return 0;
        case Page::Devices:
            return 1;
        case Page::Details:
            return 2;
        case Page::Ping:
            return 3;
        case Page::Netcheck:
            return 4;
    }
    return 0;
}

std::vector<std::unique_ptr<Screen>> create_all_screens(const Actions& actions, UiModel& ui) {
    std::vector<std::unique_ptr<Screen>> screens;
    screens.reserve(kPageCount);
    screens.push_back(std::make_unique<HomeScreen>(actions, ui));
    screens.push_back(std::make_unique<DevicesScreen>(actions, ui));
    screens.push_back(std::make_unique<DetailsScreen>(actions, ui));
    screens.push_back(std::make_unique<PingScreen>(actions, ui));
    screens.push_back(std::make_unique<NetcheckScreen>(actions, ui));
    return screens;
}

}  // namespace tailzero::ui
