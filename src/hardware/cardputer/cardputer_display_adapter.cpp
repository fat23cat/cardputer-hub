#include "hardware/cardputer/cardputer_display_adapter.h"
#include "core/display/color_palette.h"
#include "core/display/display_glyphs.h"
#include "hardware/cardputer/assets/system_font_extension.h"
#include <M5Unified.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <optional>
#include <string>

namespace cardputer_hub::hardware {
namespace {
std::uint32_t toDeviceColor(core::RgbColor color) {
    return M5.Display.color888(color.red, color.green, color.blue);
}

constexpr std::size_t glyphBytes = 5;

// Font0's 256 glyphs with the Cyrillic and typographic glyphs of
// display_glyphs.h in place of its unused upper half.
const lgfx::IFont& systemFont() {
    static std::array<std::uint8_t, 256 * glyphBytes> table{};
    static const std::uint8_t info[] = {0, 255, glyphBytes};
    static const lgfx::GLCDfont font(table.data(), info, 6, 8, 7);
    static bool built = false;
    if (!built) {
        std::memcpy(table.data(), fonts::Font0.chartbl, table.size());
        for (const auto& glyph : assets::systemFontExtension) {
            auto* columns = table.data() + glyph.code * glyphBytes;
            if (glyph.latin != 0) {
                std::memcpy(columns,
                            fonts::Font0.chartbl +
                                static_cast<std::uint8_t>(glyph.latin) * glyphBytes,
                            glyphBytes);
                continue;
            }
            for (std::size_t column = 0; column < glyphBytes; ++column) {
                std::uint8_t bits = 0;
                for (int row = 0; row < 8; ++row) {
                    if (glyph.rows[row][column] == '#')
                        bits = static_cast<std::uint8_t>(bits | (1U << row));
                }
                columns[column] = bits;
            }
        }
        built = true;
    }
    return font;
}
} // namespace

// One 240x135 canvas with an 8-bit palette: 32.4 KB of internal RAM instead
// of 64.8 KB for RGB565. The interface draws a handful of exact colours, which
// take palette entries as they are first used; pushSprite expands the palette
// for the LCD. Page changes are presented at once: the slide that needed a
// second full-screen buffer was removed because, next to Wi-Fi and BLE, that
// allocation could exhaust the heap.
//
// Overlay mode (the STATUS sheet) needs no second full-screen buffer either:
// page drawing goes on into the canvas without being presented, and the sheet
// is drawn region by region into a 240x24 band (5.6 KB, the same palette) that
// is pushed once per region. Leaving overlay mode presents the whole canvas.
struct CardputerDisplayAdapter::Frame {
    static constexpr std::int32_t bandHeight = 24;

