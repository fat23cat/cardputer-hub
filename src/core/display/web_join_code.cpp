#include "core/display/web_join_code.h"
#include "core/display/palette.h"
#include "core/display/qr_code.h"
#include "core/display/text_layout.h"
#include <algorithm>
namespace cardputer_hub::core {
std::int32_t webJoinQrModulePixels(int modules) {
    return std::clamp<std::int32_t>(135 / (modules + 4), 1, 4);
}
void drawWebJoinCode(IDisplayAdapter& display, std::string_view ssid, std::string_view password) {
    const core::TextStyle quiet{core::palette::ordinal, core::palette::bone, core::systemTextScale};
    const auto code = core::QrCode::encodeText(core::wifiQrPayload(ssid, password));
    if (!code)
        return;
    const auto modulePixels = webJoinQrModulePixels(code->size());
    const auto extent = code->size() * modulePixels;
    const auto left = (240 - extent) / 2;
    const auto top = (135 - extent) / 2;
    // One rectangle per run of dark modules in a row.
    for (int y = 0; y < code->size(); ++y) {
        for (int x = 0; x < code->size();) {
            if (!code->module(x, y)) {
                ++x;
                continue;
            }
            int end = x;
            while (end < code->size() && code->module(end, y))
                ++end;
            display.fillRectangle({left + x * modulePixels, top + y * modulePixels},
                                  (end - x) * modulePixels, modulePixels, core::palette::ink);
            x = end;
        }
    }
    // The side labels keep the code's two-module light border clear.
    const auto border = 2 * modulePixels;
    const auto side = [&](std::int32_t y, const char* text, bool rightSide) {
        const auto columnLeft = rightSide ? left + extent + border : 0;
        display.drawText({core::centeredTextX(text, columnLeft, left - border), y}, text, quiet);
    };
    side(54, "SCAN", false);
    side(70, "TO JOIN", false);
    side(62, "Q CLOSE", true);
}
} // namespace cardputer_hub::core
