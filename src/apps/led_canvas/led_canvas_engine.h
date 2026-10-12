#pragma once
#include "core/indicator/led_adapter.h"
#include "core/indicator/led_canvas_types.h"
#include <chrono>
namespace cardputer_hub::apps {
class LedCanvasEngine {
  public:
    void setSettings(core::LedCanvasSettings settings) { settings_ = settings; }
    void clear();
    void point(const core::LedCanvasPlayback& point);
    void endPointers(std::uint32_t mask);
    void advance(std::chrono::milliseconds elapsed);
    [[nodiscard]] core::LedHardwareFrame frame() const;
    [[nodiscard]] bool live() const;

  private:
    using Color = std::array<float, 3>;
    struct Pointer {
        bool down = false;
        float x = 0, y = 0, radius = 0.75f, speed = 0;
        int led = -1;
        std::uint32_t stroke = 0;
        std::uint64_t time = 0;
        float hue = 0;
        bool glow = true;
    };
    struct Ring {
        float x = 0, y = 0, age = 0;
        Color color{};
    };
    static Color hsv(float hue);
    static Color ramp(const std::array<Color, 5>& colors, float amount);
    void brush(Pointer& pointer, float x, float y, bool entered);
    void dab(std::size_t led, const Color& color, float weight);
    std::array<Color, 64> rgb_{};
    std::array<float, 64> heat_{};
    std::array<Pointer, 20> pointers_{};
    std::array<Ring, 10> rings_{};
    std::size_t ringCount_ = 0;
    float hue_ = 0;
    std::uint32_t strokes_ = 0;
    std::uint64_t clock_ = 0;
    core::LedCanvasSettings settings_;
};
} // namespace cardputer_hub::apps
