#pragma once
#include "core/display/display_adapter.h"
#include "services/hosts/host_service.h"
#include "services/network/network_service.h"
#include <optional>

namespace cardputer_hub::apps {
inline constexpr core::PixelPosition homeWifiDotPosition{8, 8};
inline constexpr core::PixelPosition homeBluetoothDotPosition{62, 8};
inline constexpr core::PixelPosition homeCompanionPosition{94, 6};
inline constexpr std::int32_t homeCompanionIndicatorSize = 9;
inline constexpr core::PixelPosition homeAmbientOrigin{0, 22};
inline constexpr std::int32_t homeAmbientWidth = 240;
inline constexpr std::int32_t homeAmbientHeight = 113;

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
void drawHomeCompanion(core::IDisplayAdapter& display, bool visible);
void drawHomeBattery(core::IDisplayAdapter& display, std::optional<std::uint8_t> batteryPercent);
} // namespace cardputer_hub::apps
