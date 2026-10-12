#pragma once
#include "core/display/display_adapter.h"
#include "services/companion/companion_service.h"
#include "services/hosts/host_service.h"
#include "services/network/network_service.h"
#include "services/web_access/web_access_service.h"
#include <cstddef>
#include <optional>
#include <string>

namespace cardputer_hub::apps {
inline constexpr core::PixelPosition homeWifiDotPosition{8, 8};
inline constexpr core::PixelPosition homeBluetoothDotPosition{62, 8};
// Shown only while WEB ACCESS is not Off.
inline constexpr core::PixelPosition homeWebDotPosition{104, 8};
inline constexpr core::PixelPosition homeAmbientOrigin{0, 22};
inline constexpr std::int32_t homeAmbientWidth = 240;
inline constexpr std::int32_t homeAmbientHeight = 74;
inline constexpr core::PixelPosition homeDeviceRowOrigin{0, 96};
inline constexpr std::int32_t homeDeviceRowHeight = 16;
inline constexpr core::PixelPosition homeActionBarOrigin{0, 113};
inline constexpr std::int32_t homeActionBarHeight = 22;

enum class HomeStatusIndicator : std::uint8_t {
    HollowQuiet,
    FilledPale,
    FilledBlue,
    FilledLeaf,
    FilledVermilion,
};
using HomeWifiIndicator = HomeStatusIndicator;

HomeStatusIndicator homeWifiIndicator(const services::WifiStatusSnapshot& status);
HomeStatusIndicator homeBluetoothIndicator(services::HostConnectionStatus status);
void drawHomeStatusBar(core::IDisplayAdapter& display);
void drawHomeWifi(core::IDisplayAdapter& display, HomeStatusIndicator indicator);
void drawHomeBluetooth(core::IDisplayAdapter& display, HomeStatusIndicator indicator);
// Nothing while WEB ACCESS is Off; Leaf while it runs, Vermilion after a failure.
std::optional<HomeStatusIndicator> homeWebIndicator(services::WebAccessState state);
// Clears the group, then draws the dot, the label and the client count.
void drawHomeWeb(core::IDisplayAdapter& display, std::optional<HomeStatusIndicator> indicator,
                 std::size_t clients);
std::string homeConnectedDeviceName(const services::HostStatusSnapshot& status);
// `warning` swaps the Leaf dot for Vermilion, for a Companion mismatch notice.
void drawHomeConnectedDevice(core::IDisplayAdapter& display, const std::string& name,
                             bool warning = false);
std::string homeCompanionMismatchText(services::CompanionMismatchAdvice advice);
void drawHomeActions(core::IDisplayAdapter& display, std::int32_t plateX);
void drawHomeBattery(core::IDisplayAdapter& display, std::optional<std::uint8_t> batteryPercent);
} // namespace cardputer_hub::apps
