#pragma once
#include "core/display/display_adapter.h"
#include "services/network/network_service.h"
#include <string>
namespace cardputer_hub::apps {
inline constexpr core::PixelPosition homeWifiIconPosition{108, 7};
inline constexpr core::PixelPosition homeWifiDotPosition{123, 9};
inline constexpr core::PixelPosition homeWifiRegion{104, 5};
inline constexpr core::PixelPosition homeCompanionIndicatorPosition{214, 82};
inline constexpr std::int32_t homeCompanionIndicatorSize = 11;
inline constexpr std::int32_t homeWifiRegionWidth = 36;
inline constexpr std::int32_t homeWifiRegionHeight = 14;

enum class HomeWifiIndicator : std::uint8_t {
    HollowQuiet,
    FilledPale,
    FilledBlue,
    FilledLeaf,
    FilledVermilion,
};

HomeWifiIndicator homeWifiIndicator(const services::WifiStatusSnapshot& status);
std::string fitHomeHostName(const std::string& name);
void drawHomeHostName(core::IDisplayAdapter& display, const std::string& name);
void drawBluetoothIcon(core::IDisplayAdapter& display, core::RgbColor color);
void drawWifiIcon(core::IDisplayAdapter& display, core::PixelPosition position,
                  core::RgbColor color);
void drawWifiStatusIndicator(core::IDisplayAdapter& display, core::PixelPosition position,
                             HomeWifiIndicator indicator);
void drawCompanionIndicator(core::IDisplayAdapter& display, bool visible);
void drawHomeWave(core::IDisplayAdapter& display, unsigned phaseMilliseconds);
} // namespace cardputer_hub::apps
