#include "apps/led_gallery/led_gallery_engine.h"

#include <algorithm>
#include <cmath>

namespace cardputer_hub::apps {
namespace {
constexpr float pi = 3.14159265f;
float clamp(float value, float low = 0, float high = 1) {
    return std::max(low, std::min(high, value));
}
float wave(float x) { return std::sin(x); }
core::RgbColor hsv(float hue, float saturation, float value) {
    hue -= std::floor(hue);
    const float c = clamp(value) * clamp(saturation);
    const float h = hue * 6;
    const float x = c * (1 - std::fabs(std::fmod(h, 2.f) - 1));
    float r = 0, g = 0, b = 0;
    if (h < 1) {
        r = c;
        g = x;
    } else if (h < 2) {
        r = x;
        g = c;
    } else if (h < 3) {
        g = c;
        b = x;
    } else if (h < 4) {
        g = x;
        b = c;
    } else if (h < 5) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const float m = clamp(value) - c;
    return {static_cast<std::uint8_t>(255 * (r + m)), static_cast<std::uint8_t>(255 * (g + m)),
            static_cast<std::uint8_t>(255 * (b + m))};
}
void blend(services::IndicatorFrame& frame, int x, int y, core::RgbColor color,
           float strength = 1) {
    if (x < 0 || x >= 8 || y < 0 || y >= 8)
        return;
    auto& pixel = frame.pixels[static_cast<std::size_t>(y * 8 + x)];
    const auto add = [strength](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::min(255.f, a + b * clamp(strength)));
    };
    pixel = {add(pixel.red, color.red), add(pixel.green, color.green), add(pixel.blue, color.blue)};
}
} // namespace

namespace detail {
void glow(services::IndicatorFrame& frame, float x, float y, core::RgbColor color, float power) {
    constexpr float threshold = .015f;
    constexpr float falloff = 1.25f;
    if (power <= threshold)
        return;
    const float radius = std::sqrt(std::log(power / threshold) / falloff);
    const int minX = std::max(0, static_cast<int>(std::ceil(x - radius)));
    const int maxX = std::min(7, static_cast<int>(std::floor(x + radius)));
    const int minY = std::max(0, static_cast<int>(std::ceil(y - radius)));
    const int maxY = std::min(7, static_cast<int>(std::floor(y + radius)));
    for (int py = minY; py <= maxY; ++py)
        for (int px = minX; px <= maxX; ++px) {
            const float dx = px - x, dy = py - y;
            const float weight = power * std::exp(-(dx * dx + dy * dy) * falloff);
            if (weight > threshold)
                blend(frame, px, py, color, weight);
        }
}
} // namespace detail

