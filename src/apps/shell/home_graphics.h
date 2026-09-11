#pragma once
#include "core/display/display_adapter.h"
#include <string>
namespace cardputer_hub::apps {
std::string fitHomeHostName(const std::string& name);
void drawHomeHostName(core::IDisplayAdapter& display, const std::string& name);
void drawBluetoothIcon(core::IDisplayAdapter& display, core::RgbColor color);
void drawWifiOfflineIcon(core::IDisplayAdapter& display);
void drawHomeWave(core::IDisplayAdapter& display, unsigned phaseMilliseconds);
} // namespace cardputer_hub::apps