    M5Canvas canvas{&M5.Display};
    core::ColorPalette palette;
    bool usable = false;
    bool active = false;
    std::int32_t left = 240, top = 135, right = 0, bottom = 0;
    bool overlay = false;
    // Present the whole canvas at the next frame end (overlay mode ended).
    bool presentAll = false;
    // Absent when it could not be allocated: regions then draw on the LCD.
    std::unique_ptr<M5Canvas> band;
    std::optional<core::PixelRectangle> region;
    Frame() {
        canvas.setColorDepth(lgfx::color_depth_t::palette_8bit);
        usable = canvas.createSprite(240, 135) != nullptr;
    }
    // The canvas colour for `color`: its palette index.
    std::uint32_t index(core::RgbColor color) {
        const auto found = palette.indexOf(color);
        if (found.added) {
            canvas.setPaletteColor(found.index, color.red, color.green, color.blue);
            if (band)
                band->setPaletteColor(found.index, color.red, color.green, color.blue);
        }
        return found.index;
    }
    void allocateBand() {
        std::unique_ptr<M5Canvas> created(new (std::nothrow) M5Canvas(&M5.Display));
        if (!created)
            return;
        created->setColorDepth(lgfx::color_depth_t::palette_8bit);
        if (created->createSprite(240, bandHeight) == nullptr)
            return;
        for (std::size_t entry = 0; entry < palette.size(); ++entry) {
            const auto& color = palette.color(static_cast<std::uint8_t>(entry));
            created->setPaletteColor(entry, color.red, color.green, color.blue);
        }
        band = std::move(created);
    }
    void damage(core::PixelPosition position, std::int32_t width, std::int32_t height) {
        if (!active || width <= 0 || height <= 0)
            return;
        left = std::min(left, std::max<std::int32_t>(0, position.x));
        top = std::min(top, std::max<std::int32_t>(0, position.y));
        right = std::max(right, std::min<std::int32_t>(240, position.x + width));
        bottom = std::max(bottom, std::min<std::int32_t>(135, position.y + height));
    }
};

// Where drawing goes now: the page canvas, the band of an overlay region, or
// the LCD (startup, allocation failure, or a region without a band).
struct CardputerDisplayAdapter::Surface {
    lgfx::LovyanGFX* target;
    // Subtracted from screen y: the band starts at its region's top row.
    std::int32_t offsetY;
    bool indexed;
    bool page;
    // Screen-coordinate scissor of an overlay region.
    std::optional<core::PixelRectangle> clip;
};

CardputerDisplayAdapter::Surface CardputerDisplayAdapter::surface() {
    if (frame_ && frame_->overlay && frame_->region) {
        if (frame_->band)
            return {frame_->band.get(), frame_->region->origin.y, true, false, frame_->region};
        return {&M5.Display, 0, false, false, frame_->region};
    }
    // While the sheet covers the screen, page drawing stays in the canvas.
    if (frame_ && (frame_->active || frame_->overlay))
        return {&frame_->canvas, 0, true, true, std::nullopt};
    return {&M5.Display, 0, false, false, std::nullopt};
}

CardputerDisplayAdapter::CardputerDisplayAdapter() = default;
CardputerDisplayAdapter::~CardputerDisplayAdapter() = default;

void CardputerDisplayAdapter::beginFrame() {
    // Allocate only after the board/display is initialized. Keep a direct-draw
    // fallback if a canvas cannot be allocated; UI remains usable under pressure.
    if (!frame_)
        frame_.reset(new (std::nothrow) Frame());
    if (!frame_)
        return;
    frame_->active = frame_->usable;
    frame_->left = 240;
    frame_->top = 135;
    frame_->right = 0;
    frame_->bottom = 0;
}

void CardputerDisplayAdapter::endFrame() {
    if (!frame_ || !frame_->active)
        return;
    frame_->active = false;
    // The sheet covers the LCD; page damage waits for endOverlay().
    if (frame_->overlay)
        return;
    if (frame_->presentAll) {
        frame_->presentAll = false;
        frame_->left = 0;
        frame_->top = 0;
        frame_->right = 240;
        frame_->bottom = 135;
    }
    if (frame_->left < frame_->right && frame_->top < frame_->bottom) {
        // Copy only the completed dirty region: the LCD never sees a blank
        // clear followed by separately appearing text during a page change.
        M5.Display.startWrite();
        M5.Display.setClipRect(frame_->left, frame_->top, frame_->right - frame_->left,
                               frame_->bottom - frame_->top);
        frame_->canvas.pushSprite(0, 0);
        M5.Display.clearClipRect();
        M5.Display.endWrite();
    }
}

bool CardputerDisplayAdapter::beginOverlay() {
    if (!frame_)
        frame_.reset(new (std::nothrow) Frame());
    // Without the canvas, page drawing would reach the LCD over the sheet.
    if (!frame_ || !frame_->usable)
        return false;
    if (!frame_->overlay) {
        frame_->overlay = true;
        frame_->allocateBand();
    }
    return true;
}

void CardputerDisplayAdapter::endOverlay() {
    if (!frame_ || !frame_->overlay)
        return;
    frame_->region.reset();
    frame_->band.reset();
    frame_->overlay = false;
    frame_->presentAll = true;
}

void CardputerDisplayAdapter::beginOverlayRegion(core::PixelRectangle region) {
    if (!frame_ || !frame_->overlay)
        return;
    const auto left = std::max<std::int32_t>(0, region.origin.x);
    const auto top = std::max<std::int32_t>(0, region.origin.y);
    const auto right = std::min<std::int32_t>(240, region.origin.x + region.width);
    const auto bottom =
        std::min<std::int32_t>({135, region.origin.y + region.height, top + Frame::bandHeight});
    if (left >= right || top >= bottom)
        return;
    frame_->region = core::PixelRectangle{{left, top}, right - left, bottom - top};
}

void CardputerDisplayAdapter::endOverlayRegion() {
    if (!frame_ || !frame_->region)
        return;
    const auto region = *frame_->region;
    frame_->region.reset();
    if (!frame_->band)
        return;
    // One transfer per region: the LCD never shows a cleared row before its text.
    M5.Display.startWrite();
    M5.Display.setClipRect(region.origin.x, region.origin.y, region.width, region.height);
    frame_->band->pushSprite(0, region.origin.y);
    M5.Display.clearClipRect();
    M5.Display.endWrite();
}

void CardputerDisplayAdapter::clear(core::RgbColor color) {
    const auto target = surface();
    if (target.clip) {
        fillRectangle(target.clip->origin, target.clip->width, target.clip->height, color);
    } else if (target.page) {
        frame_->canvas.fillScreen(frame_->index(color));
        frame_->damage({0, 0}, 240, 135);
    } else
        M5.Display.fillScreen(toDeviceColor(color));
}

void CardputerDisplayAdapter::fillRectangle(core::PixelPosition position, std::int32_t width,
                                            std::int32_t height, core::RgbColor color) {
    const auto target = surface();
    const auto value = target.indexed ? frame_->index(color) : toDeviceColor(color);
    if (target.clip)
        target.target->setClipRect(target.clip->origin.x, target.clip->origin.y - target.offsetY,
                                   target.clip->width, target.clip->height);
    target.target->fillRect(position.x, position.y - target.offsetY, width, height, value);
    if (target.clip)
        target.target->clearClipRect();
    if (target.page)
        frame_->damage(position, width, height);
}

void CardputerDisplayAdapter::drawText(core::PixelPosition position, const char* text,
                                       core::TextStyle style) {
    const auto surfaceNow = surface();
    lgfx::LovyanGFX& target = *surfaceNow.target;
    // UTF-8 becomes one glyph byte per code point, drawn without the library's
    // own UTF-8 decoding or its classic-charset shift.
    const auto glyphs = core::displayGlyphs(text);
    target.setFont(&systemFont());
    target.setAttribute(lgfx::utf8_switch, 0);
    target.setAttribute(lgfx::cp437_switch, 1);
    if (surfaceNow.indexed)
        target.setTextColor(frame_->index(style.foreground), frame_->index(style.background));
    else
        target.setTextColor(toDeviceColor(style.foreground), toDeviceColor(style.background));
    target.setTextSize(style.scale);
    auto left = position.x;
    auto top = position.y;
    auto right = position.x + target.textWidth(glyphs.c_str());
    auto bottom = position.y + target.fontHeight();
    std::optional<core::PixelRectangle> clip = style.clip;
    if (surfaceNow.clip) {
        if (!clip)
            clip = surfaceNow.clip;
        else {
            const auto clipLeft = std::max(clip->origin.x, surfaceNow.clip->origin.x);
            const auto clipTop = std::max(clip->origin.y, surfaceNow.clip->origin.y);
            const auto clipRight = std::min(clip->origin.x + clip->width,
                                            surfaceNow.clip->origin.x + surfaceNow.clip->width);
            const auto clipBottom = std::min(clip->origin.y + clip->height,
                                             surfaceNow.clip->origin.y + surfaceNow.clip->height);
            clip = core::PixelRectangle{
                {clipLeft, clipTop}, clipRight - clipLeft, clipBottom - clipTop};
        }
    }
    if (clip) {
        left = std::max<std::int32_t>({left, clip->origin.x, 0});
        top = std::max<std::int32_t>({top, clip->origin.y, 0});
        right = std::min<std::int32_t>({right, clip->origin.x + clip->width, 240});
        bottom = std::min<std::int32_t>({bottom, clip->origin.y + clip->height, 135});
        if (left >= right || top >= bottom)
            return;
        target.setClipRect(left, top - surfaceNow.offsetY, right - left, bottom - top);
    }
    target.drawString(glyphs.c_str(), position.x, position.y - surfaceNow.offsetY);
    if (clip)
        target.clearClipRect();
    if (surfaceNow.page)
        frame_->damage({left, top}, right - left, bottom - top);
}
} // namespace cardputer_hub::hardware
