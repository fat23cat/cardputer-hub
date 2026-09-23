#include "apps/sound_reactive/sound_reactive_graphics.h"

#include "core/display/palette.h"
#include "services/microphone/microphone_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr core::RgbColor ink{6, 11, 13};

std::uint8_t channel(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}
} // namespace

core::RgbColor soundReactiveColor(float level) noexcept {
    const float value = std::clamp(level, 0.0f, 1.0f);
    if (value <= 0.5f)
        return {channel(value * 510.0f), channel(255.0f - value * 70.0f), 0};
    return {255, channel((1.0f - value) * 440.0f), 0};
}

float soundReactiveSpeed(float level) noexcept {
    return 0.15f + 2.35f * std::pow(std::clamp(level, 0.0f, 1.0f), 1.4f);
}

services::IndicatorFrame soundReactiveLedFrame(float phase, float level,
                                               core::RgbColor color) noexcept {
    services::IndicatorFrame frame{};
    const float width = 0.8f + 0.8f * std::clamp(level, 0.0f, 1.0f);
    for (int x = 0; x < 8; ++x) {
        const float center = 3.5f + 1.65f * std::sin(2.0f * pi * (x / 8.0f + phase));
        for (int y = 0; y < 8; ++y) {
            if (std::abs(y - center) <= width)
                frame.pixels[y * 8 + x] = color;
        }
    }
    return frame;
}

void drawSoundReactiveScreen(core::IDisplayAdapter& display, float phase, float level,
                             core::RgbColor color) {
    display.clear(ink);
    const float amplitude = 8.0f + 14.0f * std::clamp(level, 0.0f, 1.0f);
    for (int wave = 0; wave < 3; ++wave) {
        const float center = 36.0f + wave * 31.0f;
        for (int dot = 0; dot < 36; ++dot) {
            const float x = 5.0f + dot * 6.5f;
            const float angle = 2.0f * pi * (dot / 28.0f + phase + wave * 0.28f);
            const float y = center + amplitude * std::sin(angle);
            const int size = (dot % 4 == 0) ? 3 : 2;
            display.fillRectangle({static_cast<int>(x), static_cast<int>(y)}, size, size, color);
        }
    }
}

void drawSoundReactiveFailure(core::IDisplayAdapter& display) {
    display.clear(ink);
    display.drawText({62, 62}, "MIC UNAVAILABLE", {core::palette::bone, ink, 1});
}

void drawSoundReactiveDiagnostics(core::IDisplayAdapter& display,
                                  const services::MicrophoneSnapshot& snapshot) {
    char line[48];
    const core::TextStyle style{core::palette::bone, ink, 1};
    std::snprintf(line, sizeof(line), "WINDOWS %lu",
                  static_cast<unsigned long>(snapshot.windowCount));
    display.drawText({5, 4}, line, style);
    std::snprintf(line, sizeof(line), "PCM %d..%d", static_cast<int>(snapshot.sampleMin),
                  static_cast<int>(snapshot.sampleMax));
    display.drawText({5, 18}, line, style);
    std::snprintf(line, sizeof(line), "DBFS %.1f", static_cast<double>(snapshot.dbfs));
    display.drawText({5, 32}, line, style);
    std::snprintf(line, sizeof(line), "LEVEL %.2f BASE %.2f",
                  static_cast<double>(snapshot.normalizedLevel),
                  static_cast<double>(snapshot.ambientLevel));
    display.drawText({5, 46}, line, style);
}

} // namespace cardputer_hub::apps
