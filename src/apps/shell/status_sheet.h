#pragma once
#include "apps/shell/home_graphics.h"
#include "core/display/display_adapter.h"
#include "services/companion/companion_service.h"
#include "services/hosts/host_service.h"
#include "services/network/network_service.h"
#include "services/web_access/web_access_service.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace cardputer_hub::apps {

// The snapshots the STATUS sheet reads. A missing Companion reads as Unavailable.
struct StatusSheetInputs {
    services::WifiStatusSnapshot wifi;
    services::HostStatusSnapshot host;
    std::optional<services::CompanionServiceState> companion;
    services::CompanionMismatchAdvice companionAdvice =
        services::CompanionMismatchAdvice::RebuildBoth;
    services::WebAccessState web = services::WebAccessState::Off;
    std::size_t webClients = 0;
    std::optional<std::uint8_t> batteryPercent;
};

struct StatusSheetRow {
    const char* ordinal = "";
    const char* label = "";
    // No dot for BATTERY.
    std::optional<HomeStatusIndicator> indicator;
    std::string value;
    core::RgbColor valueColor{};
};

inline constexpr std::size_t statusSheetRowCount = 5;
using StatusSheetModel = std::array<StatusSheetRow, statusSheetRowCount>;

// WI-FI, BLUETOOTH, COMPANION, WEB and BATTERY, as words, dots and colours.
StatusSheetModel buildStatusSheetModel(const StatusSheetInputs& inputs);

// Draws the sheet through display overlay regions: everything after reset(),
// then only the rows whose model changed. A settled sheet draws nothing.
class StatusSheetView {
  public:
    void reset() { drawn_.reset(); }
    void render(core::IDisplayAdapter& display, const StatusSheetModel& model);

  private:
    std::optional<StatusSheetModel> drawn_;
};

} // namespace cardputer_hub::apps
