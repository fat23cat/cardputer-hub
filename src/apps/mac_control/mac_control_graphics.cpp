#include "apps/mac_control/mac_control_graphics.h"

#include "apps/hosts/assets/micro5_digits.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <string_view>

namespace cardputer_hub::apps {
using namespace core;

namespace {
void glyph(IDisplayAdapter& display, PixelPosition position, char digit, RgbColor color) {
    constexpr std::string_view sheet = "1234560789";
    const auto index = sheet.find(digit);
    if (index == std::string_view::npos)
        return;
    const auto& bits = micro5_digits::kGlyphs[0][index];
    for (int y = 0; y < micro5_digits::kHeight; ++y) {
        for (int x = 0; x < micro5_digits::kWidth; ++x) {
            if (bits[y * micro5_digits::kStride + x / 8] & (0x80U >> (x % 8)))
                display.fillRectangle({position.x + x, position.y + y}, 1, 1, color);
        }
    }
}

void tileLabel(IDisplayAdapter& display, MacControlTileRect tile, std::int32_t y, const char* text,
               TextStyle style) {
    const auto width =
        static_cast<std::int32_t>(std::char_traits<char>::length(text)) * systemGlyphWidth;
    display.drawText({tile.origin.x + (tile.width - width) / 2, y}, text, style);
}
} // namespace

MacControlTileRect macControlTileRect(std::uint8_t slot) {
    if (slot < 1 || slot > macControlSlotCount)
        return {};
    const auto index = static_cast<int>(slot - 1);
    const auto column = index % 3;
    const auto row = index / 3;
    return {{column * macControlColumnWidth, row == 0 ? 0 : macControlRow0Height},
            macControlColumnWidth,
            row == 0 ? macControlRow0Height : macControlRow1Height};
}

void drawMacControlDigit(IDisplayAdapter& display, PixelPosition position, char digit,
                         RgbColor color) {
    glyph(display, position, digit, color);
}

void drawMacControlGrid(IDisplayAdapter& display, const MacControlPage& page) {
    display.clear(palette::bone);
    display.fillRectangle({80, 0}, 1, macControlHeight, palette::ink);
    display.fillRectangle({160, 0}, 1, macControlHeight, palette::ink);
    display.fillRectangle({0, macControlRow0Height}, macControlWidth, 1, palette::ink);
    for (std::uint8_t slot = 1; slot <= macControlSlotCount; ++slot) {
        const auto tile = macControlTileRect(slot);
        const auto* binding = macControlBindingAt(page, slot);
        const auto numberColor = binding ? palette::ink : palette::ordinal;
        drawMacControlDigit(display, {tile.origin.x + 2, tile.origin.y + 1},
                            static_cast<char>('0' + slot), numberColor);
        if (binding == nullptr)
            continue;
        const auto labelWidth = static_cast<std::int32_t>(binding->label.size()) * systemGlyphWidth;
        const auto labelX = tile.origin.x + (tile.width - labelWidth) / 2;
        const auto labelY = tile.origin.y + (tile.height - 8) / 2 + 8;
        display.drawText({labelX, labelY}, binding->label.data(), {palette::ink, palette::bone, 1});
    }
}

void drawMacControlTakeover(IDisplayAdapter& display, MacControlTileRect tile, RgbColor surface,
                            std::uint8_t slot, const MacControlBinding* binding) {
    if (tile.width <= 0 || tile.height <= 0 || slot < 1 || slot > macControlSlotCount)
        return;
    display.fillRectangle(tile.origin, tile.width, tile.height, surface);
    drawMacControlDigit(display, {tile.origin.x + 2, tile.origin.y + 1},
                        static_cast<char>('0' + slot), palette::bone);
    if (binding != nullptr)
        tileLabel(display, tile, tile.origin.y + (tile.height - 8) / 2 + 8, binding->label.data(),
                  {palette::bone, surface, 1});
}

} // namespace cardputer_hub::apps
