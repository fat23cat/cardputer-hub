#pragma once
#include "core/display/display_adapter.h"
#include <string_view>
namespace cardputer_hub::core {
[[nodiscard]] std::int32_t webJoinQrModulePixels(int modules);
// Call on a Bone-cleared full screen; leaves a two-module quiet border and side hints.
void drawWebJoinCode(IDisplayAdapter& display, std::string_view ssid, std::string_view password);
} // namespace cardputer_hub::core
