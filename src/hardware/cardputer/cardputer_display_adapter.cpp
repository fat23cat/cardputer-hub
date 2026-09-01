#include "hardware/cardputer/cardputer_display_adapter.h"

#include <M5Cardputer.h>

namespace cardputer_hub::hardware {
namespace {

std::uint32_t toDeviceColor(core::RgbColor color) {
    return M5Cardputer.Display.color888(color.red, color.green, color.blue);
}

} // namespace

void CardputerDisplayAdapter::clear(core::RgbColor color) {
    M5Cardputer.Display.fillScreen(toDeviceColor(color));
}

void CardputerDisplayAdapter::drawText(core::PixelPosition position, const char* text,
                                       core::TextStyle style) {
    M5Cardputer.Display.setTextColor(toDeviceColor(style.foreground),
                                     toDeviceColor(style.background));
    M5Cardputer.Display.setTextSize(style.scale);
    M5Cardputer.Display.drawString(text, position.x, position.y);
}

} // namespace cardputer_hub::hardware