LedGalleryEngine::LedGalleryEngine(std::uint32_t seed) { reset(LedGalleryEffect::Plasma, seed); }
std::uint32_t LedGalleryEngine::random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}
float LedGalleryEngine::unitRandom() { return (random() & 0xffffU) / 65535.f; }
void LedGalleryEngine::initParticle(Particle& p, std::size_t index) {
    p = {};
    p.phase = unitRandom() * 2 * pi;
    p.hue = unitRandom();
    const float angle = unitRandom() * 2 * pi;
    switch (effect_) {
    case LedGalleryEffect::Warp:
        p.x = 3.5f + std::cos(angle) * (.1f + unitRandom());
        p.y = 3.5f + std::sin(angle) * (.1f + unitRandom());
        p.vx = std::cos(angle) * (1 + unitRandom());
        p.vy = std::sin(angle) * (1 + unitRandom());
        p.life = 3.5f;
        break;
    case LedGalleryEffect::Comets:
        p.x = -static_cast<float>(index * 3);
        p.y = unitRandom() * 7;
        p.vx = 2 + unitRandom() * 2;
        p.vy = (unitRandom() - .5f) * 1.4f;
        p.life = 8;
        break;
    case LedGalleryEffect::Fireflies:
        p.x = unitRandom() * 7;
        p.y = unitRandom() * 7;
        p.vx = (unitRandom() - .5f) * .5f;
        p.vy = (unitRandom() - .5f) * .5f;
        p.hue = .13f + unitRandom() * .36f;
        p.life = 1000;
        break;
    case LedGalleryEffect::Vortex:
        p.x = 2 + unitRandom() * 5;        // radius
        p.y = angle;                       // angle
        p.vx = .5f + unitRandom() * .6f;   // angular speed
        p.vy = .18f + unitRandom() * .18f; // radial speed
        p.life = 1000;
        break;
    default:
        break;
    }
}
void LedGalleryEngine::reset(LedGalleryEffect effect, std::uint32_t seed) {
    effect_ = effect;
    rng_ = seed ? seed : 0x6d2b79f5U;
    time_ = 0;
    ambientAccumulator_ = 0;
    sequence_ = 0;
    particles_.fill({});
    ripples_.fill({});
    std::size_t count = 0;
    switch (effect_) {
    case LedGalleryEffect::Warp:
        count = 14;
        break;
    case LedGalleryEffect::Comets:
        count = 3;
        break;
    case LedGalleryEffect::Fireflies:
        count = 7;
        break;
    case LedGalleryEffect::Vortex:
        count = 16;
        break;
    default:
        break;
    }
    for (std::size_t i = 0; i < count; ++i)
        initParticle(particles_[i], i);
    if (effect_ == LedGalleryEffect::ParticleStorm)
        for (std::size_t i = 0; i < 4; ++i) {
            auto& p = particles_[i];
            p.x = unitRandom() * 7;
            p.y = unitRandom() * 7;
            p.vx = (unitRandom() - .5f) * .7f;
            p.vy = (unitRandom() - .5f) * .7f;
            p.hue = unitRandom();
            p.life = 12;
        }
}
void LedGalleryEngine::addRipple(bool manual) {
    std::size_t slot = ripples_.size();
    float oldest = -1;
    for (std::size_t i = 0; i < ripples_.size(); ++i) {
        if (!ripples_[i].active) {
            slot = i;
            break;
        }
        if (ripples_[i].age > oldest) {
            oldest = ripples_[i].age;
            slot = i;
        }
    }
    auto& r = ripples_[slot];
    r = {manual ? static_cast<float>((sequence_ * 3U + rng_) % 8U) : unitRandom() * 7,
         manual ? static_cast<float>((sequence_ * 5U + rng_ / 8U) % 8U) : unitRandom() * 7, 0,
         .48f + unitRandom() * .2f, true};
    ++sequence_;
}
void LedGalleryEngine::burst(char key, bool large) {
    std::uint32_t hash = static_cast<unsigned char>(key) * 2654435761U + sequence_++;
    const float cx = large ? 3.5f : 1.f + static_cast<float>(hash % 6U);
    const float cy = large ? 3.5f : 1.f + static_cast<float>((hash >> 8) % 6U);
    const float hue = ((hash >> 16) & 255U) / 255.f;
    const int count = large ? 10 : 5;
    for (int i = 0; i < count; ++i) {
        std::size_t slot = (sequence_ + static_cast<std::uint32_t>(i)) % particles_.size();
        float oldest = -1;
        for (std::size_t j = 0; j < particles_.size(); ++j) {
            if (particles_[j].life <= 0 || particles_[j].age >= particles_[j].life) {
                slot = j;
                break;
            }
            if (particles_[j].age > oldest) {
                oldest = particles_[j].age;
                slot = j;
            }
        }
        auto& p = particles_[slot];
        const float angle = 2 * pi * (i + unitRandom() * .5f) / count;
        p = {cx,
             cy,
             std::cos(angle) * (1.5f + unitRandom() * 2),
             std::sin(angle) * (1.5f + unitRandom() * 2),
             0,
             1.5f + unitRandom(),
             hue + i * .015f,
             0};
    }
}
void LedGalleryEngine::interact(char key) {
    if (effect_ == LedGalleryEffect::Ripple && key == ' ')
        addRipple(true);
    else if (effect_ == LedGalleryEffect::ParticleStorm) {
        if (key == ' ')
            burst(key, true);
        else if (key >= 33 && key <= 126)
            burst(key, false);
    }
}
void LedGalleryEngine::advance(std::chrono::milliseconds elapsed) {
    if (elapsed.count() <= 0)
        return;
    const float rawDt = elapsed.count() / 1000.f;
    const float dt = std::min(2.f, rawDt);
    time_ += rawDt;
    if (effect_ == LedGalleryEffect::Ripple) {
        for (auto& r : ripples_)
            if (r.active) {
                r.age += dt;
                if (r.age > 2.7f)
                    r.active = false;
            }
        ambientAccumulator_ += dt;
        if (ambientAccumulator_ >= 3.5f) {
            ambientAccumulator_ = 0;
            addRipple(false);
        }
    }
    for (auto& p : particles_) {
        if (p.life <= 0 || p.age >= p.life)
            continue;
        p.age += dt;
        if (p.age >= p.life && effect_ != LedGalleryEffect::ParticleStorm) {
            initParticle(p, 0);
            continue;
        }
        switch (effect_) {
        case LedGalleryEffect::Warp:
            p.x += p.vx * dt * (1 + p.age);
            p.y += p.vy * dt * (1 + p.age);
            if (p.x < -1 || p.x > 8 || p.y < -1 || p.y > 8)
                initParticle(p, 0);
            break;
        case LedGalleryEffect::Comets:
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.x > 11 || p.y < -2 || p.y > 9)
                initParticle(p, 0);
            break;
        case LedGalleryEffect::Fireflies:
            p.vx = clamp(p.vx + wave(time_ * .7f + p.phase) * dt * .08f, -.4f, .4f);
            p.vy = clamp(p.vy + wave(time_ * .6f + p.phase * 1.7f) * dt * .08f, -.4f, .4f);
            p.x = clamp(p.x + p.vx * dt, 0, 7);
            p.y = clamp(p.y + p.vy * dt, 0, 7);
            break;
        case LedGalleryEffect::Vortex:
            p.y += p.vx * dt;
            p.x -= p.vy * dt;
            if (p.x < .2f)
                initParticle(p, 0);
            break;
        case LedGalleryEffect::ParticleStorm:
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vx *= std::exp(-dt * .6f);
            p.vy *= std::exp(-dt * .6f);
            break;
        default:
            break;
        }
    }
    if (effect_ == LedGalleryEffect::ParticleStorm) {
        ambientAccumulator_ += dt;
        if (ambientAccumulator_ > 1.8f) {
            ambientAccumulator_ = 0;
            burst(static_cast<char>('a' + random() % 26U), false);
        }
    }
}
std::size_t LedGalleryEngine::activeRipples() const noexcept {
    std::size_t n = 0;
    for (const auto& r : ripples_)
        if (r.active)
            ++n;
    return n;
}
std::size_t LedGalleryEngine::activeParticles() const noexcept {
    std::size_t n = 0;
    for (const auto& p : particles_)
        if (p.life > 0 && p.age < p.life)
            ++n;
    return n;
}
services::IndicatorFrame LedGalleryEngine::frame() const {
    services::IndicatorFrame out{};
    const float seedPhase = (rng_ & 255U) * .021f;
    const float kaleidoOrientation = effect_ == LedGalleryEffect::Kaleidoscope
                                         ? time_ * .19f + .35f * wave(time_ * .11f + seedPhase)
                                         : 0.f;
    const float kaleidoRadialFrequency =
        effect_ == LedGalleryEffect::Kaleidoscope ? 3.2f + .25f * wave(time_ * .17f) : 0.f;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            core::RgbColor color{};
            switch (effect_) {
            case LedGalleryEffect::Plasma: {
                const float v = wave(fx * .7f + time_ * .6f + seedPhase) +
                                wave(fy * .8f - time_ * .47f) +
                                wave(std::hypot(fx - 3.5f, fy - 3.5f) * 1.1f - time_ * .55f);
                color = hsv(v * .11f + time_ * .045f + seedPhase, .85f,
                            .65f + .35f * wave(v) * wave(v));
                break;
            }
            case LedGalleryEffect::Lava: {
                float field = 0;
                for (int i = 0; i < 4; ++i) {
                    const float bx =
                        3.5f + 3 * wave(time_ * (.24f + i * .04f) + i * 1.7f + seedPhase);
                    const float by = 3.5f + 3 * wave(time_ * (.18f + i * .06f) + i * 2.3f);
                    const float dx = fx - bx, dy = fy - by;
                    field += 2.8f / (1.5f + dx * dx + dy * dy);
                }
                color = hsv(.94f + .1f * clamp(field), .95f, clamp((field - .2f) * .8f));
                break;
            }
            case LedGalleryEffect::Kaleidoscope: {
                const float dx = fx - 3.5f, dy = fy - 3.5f;
                const float radius = std::hypot(dx, dy);
                constexpr float sector = pi / 3.f; // Six mirrored sectors.
                const float folded =
                    std::fabs(std::remainder(std::atan2(dy, dx) + kaleidoOrientation, sector));
                const float petals = std::cos(folded * 6.f);
                const float ring = wave(radius * kaleidoRadialFrequency - time_ * .65f +
                                        petals * 1.4f + seedPhase);
                const float filigree = std::cos(radius * 2.2f + folded * 10.f + time_ * .37f +
                                                .45f * wave(radius * 2.f - time_ * .23f));
                const float hue = time_ * .041f + .24f * ring + .18f * filigree + radius * .06f +
                                  seedPhase * .12f;
                color = hsv(hue, .92f, clamp(.52f + .27f * ring + .18f * filigree, .12f));
                break;
            }
            case LedGalleryEffect::Aurora: {
                float v = 0;
                for (int i = 0; i < 3; ++i) {
                    const float center =
                        1.2f + i * 2.f +
                        wave(fx * .6f + time_ * (.3f + i * .11f) + i + seedPhase) * 1.1f;
                    const float distance = fy - center;
                    v += std::exp(-distance * distance * .65f) * (.28f + i * .12f);
                }
                color = hsv(.39f + .08f * fy + .03f * wave(time_ * .2f), .8f, clamp(v));
                break;
            }
            default:
                break;
            }
            out.pixels[y * 8 + x] = color;
        }
    switch (effect_) {
    case LedGalleryEffect::Warp:
    case LedGalleryEffect::Comets:
    case LedGalleryEffect::Fireflies:
    case LedGalleryEffect::Vortex:
    case LedGalleryEffect::ParticleStorm:
        for (const auto& p : particles_) {
            if (p.life <= 0 || p.age >= p.life)
                continue;
            float x = p.x, y = p.y;
            if (effect_ == LedGalleryEffect::Vortex) {
                x = 3.5f + .35f * wave(time_ * .3f) + p.x * std::cos(p.y);
                y = 3.5f + .35f * wave(time_ * .27f) + p.x * std::sin(p.y);
            }
            float power = 1;
            if (effect_ == LedGalleryEffect::Fireflies)
                power = .1f + .7f * std::pow(.5f + .5f * wave(time_ * 1.7f + p.phase), 2);
            if (effect_ == LedGalleryEffect::ParticleStorm)
                power = clamp(1 - p.age / p.life);
            const float hue = effect_ == LedGalleryEffect::Warp ? .55f + p.hue * .25f : p.hue;
            auto color = hsv(hue, effect_ == LedGalleryEffect::Warp ? .3f : .85f, 1);
            detail::glow(out, x, y, color, power);
            if (effect_ == LedGalleryEffect::Comets || effect_ == LedGalleryEffect::Warp)
                for (int tail = 1; tail <= 3; ++tail)
                    detail::glow(out, x - p.vx * tail * .18f, y - p.vy * tail * .18f, color,
                                 power / (tail * 3));
        }
        break;
    case LedGalleryEffect::Ripple:
        for (const auto& r : ripples_)
            if (r.active) {
                const float radius = r.age * 3.f;
                const float energy = clamp(1 - r.age / 2.7f);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) {
                        const float distance = std::hypot(x - r.x, y - r.y);
                        const float band = std::exp(-std::pow((distance - radius) * 1.7f, 2));
                        blend(out, x, y, hsv(r.hue, .8f, 1), band * energy);
                    }
            }
        break;
    default:
        break;
    }
    return out;
}
} // namespace cardputer_hub::apps
