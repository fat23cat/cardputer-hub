#include "apps/led_canvas/led_canvas_engine.h"
#include <algorithm>
#include <cmath>
namespace cardputer_hub::apps {
namespace {
using Color = std::array<float, 3>;
constexpr std::array<Color, 5> ember{
    {{0, 0, 0}, {110, 8, 0}, {226, 60, 20}, {255, 150, 30}, {255, 236, 190}}};
constexpr std::array<Color, 5> velocity{
    {{20, 50, 200}, {0, 170, 220}, {230, 60, 30}, {255, 190, 40}, {255, 255, 235}}};
constexpr std::array<Color, 3> mix{{{255, 30, 20}, {30, 255, 40}, {30, 70, 255}}};
constexpr std::array<Color, 7> inks{{{255, 70, 30},
                                     {255, 176, 0},
                                     {70, 220, 60},
                                     {0, 200, 255},
                                     {40, 90, 255},
                                     {180, 70, 255},
                                     {255, 255, 255}}};
constexpr std::array<float, 3> fades{{0.35f, 1.0f, 3.0f}};
} // namespace
void LedCanvasEngine::clear() {
    rgb_ = {};
    heat_ = {};
    pointers_ = {};
    ringCount_ = 0;
    hue_ = 0;
    strokes_ = 0;
}
void LedCanvasEngine::endPointers(std::uint32_t mask) {
    for (std::size_t i = 0; i < pointers_.size(); ++i)
        if (mask & (std::uint32_t{1} << i))
            pointers_[i].down = false;
}
LedCanvasEngine::Color LedCanvasEngine::hsv(float hue) {
    hue -= std::floor(hue);
    const float h = hue * 6;
    const int i = static_cast<int>(h);
    const float f = h - i;
    const std::array<Color, 6> values{{{255, f * 255, 0},
                                       {(1 - f) * 255, 255, 0},
                                       {0, 255, f * 255},
                                       {0, (1 - f) * 255, 255},
                                       {f * 255, 0, 255},
                                       {255, 0, (1 - f) * 255}}};
    return values[static_cast<std::size_t>(i) % 6];
}
LedCanvasEngine::Color LedCanvasEngine::ramp(const std::array<Color, 5>& colors, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f) * 4;
    const auto i = std::min<std::size_t>(3, static_cast<std::size_t>(t));
    Color result{};
    for (std::size_t j = 0; j < 3; ++j)
        result[j] = colors[i][j] + (colors[i + 1][j] - colors[i][j]) * (t - i);
    return result;
}
void LedCanvasEngine::dab(std::size_t led, const Color& color, float weight) {
    for (std::size_t j = 0; j < 3; ++j)
        rgb_[led][j] = weight >= 1 ? color[j] : std::max(rgb_[led][j], color[j] * weight);
}
void LedCanvasEngine::brush(Pointer& p, float fx, float fy, bool entered) {
    using core::LedCanvasMode;
    const auto mode = settings_.mode;
    Color color{};
    if (mode == LedCanvasMode::Rainbow) {
        if (entered) {
            hue_ += 0.04f;
            p.hue = hue_;
        }
        color = hsv(p.hue);
    } else if (mode == LedCanvasMode::Kaleidoscope) {
        if (entered) {
            hue_ += 0.025f;
            p.hue = hue_;
        }
    } else if (mode == LedCanvasMode::Velocity)
        color = ramp(velocity, p.speed / 16);
    else if (mode == LedCanvasMode::Mix) {
        if (!entered)
            return;
        color = mix[p.stroke % 3];
    } else if (mode == LedCanvasMode::Solid)
        color = inks[settings_.ink];
    else if (mode == LedCanvasMode::Ripple) {
        color = {255, 255, 255};
        if (entered) {
            hue_ += 0.11f;
            if (ringCount_ == rings_.size()) {
                std::move(rings_.begin() + 1, rings_.end(), rings_.begin());
                --ringCount_;
            }
            rings_[ringCount_++] = {fx, fy, 0, hsv(hue_)};
        }
    }
    hue_ -= std::floor(hue_);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const float d = std::hypot(x + 0.5f - fx, y + 0.5f - fy);
            const float w =
                (x == static_cast<int>(fx) && y == static_cast<int>(fy)) || d <= p.radius ? 1
                : p.glow ? std::max(0.0f, 0.45f * (1 - (d - p.radius) / 1.3f))
                         : 0;
            if (w <= 0)
                continue;
            const auto led = static_cast<std::size_t>(y * 8 + x);
            if (mode == LedCanvasMode::Embers)
                heat_[led] = std::max(heat_[led], w);
            else if (mode == LedCanvasMode::Mix) {
                for (std::size_t j = 0; j < 3; ++j)
                    rgb_[led][j] =
                        std::min(255.0f, rgb_[led][j] + color[j] * (w < 1 ? w * 0.5f : 1));
            } else if (mode == LedCanvasMode::Kaleidoscope) {
                dab(led, hsv(p.hue), w);
                dab(y * 8 + 7 - x, hsv(p.hue + 0.25f), w);
                dab((7 - y) * 8 + 7 - x, hsv(p.hue + 0.5f), w);
                dab((7 - y) * 8 + x, hsv(p.hue + 0.75f), w);
            } else
                dab(led, color, w);
        }
}
void LedCanvasEngine::point(const core::LedCanvasPlayback& event) {
    if (event.pointer >= pointers_.size())
        return;
    clock_ = std::max(clock_, event.time);
    auto& p = pointers_[event.pointer];
    const auto& q = event.point;
    const float x = std::min(7.999f, q.x / 256.0f), y = std::min(7.999f, q.y / 256.0f),
                r = q.radius / 16.0f;
    if (!p.down || (q.flags & 1))
        p = {true, x, y, r, 0, -1, strokes_++, event.time, hue_};
    else
        p.radius = r == 0 ? 0 : 0.7f * p.radius + 0.3f * r;
    p.glow = q.glow;
    const float ox = p.x, oy = p.y, d = std::hypot(x - ox, y - oy);
    const float dt =
        std::max(0.008f, event.time > p.time ? (event.time - p.time) / 1000.0f : 0.008f);
    p.speed = 0.6f * p.speed + 0.4f * d / dt;
    p.x = x;
    p.y = y;
    p.time = event.time;
    const int samples = std::max(1, static_cast<int>(std::ceil(d / 0.5f)));
    if (q.hasColor) {
        p.stroke = q.mixInk;
        const auto mode = settings_.mode;
        const float step = mode == core::LedCanvasMode::Rainbow        ? 0.04f
                           : mode == core::LedCanvasMode::Kaleidoscope ? 0.025f
                           : mode == core::LedCanvasMode::Ripple       ? 0.11f
                                                                       : 0;
        if (step > 0) {
            int entered = p.led < 0 ? 1 : 0, previous = p.led;
            if (previous >= 0)
                for (int k = 1; k <= samples; ++k) {
                    const float sx = ox + (x - ox) * k / samples, sy = oy + (y - oy) * k / samples;
                    const int led = static_cast<int>(sy) * 8 + static_cast<int>(sx);
                    if (led != previous)
                        ++entered;
                    previous = led;
                }
            // The stamp is the ending phase: recover a smooth sampled path even when
            // intermediate browser points were thinned, then end at the preview's color.
            const float target = q.hue / static_cast<float>(core::ledCanvasHueUnits);
            hue_ = target - entered * step;
            hue_ -= std::floor(hue_);
            if (entered == 0)
                p.hue = target;
        }
    }
    if (p.led < 0) {
        p.led = static_cast<int>(y) * 8 + static_cast<int>(x);
        brush(p, x, y, true);
    } else {
        const int n = samples;
        for (int k = 1; k <= n; ++k) {
            const float sx = ox + (x - ox) * k / n, sy = oy + (y - oy) * k / n;
            const int led = static_cast<int>(sy) * 8 + static_cast<int>(sx);
            brush(p, sx, sy, p.led != led);
            p.led = led;
        }
    }
    if (q.flags & 2)
        p.down = false;
}
void LedCanvasEngine::advance(std::chrono::milliseconds elapsed) {
    if (elapsed.count() <= 0)
        return;
    clock_ += static_cast<std::uint64_t>(elapsed.count());
    const float dt = elapsed.count() / 1000.0f;
    const float fade = fades[static_cast<std::size_t>(settings_.fade)];
    for (auto& p : pointers_)
        if (p.down) {
            if (clock_ > p.time + 60)
                p.speed *= std::exp(-5 * dt);
            brush(p, p.x, p.y, false);
        }
    const float decay = settings_.hold ? 1 : std::exp(-dt / fade);
    for (auto& c : rgb_)
        for (auto& v : c) {
            v *= decay;
            if (v < 2)
                v = 0;
        }
    if (!settings_.hold)
        for (auto& h : heat_)
            h = std::max(0.0f, h - dt / (fade * 1.4f));
    std::size_t kept = 0;
    for (std::size_t i = 0; i < ringCount_; ++i) {
        auto g = rings_[i];
        g.age += dt;
        if (g.age > 1.3f)
            continue;
        for (std::size_t led = 0; led < 64; ++led) {
            const float d = std::hypot((led % 8) + 0.5f - g.x, (led / 8) + 0.5f - g.y);
            const float w = (1 - std::abs(d - g.age * 6) / 0.9f) * (1 - g.age / 1.3f);
            if (w > 0)
                dab(led, g.color, w);
        }
        rings_[kept++] = g;
    }
    ringCount_ = kept;
}
core::LedHardwareFrame LedCanvasEngine::frame() const {
    core::LedHardwareFrame result;
    for (std::size_t i = 0; i < 64; ++i) {
        auto c = rgb_[i];
        if (heat_[i] > 0) {
            const auto h = ramp(ember, heat_[i]);
            for (std::size_t j = 0; j < 3; ++j)
                c[j] = std::max(c[j], h[j]);
        }
        result.pixels[i] = {static_cast<std::uint8_t>(std::clamp(std::round(c[0]), 0.0f, 255.0f)),
                            static_cast<std::uint8_t>(std::clamp(std::round(c[1]), 0.0f, 255.0f)),
                            static_cast<std::uint8_t>(std::clamp(std::round(c[2]), 0.0f, 255.0f))};
    }
    return result;
}
bool LedCanvasEngine::live() const {
    if (ringCount_)
        return true;
    for (const auto& p : pointers_)
        if (p.down)
            return true;
    for (std::size_t i = 0; i < 64; ++i)
        if (heat_[i] > 0 || rgb_[i][0] >= 2 || rgb_[i][1] >= 2 || rgb_[i][2] >= 2)
            return true;
    return false;
}
} // namespace cardputer_hub::apps
