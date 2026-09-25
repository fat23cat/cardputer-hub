#pragma once

#include "core/display/display_adapter.h"
#include "services/mac_status/mac_status_service.h"

#include <array>
#include <string>

namespace cardputer_hub::apps {

struct MacStatusPresentation {
    std::array<std::string, 8> labels{};
    std::array<int, 4> bars{};
};

MacStatusPresentation formatMacStatus(const services::MacStatusSnapshot& snapshot);
void drawMacStatusMetric(core::IDisplayAdapter& display, const MacStatusPresentation& presentation,
                         int index);

} // namespace cardputer_hub::apps
