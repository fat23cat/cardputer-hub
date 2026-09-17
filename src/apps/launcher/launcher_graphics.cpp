#include "apps/launcher/launcher_graphics.h"

#include "apps/launcher/assets/app_icons.h"
#include "core/display/palette.h"

namespace cardputer_hub::apps {
namespace {
void bitmap(core::IDisplayAdapter& display, core::PixelPosition position, const char* const* rows,
            int size, core::RgbColor color) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; rows[y][x] != '\0'; ++x)
            if (rows[y][x] == '#')
                display.fillRectangle({position.x + x, position.y + y}, 1, 1, color);
}
} // namespace

void drawAppIcon(core::IDisplayAdapter& display, core::PixelPosition position, const char* iconId,
                 core::RgbColor color) {
    bitmap(display, position, assets::appIconRows(iconId), assets::appIconSize, color);
}

void drawAvailabilityDot(core::IDisplayAdapter& display, core::PixelPosition position,
                         bool available) {
    static const char* const filled[] = {" ### ", "#####", "#####", "#####", " ### "};
    static const char* const hollow[] = {" ### ", "#   #", "#   #", "#   #", " ### "};
    bitmap(display, position, available ? filled : hollow, 5,
           available ? core::palette::leaf : core::palette::vermilion);
}
} // namespace cardputer_hub::apps
