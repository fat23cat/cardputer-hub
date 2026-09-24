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
    GameOfLife,
    ReactionDiffusion,
    Fire,
    GravityWell,
    Swarm,
    FallingSand,
    LangtonsAnt,
    TetrisDream,
    RuleMachine,
    ElectricStorm,
    Count
};

struct LedGalleryEffectInfo {
    LedGalleryEffect id;
    const char* name;
    const char* actions;
};

inline constexpr std::array<LedGalleryEffectInfo, 20> ledGalleryEffects{{
    {LedGalleryEffect::Plasma, "PLASMA", nullptr},
    {LedGalleryEffect::Lava, "LAVA", nullptr},
    {LedGalleryEffect::Kaleidoscope, "KALEIDOSCOPE", nullptr},
    {LedGalleryEffect::Aurora, "AURORA", nullptr},
    {LedGalleryEffect::Warp, "WARP", nullptr},
    {LedGalleryEffect::Comets, "COMETS", nullptr},
    {LedGalleryEffect::Fireflies, "FIREFLIES", nullptr},
    {LedGalleryEffect::Vortex, "VORTEX", nullptr},
    {LedGalleryEffect::Ripple, "RIPPLE", "SPACE RIPPLE"},
    {LedGalleryEffect::ParticleStorm, "PARTICLE STORM", "KEY BURST  SPACE BURST"},
    {LedGalleryEffect::GameOfLife, "GAME OF LIFE", "SPACE ADD  G NEW WORLD"},
    {LedGalleryEffect::ReactionDiffusion, "REACTION DIFFUSION", "A/D KILL  W/S FEED  SPACE ADD"},
    {LedGalleryEffect::Fire, "FIRE", "A/D WIND  W/S HEAT  SPACE FLASH"},
    {LedGalleryEffect::GravityWell, "GRAVITY WELL", "WASD MOVE  SPACE REPULSE"},
    {LedGalleryEffect::Swarm, "SWARM", "WASD MOVE  SPACE SCATTER"},
    {LedGalleryEffect::FallingSand, "FALLING SAND", "A/D ROTATE  SPACE ADD"},
    {LedGalleryEffect::LangtonsAnt, "LANGTON'S ANT", "SPACE ADD ANT"},
    {LedGalleryEffect::TetrisDream, "TETRIS DREAM", "A/D MOVE  W ROT  S SOFT  SPACE HARD"},
    {LedGalleryEffect::RuleMachine, "RULE MACHINE", "A/D RULE  W/S SPEED  SPACE NEW"},
    {LedGalleryEffect::ElectricStorm, "ELECTRIC STORM", "WASD MOVE  SPACE STRIKE"},
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
    [[nodiscard]] const char* status() const noexcept { return status_; }

  private:
    friend struct LedGalleryEngineTestAccess;
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
    void stepSimulation();
    void seedLife();
    void injectReagent(int x, int y);
    void spawnPiece();
    bool pieceFits(int x, int y, int rotation) const;
    void placePiece();

    LedGalleryEffect effect_ = LedGalleryEffect::Plasma;
    std::uint32_t rng_ = 1;
    float time_ = 0;
    float ambientAccumulator_ = 0;
    std::uint32_t sequence_ = 0;
    std::array<Particle, 24> particles_{};
    std::array<Ripple, 4> ripples_{};
    std::array<std::uint8_t, 64> cells_{};
    std::array<std::uint8_t, 64> nextCells_{};
    std::array<float, 64> fieldA_{};
    std::array<float, 64> fieldB_{};
    std::array<float, 64> nextA_{};
    std::array<float, 64> nextB_{};
    std::array<std::uint8_t, 64> glow_{};
    float stepElapsed_ = 0;
    float feed_ = .037f, kill_ = .061f;
    int wind_ = 0, heat_ = 6, gravity_ = 0, ruleIndex_ = 0, ruleSpeed_ = 5;
    std::uint8_t sandFadeSteps_ = 0;
    int targetX_ = 3, targetY_ = 3, pulse_ = 0;
    float pulseRemainingSeconds_ = 0;
    int pieceX_ = 2, pieceY_ = 0, pieceType_ = 0, pieceRotation_ = 0;
    int antCount_ = 0;
    struct Ant {
        std::uint8_t x = 0, y = 0, direction = 0;
    };
    std::array<Ant, 4> ants_{};
    std::uint8_t ruleRow_ = 1U << 3;
    std::uint16_t generation_ = 0, still_ = 0;
    char status_[32]{};
};

} // namespace cardputer_hub::apps
