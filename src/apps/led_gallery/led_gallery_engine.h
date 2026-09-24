#pragma once

#include "services/indicator/indicator_service.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cardputer_hub::apps {

enum class LedGalleryEffect : std::uint8_t {
    Plasma,
    Lava,
    Kaleidoscope,
    Aurora,
    Warp,
    Comets,
    Fireflies,
    Vortex,
    Ripple,
    ParticleStorm,
    Count
};

struct LedGalleryEffectInfo {
    LedGalleryEffect id;
    const char* name;
};

inline constexpr std::array<LedGalleryEffectInfo, 10> ledGalleryEffects{{
    {LedGalleryEffect::Plasma, "PLASMA"},
    {LedGalleryEffect::Lava, "LAVA"},
    {LedGalleryEffect::Kaleidoscope, "KALEIDOSCOPE"},
    {LedGalleryEffect::Aurora, "AURORA"},
    {LedGalleryEffect::Warp, "WARP"},
    {LedGalleryEffect::Comets, "COMETS"},
    {LedGalleryEffect::Fireflies, "FIREFLIES"},
    {LedGalleryEffect::Vortex, "VORTEX"},
    {LedGalleryEffect::Ripple, "RIPPLE"},
    {LedGalleryEffect::ParticleStorm, "PARTICLE STORM"},
}};

namespace detail {
void glow(services::IndicatorFrame& frame, float x, float y, core::RgbColor color, float power);
} // namespace detail

class LedGalleryEngine {
  public:
    explicit LedGalleryEngine(std::uint32_t seed = 1);
    void reset(LedGalleryEffect effect, std::uint32_t seed);
    void advance(std::chrono::milliseconds elapsed);
    void interact(char key);
    [[nodiscard]] services::IndicatorFrame frame() const;
    [[nodiscard]] LedGalleryEffect effect() const noexcept { return effect_; }
    [[nodiscard]] std::size_t activeRipples() const noexcept;
    [[nodiscard]] std::size_t activeParticles() const noexcept;

  private:
    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, age = 0, life = 0, hue = 0, phase = 0;
    };
    struct Ripple {
        float x = 0, y = 0, age = 0, hue = 0;
        bool active = false;
    };
    std::uint32_t random();
    float unitRandom();
    void initParticle(Particle& particle, std::size_t index);
    void burst(char key, bool large);
    void addRipple(bool manual);

    LedGalleryEffect effect_ = LedGalleryEffect::Plasma;
    std::uint32_t rng_ = 1;
    float time_ = 0;
    float ambientAccumulator_ = 0;
    std::uint32_t sequence_ = 0;
    std::array<Particle, 24> particles_{};
    std::array<Ripple, 4> ripples_{};
};

} // namespace cardputer_hub::apps
