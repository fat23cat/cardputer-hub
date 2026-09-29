#pragma once

#include "core/display/display_adapter.h"
#include "services/mac_status/mac_status_service.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cardputer_hub::apps {

enum class MacStatusPage : std::uint8_t { Overview, Cpu, Power, Network, Memory };
inline constexpr std::uint8_t macStatusPageCount = 5;

struct MacStatusPresentation {
    std::array<std::string, 8> labels{};
    std::array<int, 4> bars{};
    bool charging = false;
    // Memory pressure and temperature: 0 neutral, 1 healthy, 2 needs attention.
    std::array<std::uint8_t, 2> severity{};
};

MacStatusPresentation formatMacStatus(const services::MacStatusSnapshot& snapshot);
// Index 0 draws the CPU sparkline from `history` in place of the CPU bar.
void drawMacStatusMetric(core::IDisplayAdapter& display, const MacStatusPresentation& presentation,
                         int index, const services::MacStatusHistory& history);
void drawMacStatusPageDots(core::IDisplayAdapter& display, MacStatusPage page);

// A detail page is a fixed set of screen regions. A region is repainted only
// when its key, which encodes everything it draws, changes.
struct MacStatusRegion {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::string key;
    std::function<void(core::IDisplayAdapter&)> draw;
};

std::vector<MacStatusRegion> layoutMacStatusPage(MacStatusPage page,
                                                 const services::MacStatusSnapshot& snapshot,
                                                 const services::MacStatusDetails& details,
                                                 const services::MacStatusHistory& history);

services::MacDetailGroup macStatusDetailGroup(MacStatusPage page);

} // namespace cardputer_hub::apps
