#include "apps/shell/home_ambient.h"
#include "core/display/palette.h"
#include <cmath>

namespace cardputer_hub::apps {
namespace {
constexpr double pi = 3.14159265358979323846;
double cycle(std::uint64_t milliseconds, double period) {
    return (static_cast<double>(milliseconds) / period) * (2.0 * pi);
}
} // namespace

HomeAmbientFrame homeAmbientFrame(std::uint64_t phaseMilliseconds) {
    HomeAmbientFrame frame{};
    const double breath = 1.0 + 0.08 * std::sin(cycle(phaseMilliseconds, 9000));
    const double rotation = cycle(phaseMilliseconds, 21000);
    const double morphA = cycle(phaseMilliseconds, 11000);
    const double morphB = cycle(phaseMilliseconds, 17000);
    const double centerX = 120 + 5 * std::sin(cycle(phaseMilliseconds, 16000));
    const double centerY = 79 + 3 * std::sin(cycle(phaseMilliseconds, 13000));
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const bool outer = i < 28;
        const unsigned ringIndex = outer ? static_cast<unsigned>(i) : static_cast<unsigned>(i - 28);
        const double baseAngle =
            2.0 * pi * ringIndex / (outer ? 28.0 : 12.0) + (outer ? 0.0 : 0.21);
        const double angle = baseAngle + rotation * (outer ? 1.0 : 0.76);
        const double morph = 1.0 + 0.05 * std::sin(3 * baseAngle + morphA) +
                             0.035 * std::sin(5 * baseAngle - morphB);
        const double radiusX = (outer ? 57.0 : 29.0) * breath * morph;
        const double radiusY = (outer ? 37.0 : 19.0) * breath * morph;
        const double local = 1.7 * std::sin(cycle(phaseMilliseconds, 7000 + i * 31) + i * 1.37);
        const double depth = std::sin(angle + i * 0.31);
        const std::uint8_t size = depth > 0.88 ? 3 : depth > -0.2 ? 2 : 1;
        const int x = static_cast<int>(std::lround(centerX + radiusX * std::cos(angle) + local));
        const int y = static_cast<int>(std::lround(centerY + radiusY * std::sin(angle) + local));
        frame[i] = {{x, y},
                    size,
                    size == 3   ? HomeAmbientTone::Near
                    : size == 2 ? HomeAmbientTone::Middle
                                : HomeAmbientTone::Far};
    }
    return frame;
}

void drawHomeAmbient(core::IDisplayAdapter& display, std::uint64_t phaseMilliseconds) {
    display.fillRectangle(homeAmbientOrigin, homeAmbientWidth, homeAmbientHeight,
                          core::palette::bone);
    for (const auto& particle : homeAmbientFrame(phaseMilliseconds)) {
        const auto color = particle.tone == HomeAmbientTone::Near     ? core::palette::ink
                           : particle.tone == HomeAmbientTone::Middle ? core::palette::ordinal
                                                                      : core::palette::homeWave;
        const auto left = particle.position.x - particle.size / 2;
        const auto top = particle.position.y - particle.size / 2;
        if (left >= homeAmbientOrigin.x && top >= homeAmbientOrigin.y &&
            left + particle.size <= homeAmbientOrigin.x + homeAmbientWidth &&
            top + particle.size <= homeAmbientOrigin.y + homeAmbientHeight)
            display.fillRectangle({left, top}, particle.size, particle.size, color);
    }
}
} // namespace cardputer_hub::apps
