#pragma once

#include "core/display/display_adapter.h"
#include "services/service_status/service_status_service.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace cardputer_hub::apps {

inline constexpr char serviceStatusAppId[] = "service-status";

struct ServiceStatusRowFrame {
    std::string value;
    // The level's accent mark beside the value; the value names the level.
    std::optional<core::RgbColor> marker;
    bool operator==(const ServiceStatusRowFrame& other) const {
        return value == other.value && marker.has_value() == other.marker.has_value() &&
               (!marker ||
                (marker->red == other.marker->red && marker->green == other.marker->green &&
                 marker->blue == other.marker->blue));
    }
};

// Everything SERVICES HEALTH shows; drawing compares two frames and repaints only
// the regions that differ.
struct ServiceStatusFrame {
    std::string status;
    std::array<ServiceStatusRowFrame, services::statusSources.size()> rows{};
    // No connection or the failed pages.
    std::string caption;
    bool operator==(const ServiceStatusFrame& other) const {
        return status == other.status && rows == other.rows && caption == other.caption;
    }
};

// OK / MAINT / MINOR / MAJOR / CRITICAL; -- before the first answer and
// without any connection; ERROR when the page could not be fetched or read.
const char* serviceStatusValue(const services::ServiceStatusEntry& entry);
// The header's right slot: CHECKING during a round, otherwise how long ago the
// last round ended in ten-second steps (0 SEC AGO, 10 SEC AGO, ...).
std::string serviceStatusHeader(const services::ServiceStatusSnapshot& snapshot);
ServiceStatusFrame serviceStatusFrame(const services::ServiceStatusSnapshot& snapshot);
void drawServiceStatus(core::IDisplayAdapter& display, const ServiceStatusFrame& next,
                       const ServiceStatusFrame* previous);

} // namespace cardputer_hub::apps
