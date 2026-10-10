#include "apps/pomodoro/pomodoro_graphics.h"

#include "apps/hosts/assets/micro5_digits.h"
#include "core/display/contextual_footer.h"
#include "core/display/monochrome_bitmap.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
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
// Below the progress bar: the PAUSED label and the footer hint.
constexpr std::int32_t runStateY = 104;
constexpr std::int32_t pausedY = 106;

void drawGlyph(IDisplayAdapter& display, PixelPosition position, char digit, RgbColor color) {
    constexpr std::string_view sheet = "1234560789";
    const auto index = sheet.find(digit);
    if (index == std::string_view::npos)
        return;
    const auto& bits = micro5_digits::kGlyphs[0][index];
    drawMonochromeBitmap(display, position, bits, micro5_digits::kWidth, micro5_digits::kHeight,
                         micro5_digits::kStride, color);
}

void drawCentered(IDisplayAdapter& display, std::int32_t y, const char* text, TextStyle style) {
    display.drawText({centeredTextX(text, 0, 240, style.scale), y}, text, style);
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
    auto roundedUp = (total.count() + 999) / 1000;
    constexpr auto maxDisplayedSeconds = 99 * 60 + 59;
    if (roundedUp > maxDisplayedSeconds)
        roundedUp = maxDisplayedSeconds;
    const auto minutes = static_cast<unsigned>(roundedUp / 60);
    const auto seconds = static_cast<unsigned>(roundedUp % 60);
    text[0] = static_cast<char>('0' + minutes / 10);
    text[1] = static_cast<char>('0' + minutes % 10);
    text[2] = ':';
    text[3] = static_cast<char>('0' + seconds / 10);
    text[4] = static_cast<char>('0' + seconds % 10);
    text[5] = '\0';
}

void drawPomodoroScreen(IDisplayAdapter& display, const PomodoroSnapshot& snapshot,
                        const PomodoroSnapshot* previous) {
    if (!previous)
        display.clear(palette::bone);
    const TextStyle ink{palette::ink, palette::bone, systemTextScale};
    if (!previous || previous->phase != snapshot.phase ||
        pomodoroCycleDisplay(*previous) != pomodoroCycleDisplay(snapshot)) {
        if (previous)
            display.fillRectangle({0, 0}, 240, screenHeaderHeight, palette::bone);
        char cycle[8] = {};
        std::snprintf(cycle, sizeof(cycle), "%u/4",
                      static_cast<unsigned>(pomodoroCycleDisplay(snapshot)));
        drawScreenHeader(display, pomodoroPhaseLabel(snapshot.phase), cycle);
    }

    char remaining[6] = {};
    formatPomodoroRemaining(snapshot, remaining);
    char oldRemaining[6] = {};
    if (previous)
        formatPomodoroRemaining(*previous, oldRemaining);
    auto x = timerX;
    for (int i = 0; i < 5; ++i) {
        if (i == 2) {
            if (!previous) {
                display.fillRectangle({x + 2, timerY + 8}, 2, 2, palette::ink);
                display.fillRectangle({x + 2, timerY + 18}, 2, 2, palette::ink);
            }
            x += colonWidth;
            continue;
        }
        if (!previous || oldRemaining[i] != remaining[i]) {
            if (previous)
                display.fillRectangle({x, timerY}, micro5_digits::kWidth, micro5_digits::kHeight,
                                      palette::bone);
            drawGlyph(display, {x, timerY}, remaining[i], palette::ink);
        }
        x += digitStride;
    }

    const auto filled = pomodoroLcdFilledSegments(snapshot);
    const auto fillColor = snapshot.phase == PomodoroPhase::Work ? palette::blue : palette::leaf;
    const auto previousFilled = previous ? pomodoroLcdFilledSegments(*previous) : 0;
    const auto previousFillColor =
        previous && previous->phase == PomodoroPhase::Work ? palette::blue : palette::leaf;
    for (std::int32_t i = 0; i < pomodoroLcdSegments; ++i) {
        const auto color = i < filled ? fillColor : palette::pale;
        const auto previousColor = i < previousFilled ? previousFillColor : palette::pale;
        if (previous && color.red == previousColor.red && color.green == previousColor.green &&
            color.blue == previousColor.blue)
            continue;
        const auto segmentX = progressX + i * (segmentWidth + segmentGap);
        display.fillRectangle({segmentX, progressY}, segmentWidth, progressHeight, color);
    }

    if (previous && previous->runState == snapshot.runState)
        return;
    if (previous)
        display.fillRectangle({0, runStateY}, 240, 135 - runStateY, palette::bone);
    if (snapshot.runState == PomodoroRunState::Paused) {
        drawCentered(display, pausedY, "PAUSED", ink);
        drawContextualFooter(display, "", "SPACE  RESUME");
    } else if (snapshot.runState == PomodoroRunState::Idle) {
        drawContextualFooter(display, "", "SPACE  START");
    } else {
        drawContextualFooter(display, "", "SPACE  PAUSE");
    }
}

} // namespace cardputer_hub::apps
