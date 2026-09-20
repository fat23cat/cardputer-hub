#include "apps/pomodoro/pomodoro_graphics.h"

#include "apps/hosts/assets/micro5_digits.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace cardputer_hub::apps {
using namespace core;
using namespace services;

namespace {
constexpr std::int32_t digitStride = 20;
constexpr std::int32_t colonWidth = 6;
constexpr std::int32_t timerWidth = digitStride * 4 + colonWidth;
constexpr std::int32_t timerX = (240 - timerWidth) / 2;
constexpr std::int32_t timerY = 36;
constexpr std::int32_t progressY = 88;
constexpr std::int32_t progressHeight = 6;
constexpr std::int32_t segmentWidth = 8;
constexpr std::int32_t segmentGap = 1;
constexpr std::int32_t progressWidth =
    pomodoroLcdSegments * segmentWidth + (pomodoroLcdSegments - 1) * segmentGap;
constexpr std::int32_t progressX = (240 - progressWidth) / 2;

void drawGlyph(IDisplayAdapter& display, PixelPosition position, char digit, RgbColor color) {
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

void drawCentered(IDisplayAdapter& display, std::int32_t y, const char* text, TextStyle style) {
    const auto width =
        static_cast<std::int32_t>(std::char_traits<char>::length(text)) * systemGlyphWidth;
    display.drawText({(240 - width) / 2, y}, text, style);
}
} // namespace

const char* pomodoroPhaseLabel(PomodoroPhase phase) noexcept {
    switch (phase) {
    case PomodoroPhase::ShortBreak:
        return "SHORT BREAK";
    case PomodoroPhase::LongBreak:
        return "LONG BREAK";
    case PomodoroPhase::Work:
        break;
    }
    return "FOCUS";
}

std::uint8_t pomodoroCycleDisplay(const PomodoroSnapshot& snapshot) noexcept {
    const auto next = static_cast<unsigned>(snapshot.completedWorkSessions) + 1U;
    return static_cast<std::uint8_t>(std::min(next, 4U));
}

std::uint8_t pomodoroLcdFilledSegments(const PomodoroSnapshot& snapshot) noexcept {
    if (snapshot.remaining.count() <= 0 || snapshot.duration.count() <= 0)
        return 0;
    const auto filled =
        (snapshot.remaining.count() * pomodoroLcdSegments + snapshot.duration.count() - 1) /
        snapshot.duration.count();
    if (filled <= 0)
        return 0;
    if (filled >= pomodoroLcdSegments)
        return static_cast<std::uint8_t>(pomodoroLcdSegments);
    return static_cast<std::uint8_t>(filled);
}

void formatPomodoroRemaining(const PomodoroSnapshot& snapshot, char (&text)[6]) noexcept {
    auto total = snapshot.remaining;
    if (total.count() < 0)
        total = std::chrono::milliseconds(0);
    const auto roundedUp = (total.count() + 999) / 1000;
    const auto minutes = static_cast<int>(roundedUp / 60);
    const auto seconds = static_cast<int>(roundedUp % 60);
    std::snprintf(text, 6, "%02d:%02d", minutes, seconds);
}

void drawPomodoroScreen(IDisplayAdapter& display, const PomodoroSnapshot& snapshot) {
    display.clear(palette::bone);
    const TextStyle ink{palette::ink, palette::bone, 1};
    const TextStyle ordinal{palette::ordinal, palette::bone, 1};
    display.drawText({6, 6}, pomodoroPhaseLabel(snapshot.phase), ink);
    char cycle[8] = {};
    std::snprintf(cycle, sizeof(cycle), "%u / 4",
                  static_cast<unsigned>(pomodoroCycleDisplay(snapshot)));
    display.drawText({rightAlignedTextX(cycle), 6}, cycle, ordinal);

    char remaining[6] = {};
    formatPomodoroRemaining(snapshot, remaining);
    auto x = timerX;
    for (int i = 0; i < 2; ++i) {
        drawGlyph(display, {x, timerY}, remaining[i], palette::ink);
        x += digitStride;
    }
    display.fillRectangle({x + 2, timerY + 8}, 2, 2, palette::ink);
    display.fillRectangle({x + 2, timerY + 18}, 2, 2, palette::ink);
    x += colonWidth;
    for (int i = 3; i < 5; ++i) {
        drawGlyph(display, {x, timerY}, remaining[i], palette::ink);
        x += digitStride;
    }

    const auto filled = pomodoroLcdFilledSegments(snapshot);
    const auto fillColor = snapshot.phase == PomodoroPhase::Work ? palette::blue : palette::leaf;
    for (std::int32_t i = 0; i < pomodoroLcdSegments; ++i) {
        const auto segmentX = progressX + i * (segmentWidth + segmentGap);
        display.fillRectangle({segmentX, progressY}, segmentWidth, progressHeight,
                              i < filled ? fillColor : palette::pale);
    }

    if (snapshot.runState == PomodoroRunState::Paused)
        drawCentered(display, 118, "PAUSED", ink);
    else if (snapshot.runState == PomodoroRunState::Idle)
        drawCentered(display, 118, "SPACE  START", ordinal);
    else
        drawCentered(display, 118, "SPACE  PAUSE", ordinal);
}

} // namespace cardputer_hub::apps
