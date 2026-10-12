#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace cardputer_hub::core {
enum class LedCanvasMode : std::uint8_t {
    Rainbow,
    Embers,
    Velocity,
    Mix,
    Ripple,
    Kaleidoscope,
    Solid
};
enum class LedCanvasFade : std::uint8_t { Short, Medium, Long };
inline constexpr std::array<const char*, 7> ledCanvasModeIds{
    {"rainbow", "embers", "velocity", "mix", "ripple", "kaleidoscope", "solid"}};
inline constexpr std::array<const char*, 7> ledCanvasModeNames{
    {"RAINBOW", "EMBERS", "VELOCITY", "MIX", "RIPPLE", "KALEIDOSCOPE", "SOLID"}};
inline constexpr std::array<const char*, 3> ledCanvasFadeIds{{"short", "medium", "long"}};
struct LedCanvasSettings {
    LedCanvasMode mode = LedCanvasMode::Rainbow;
    LedCanvasFade fade = LedCanvasFade::Medium;
    std::uint8_t ink = 0;
    bool hold = false;
};
inline constexpr std::uint16_t ledCanvasHueUnits = 10000;
struct LedCanvasPoint {
    std::uint8_t slot = 0;
    std::uint16_t x = 0, y = 0;
    std::uint8_t radius = 12, dt = 0, flags = 0;
    // Optional input color phase; RGB, halo and animation remain computed by the engine.
    std::uint16_t hue = 0;
    std::uint8_t mixInk = 0;
    bool hasColor = false;
};
struct LedCanvasBatch {
    std::uint32_t client = 0, seq = 0;
    std::array<LedCanvasPoint, 64> points{};
    std::size_t count = 0;
};
struct LedCanvasPlayback {
    LedCanvasPoint point;
    std::uint8_t pointer = 0;
    std::uint64_t time = 0;
};
} // namespace cardputer_hub::core
