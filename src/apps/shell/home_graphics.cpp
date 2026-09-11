#include "apps/shell/home_graphics.h"
#include "apps/shell/assets/micro5_home.h"
#include "core/display/palette.h"
#include <cmath>
namespace cardputer_hub::apps {
namespace {
unsigned glyphIndex(char c) { return c >= 32 && c <= 126 ? unsigned(c - 32) : unsigned('?' - 32); }
int textWidth(const std::string& text) {
    int width = 0;
    for (const auto c : text)
        width += assets::homeGlyphAdvance[glyphIndex(c)];
    return width;
}
void bitmap(core::IDisplayAdapter& display, core::PixelPosition position, const char* const* rows,
            int height, core::RgbColor color) {
    for (int y = 0; y < height; ++y)
        for (int x = 0; rows[y][x] != '\0'; ++x)
            if (rows[y][x] == '#')
                display.fillRectangle({position.x + x, position.y + y}, 1, 1, color);
}
} // namespace
std::string fitHomeHostName(const std::string& name) {
    std::string label = name;
    for (auto& c : label)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    if (textWidth(label) <= 224)
        return label;
    while (!label.empty() && textWidth(label + "...") > 224)
        label.pop_back();
    return label + "...";
}
void drawHomeHostName(core::IDisplayAdapter& display, const std::string& name) {
    int x = 8;
    for (const auto c : fitHomeHostName(name)) {
        const auto index = glyphIndex(c);
        const auto* mask = assets::homeGlyphs[index];
        // Fixed baseline: uppercase ink occupies y=55..70 at this optical size.
        for (int y = 0; y < 36; ++y) {
            for (int col = 0; col < 32;) {
                if (!(mask[y * 4 + col / 8] & (0x80 >> (col % 8)))) {
                    ++col;
                    continue;
                }
                const auto start = col;
                do {
                    ++col;
                } while (col < 32 && (mask[y * 4 + col / 8] & (0x80 >> (col % 8))));
                display.fillRectangle({x + start, 45 + y}, col - start, 1, core::palette::ink);
            }
        }
        x += assets::homeGlyphAdvance[index];
    }
}
void drawBluetoothIcon(core::IDisplayAdapter& display, core::RgbColor color) {
    static const char* const rows[] = {
        "     #      ", "     ##     ", "     # #    ", " #   #  #   ", "  #  # #    ",
        "   # ##     ", "    ##      ", "    ##      ", "   # ##     ", "  #  # #    ",
        " #   #  #   ", "     # #    ", "     ##     ", "     #      "};
    bitmap(display, {8, 80}, rows, 14, color);
}
void drawWifiOfflineIcon(core::IDisplayAdapter& display) {
    static const char* const rows[] = {
        "              ", "   ########   ", " ##        ## ", "#            #", "              ",
        "    ######    ", "  ##      ##  ", "              ", "     ####     ", "    #    #    ",
        "              ", "      ##      ", "      ##      ", "              "};
    bitmap(display, {84, 5}, rows, 14, core::palette::ordinal);
}
void drawHomeWave(core::IDisplayAdapter& display, unsigned phaseMilliseconds) {
    display.fillRectangle({0, 99}, 240, 36, core::palette::bone);
    const double phase = phaseMilliseconds * (6.283185307179586 / 28000.0);
    for (int band = 0; band < 5; ++band)
        for (int x = 0; x < 240; x += 3) {
            const int y =
                int(std::lround(104 + band * 7 + std::sin(x / 34.0 + phase + band * .45) * 5));
            if (y >= 99 && y < 135)
                display.fillRectangle({x, y}, 1, 1, core::palette::homeWave);
        }
}
} // namespace cardputer_hub::apps
