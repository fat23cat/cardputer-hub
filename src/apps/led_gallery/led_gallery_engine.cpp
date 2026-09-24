#include "apps/led_gallery/led_gallery_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
constexpr float pi = 3.14159265f;
constexpr std::uint8_t sandFadeFrames = 10;
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
    cells_.fill(0);
    nextCells_.fill(0);
    fieldA_.fill(1);
    fieldB_.fill(0);
    nextA_.fill(1);
    nextB_.fill(0);
    glow_.fill(0);
    stepElapsed_ = 0;
    feed_ = .037f;
    kill_ = .061f;
    wind_ = 0;
    heat_ = 6;
    gravity_ = 0;
    sandFadeSteps_ = 0;
    ruleIndex_ = 0;
    ruleSpeed_ = 5;
    targetX_ = targetY_ = 3;
    pulse_ = 0;
    pulseRemainingSeconds_ = 0;
    antCount_ = 0;
    ruleRow_ = 1U << 3;
    generation_ = still_ = 0;
    status_[0] = 0;
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
    if (effect_ == LedGalleryEffect::GameOfLife)
        seedLife();
    if (effect_ == LedGalleryEffect::ReactionDiffusion)
        injectReagent(3, 3);
    if (effect_ == LedGalleryEffect::GravityWell || effect_ == LedGalleryEffect::Swarm ||
        effect_ == LedGalleryEffect::ElectricStorm) {
        const int n = effect_ == LedGalleryEffect::ElectricStorm ? 3
                      : effect_ == LedGalleryEffect::Swarm       ? 14
                                                                 : 20;
        for (int i = 0; i < n; ++i) {
            auto& p = particles_[i];
            p.x = unitRandom() * 7;
            p.y = unitRandom() * 7;
            p.vx = (unitRandom() - .5f) * 2;
            p.vy = (unitRandom() - .5f) * 2;
            p.hue = unitRandom();
            p.phase = unitRandom() * 2 * pi;
            p.life = 1; // Permanent entity; excluded from legacy lifetime processing.
        }
    }
    if (effect_ == LedGalleryEffect::LangtonsAnt) {
        ants_[0] = {3, 3, 0};
        antCount_ = 1;
    }
    if (effect_ == LedGalleryEffect::TetrisDream)
        spawnPiece();
    if (effect_ == LedGalleryEffect::RuleMachine)
        cells_[3] = 1;
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
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    if (effect_ == LedGalleryEffect::Ripple && key == ' ')
        addRipple(true);
    else if (effect_ == LedGalleryEffect::ParticleStorm) {
        if (key == ' ')
            burst(key, true);
        else if (key >= 33 && key <= 126)
            burst(key, false);
    } else if (effect_ == LedGalleryEffect::GameOfLife) {
        if (key == 'g')
            seedLife();
        if (key == ' ') {
            const int center = static_cast<int>(random() % 64U);
            const int x = center % 8, y = center / 8;
            cells_[center] = 1;
            cells_[y * 8 + (x + 7) % 8] = 1;
            cells_[y * 8 + (x + 1) % 8] = 1;
            cells_[((y + 7) % 8) * 8 + x] = 1;
            cells_[((y + 1) % 8) * 8 + x] = 1;
        }
    } else if (effect_ == LedGalleryEffect::ReactionDiffusion) {
        if (key == 'w')
            feed_ = clamp(feed_ + .002f, .018f, .065f);
        if (key == 's')
            feed_ = clamp(feed_ - .002f, .018f, .065f);
        if (key == 'a')
            kill_ = clamp(kill_ - .002f, .04f, .075f);
        if (key == 'd')
            kill_ = clamp(kill_ + .002f, .04f, .075f);
        if (key == ' ') {
            injectReagent(1 + static_cast<int>(random() % 6U), 1 + static_cast<int>(random() % 6U));
            std::snprintf(status_, sizeof(status_), "REAGENT ADDED");
        } else
            std::snprintf(status_, sizeof(status_), "%s %.3f",
                          key == 'a' || key == 'd' ? "KILL" : "FEED",
                          key == 'a' || key == 'd' ? kill_ : feed_);
    } else if (effect_ == LedGalleryEffect::Fire) {
        if (key == 'a')
            wind_ = std::max(-3, wind_ - 1);
        if (key == 'd')
            wind_ = std::min(3, wind_ + 1);
        if (key == 'w')
            heat_ = std::min(9, heat_ + 1);
        if (key == 's')
            heat_ = std::max(1, heat_ - 1);
        if (key == ' ') {
            for (int x = 1; x < 7; ++x)
                cells_[7 * 8 + x] = 255;
            std::snprintf(status_, sizeof(status_), "FLASH");
        } else
            std::snprintf(status_, sizeof(status_),
                          key == 'a' || key == 'd' ? "WIND %d" : "HEAT %d",
                          key == 'a' || key == 'd' ? wind_ : heat_);
    } else if (effect_ == LedGalleryEffect::GravityWell || effect_ == LedGalleryEffect::Swarm ||
               effect_ == LedGalleryEffect::ElectricStorm) {
        if (key == 'a')
            targetX_ = std::max(0, targetX_ - 1);
        if (key == 'd')
            targetX_ = std::min(7, targetX_ + 1);
        if (key == 'w')
            targetY_ = std::max(0, targetY_ - 1);
        if (key == 's')
            targetY_ = std::min(7, targetY_ + 1);
        if (key == ' ') {
            if (effect_ == LedGalleryEffect::ElectricStorm)
                pulse_ = 4;
            else
                pulseRemainingSeconds_ = .65f;
        }
    } else if (effect_ == LedGalleryEffect::FallingSand) {
        if (key == 'a')
            gravity_ = (gravity_ + 3) % 4;
        if (key == 'd')
            gravity_ = (gravity_ + 1) % 4;
        if (key == ' ') {
            if (sandFadeSteps_) {
                sandFadeSteps_ = 0;
                cells_.fill(0);
                generation_ = 0;
            }
            for (int i = 0; i < 3; ++i)
                cells_[gravity_ == 0   ? random() % 8U
                       : gravity_ == 2 ? 56 + random() % 8U
                       : gravity_ == 1 ? (random() % 8U) * 8U + 7U
                                       : (random() % 8U) * 8U] = 1;
            std::snprintf(status_, sizeof(status_), "SAND ADDED");
        } else {
            constexpr const char* directions[] = {"DOWN", "LEFT", "UP", "RIGHT"};
            std::snprintf(status_, sizeof(status_), "GRAVITY %s", directions[gravity_]);
        }
    } else if (effect_ == LedGalleryEffect::LangtonsAnt) {
        if (key == ' ' && antCount_ < 4)
            ants_[antCount_++] = {static_cast<std::uint8_t>(random() % 8U),
                                  static_cast<std::uint8_t>(random() % 8U), 0};
    } else if (effect_ == LedGalleryEffect::TetrisDream) {
        if (key == 'a' && pieceFits(pieceX_ - 1, pieceY_, pieceRotation_))
            --pieceX_;
        if (key == 'd' && pieceFits(pieceX_ + 1, pieceY_, pieceRotation_))
            ++pieceX_;
        if (key == 'w' && pieceFits(pieceX_, pieceY_, (pieceRotation_ + 1) % 4))
            pieceRotation_ = (pieceRotation_ + 1) % 4;
        if (key == 's' || key == ' ') {
            if (key == ' ')
                while (pieceFits(pieceX_, pieceY_ + 1, pieceRotation_))
                    ++pieceY_;
            if (pieceFits(pieceX_, pieceY_ + 1, pieceRotation_))
                ++pieceY_;
            else
                placePiece();
        }
    } else if (effect_ == LedGalleryEffect::RuleMachine) {
        if (key == 'a')
            ruleIndex_ = (ruleIndex_ + 4) % 5;
        if (key == 'd')
            ruleIndex_ = (ruleIndex_ + 1) % 5;
        if (key == 'w')
            ruleSpeed_ = std::min(10, ruleSpeed_ + 1);
        if (key == 's')
            ruleSpeed_ = std::max(2, ruleSpeed_ - 1);
        if (key == ' ') {
            cells_.fill(0);
            ruleRow_ = static_cast<std::uint8_t>(1U << (random() % 8U));
            for (int x = 0; x < 8; ++x)
                cells_[x] = (ruleRow_ >> x) & 1U;
        }
        constexpr int rules[] = {30, 54, 90, 110, 150};
        std::snprintf(status_, sizeof(status_), "RULE %d", rules[ruleIndex_]);
    }
}
void LedGalleryEngine::seedLife() {
    generation_ = still_ = 0;
    for (auto& cell : cells_)
        cell = unitRandom() < .34f ? 1 : 0;
}
void LedGalleryEngine::injectReagent(int x, int y) {
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int i = (y + dy) * 8 + x + dx;
            fieldA_[i] = std::min(fieldA_[i], .45f);
            fieldB_[i] = std::max(fieldB_[i], .8f);
        }
}
void LedGalleryEngine::spawnPiece() {
    pieceType_ = static_cast<int>(random() % 5U);
    pieceRotation_ = 0;
    pieceX_ = 2 + static_cast<int>(random() % 3U);
    pieceY_ = 0;
    if (!pieceFits(pieceX_, pieceY_, pieceRotation_)) {
        cells_.fill(0);
        glow_.fill(110);
    }
}
bool LedGalleryEngine::pieceFits(int x, int y, int rotation) const {
    constexpr std::uint8_t shapes[5][4] = {{0x4, 0x5, 0x6, 0x7},
                                           {0x1, 0x5, 0x9, 0xd},
                                           {0x4, 0x5, 0x8, 0x9},
                                           {0x1, 0x4, 0x5, 0x6},
                                           {0x0, 0x4, 0x5, 0x6}};
    for (auto point : shapes[pieceType_]) {
        int px = point % 4 - 1, py = point / 4 - 1;
        for (int i = 0; i < rotation; ++i) {
            const int old = px;
            px = -py;
            py = old;
        }
        const int xx = x + px, yy = y + py;
        if (xx < 0 || xx > 7 || yy > 7 || (yy >= 0 && cells_[yy * 8 + xx]))
            return false;
    }
    return true;
}
void LedGalleryEngine::placePiece() {
    constexpr std::uint8_t shapes[5][4] = {{0x4, 0x5, 0x6, 0x7},
                                           {0x1, 0x5, 0x9, 0xd},
                                           {0x4, 0x5, 0x8, 0x9},
                                           {0x1, 0x4, 0x5, 0x6},
                                           {0x0, 0x4, 0x5, 0x6}};
    for (auto point : shapes[pieceType_]) {
        int x = point % 4 - 1, y = point / 4 - 1;
        for (int i = 0; i < pieceRotation_; ++i) {
            const int old = x;
            x = -y;
            y = old;
        }
        x += pieceX_;
        y += pieceY_;
        if (x >= 0 && x < 8 && y >= 0 && y < 8)
            cells_[y * 8 + x] = static_cast<std::uint8_t>(pieceType_ + 1);
    }
    for (int y = 7; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < 8; ++x)
            full &= cells_[y * 8 + x] != 0;
        if (full) {
            for (int row = y; row > 0; --row)
                for (int x = 0; x < 8; ++x)
                    cells_[row * 8 + x] = cells_[(row - 1) * 8 + x];
            for (int x = 0; x < 8; ++x)
                cells_[x] = 0;
            glow_.fill(70);
            ++y;
        }
    }
    spawnPiece();
}
void LedGalleryEngine::stepSimulation() {
    ++generation_;
    switch (effect_) {
    case LedGalleryEffect::GameOfLife: {
        int alive = 0, changes = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                int neighbors = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy)
                            neighbors += cells_[((y + dy + 8) % 8) * 8 + (x + dx + 8) % 8] != 0;
                const int i = y * 8 + x;
                const bool born = neighbors == 3 || (cells_[i] && neighbors == 2);
                nextCells_[i] = born ? static_cast<std::uint8_t>(std::min(240, cells_[i] + 1)) : 0;
                alive += born;
                changes += born != (cells_[i] != 0);
            }
        cells_ = nextCells_;
        still_ = changes ? 0 : still_ + 1;
        if (!alive || still_ >= 7 || generation_ > 180)
            seedLife();
        break;
    }
    case LedGalleryEffect::ReactionDiffusion:
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const int i = y * 8 + x;
                const int l = y * 8 + (x + 7) % 8, r = y * 8 + (x + 1) % 8;
                const int u = ((y + 7) % 8) * 8 + x, d = ((y + 1) % 8) * 8 + x;
                const float a = fieldA_[i], b = fieldB_[i], reaction = a * b * b;
                nextA_[i] =
                    clamp(a + .16f * (fieldA_[l] + fieldA_[r] + fieldA_[u] + fieldA_[d] - 4 * a) -
                          reaction + feed_ * (1 - a));
                nextB_[i] =
                    clamp(b + .06f * (fieldB_[l] + fieldB_[r] + fieldB_[u] + fieldB_[d] - 4 * b) +
                          reaction - (kill_ + feed_) * b);
            }
        fieldA_ = nextA_;
        fieldB_ = nextB_;
        if (generation_ % 64 == 0)
            injectReagent(1 + static_cast<int>(random() % 6U), 1 + static_cast<int>(random() % 6U));
        break;
    case LedGalleryEffect::Fire:
        for (int x = 0; x < 8; ++x)
            nextCells_[56 + x] = static_cast<std::uint8_t>(random() % (heat_ * 20U + 1U));
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 8; ++x) {
                const int source = std::max(0, std::min(7, x - (wind_ > 0   ? 1
                                                                : wind_ < 0 ? -1
                                                                            : 0)));
                const int calm =
                    (cells_[(y + 1) * 8 + x] * 2 + cells_[(y + 1) * 8 + std::max(0, x - 1)] +
                     cells_[(y + 1) * 8 + std::min(7, x + 1)]) /
                    4;
                const int gust = (cells_[(y + 1) * 8 + source] * 2 +
                                  cells_[(y + 1) * 8 + std::max(0, source - 1)] +
                                  cells_[(y + 1) * 8 + std::min(7, source + 1)]) /
                                 4;
                const int strength = std::abs(wind_);
                const int v = (calm * (3 - strength) + gust * strength) / 3;
                nextCells_[y * 8 + x] =
                    static_cast<std::uint8_t>(std::max(0, v - static_cast<int>(random() % 19U)));
            }
        cells_ = nextCells_;
        break;
    case LedGalleryEffect::FallingSand: {
        if (sandFadeSteps_) {
            if (--sandFadeSteps_ == 0) {
                cells_.fill(0);
                generation_ = 0;
            }
            break;
        }
        const auto edge = [this](int lane, bool downstream) {
            const int direction = downstream ? gravity_ : (gravity_ + 2) % 4;
            return direction == 0   ? 56 + lane
                   : direction == 2 ? lane
                   : direction == 1 ? lane * 8
                                    : lane * 8 + 7;
        };
        nextCells_ = cells_;
        const int dx = gravity_ == 1 ? -1 : gravity_ == 3 ? 1 : 0;
        const int dy = gravity_ == 0 ? 1 : gravity_ == 2 ? -1 : 0;
        for (int depth = 0; depth < 8; ++depth)
            for (int lane = 0; lane < 8; ++lane) {
                const int transverse = generation_ & 1U ? 7 - lane : lane;
                const int x = gravity_ == 1 ? depth : gravity_ == 3 ? 7 - depth : transverse;
                const int y = gravity_ == 0 ? 7 - depth : gravity_ == 2 ? depth : transverse;
                const int i = y * 8 + x;
                if (!cells_[i] || !nextCells_[i])
                    continue;
                const int side = (random() & 1U) ? 1 : -1;
                for (int j = 0; j < 3; ++j) {
                    const int xx = x + dx + (dy ? (j == 0 ? 0 : side * (j == 1 ? 1 : -1)) : 0);
                    const int yy = y + dy + (dx ? (j == 0 ? 0 : side * (j == 1 ? 1 : -1)) : 0);
                    if (xx >= 0 && xx < 8 && yy >= 0 && yy < 8 && !nextCells_[yy * 8 + xx]) {
                        nextCells_[yy * 8 + xx] = 1;
                        nextCells_[i] = 0;
                        break;
                    }
                }
            }
        cells_ = nextCells_;
        if (generation_ == 1 || generation_ % 8 == 0) {
            const int lane = static_cast<int>(random() % 8U);
            for (int offset = 0; offset < 8; ++offset) {
                const int i = edge((lane + offset) % 8, false);
                if (!cells_[i]) {
                    cells_[i] = 1;
                    break;
                }
            }
        }
        if (std::all_of(cells_.begin(), cells_.end(), [](std::uint8_t cell) { return cell != 0; }))
            sandFadeSteps_ = sandFadeFrames;
        break;
    }
    case LedGalleryEffect::LangtonsAnt:
        for (int n = 0; n < antCount_; ++n) {
            auto& ant = ants_[n];
            const int i = ant.y * 8 + ant.x;
            ant.direction = static_cast<std::uint8_t>((ant.direction + (cells_[i] ? 3 : 1)) % 4);
            cells_[i] ^= 1;
            ant.x = static_cast<std::uint8_t>(
                (ant.x + (ant.direction == 1) - (ant.direction == 3) + 8) % 8);
            ant.y = static_cast<std::uint8_t>(
                (ant.y + (ant.direction == 2) - (ant.direction == 0) + 8) % 8);
        }
        break;
    case LedGalleryEffect::Swarm:
        if (generation_ % 8 == 0) {
            targetX_ = std::max(1, std::min(6, targetX_ + static_cast<int>(random() % 3U) - 1));
            targetY_ = std::max(1, std::min(6, targetY_ + static_cast<int>(random() % 3U) - 1));
        }
        break;
    case LedGalleryEffect::TetrisDream:
        for (auto& value : glow_)
            value = static_cast<std::uint8_t>(value * .6f);
        if (generation_ % 3 == 0) {
            const int move = static_cast<int>(random() % 3U) - 1;
            if (pieceFits(pieceX_ + move, pieceY_, pieceRotation_))
                pieceX_ += move;
        }
        if (generation_ % 4 == 0 && random() % 3U == 0 &&
            pieceFits(pieceX_, pieceY_, (pieceRotation_ + 1) % 4))
            pieceRotation_ = (pieceRotation_ + 1) % 4;
        if (pieceFits(pieceX_, pieceY_ + 1, pieceRotation_))
            ++pieceY_;
        else
            placePiece();
        break;
    case LedGalleryEffect::RuleMachine: {
        constexpr std::uint8_t rules[] = {30, 54, 90, 110, 150};
        std::uint8_t next = 0;
        for (int x = 0; x < 8; ++x) {
            const int pattern = ((ruleRow_ >> ((x + 7) % 8) & 1U) << 2) |
                                ((ruleRow_ >> x & 1U) << 1) | (ruleRow_ >> ((x + 1) % 8) & 1U);
            next |= ((rules[ruleIndex_] >> pattern) & 1U) << x;
        }
        for (int i = 63; i >= 8; --i)
            cells_[i] = cells_[i - 8];
        if (!next)
            next = static_cast<std::uint8_t>(1U << (random() % 8U));
        ruleRow_ = next;
        for (int x = 0; x < 8; ++x)
            cells_[x] = (next >> x) & 1U;
        break;
    }
    case LedGalleryEffect::ElectricStorm:
        for (auto& value : glow_)
            value = static_cast<std::uint8_t>(value * .65f);
        for (int c = 1; c < 3; ++c) {
            int x = static_cast<int>(particles_[0].x), y = static_cast<int>(particles_[0].y);
            const int tx = static_cast<int>(particles_[c].x),
                      ty = static_cast<int>(particles_[c].y);
            for (int i = 0; i < 10 && (x != tx || y != ty); ++i) {
                glow_[y * 8 + x] = pulse_ ? 210 : 130;
                if (random() & 1U)
                    x += (tx > x) - (tx < x);
                else
                    y += (ty > y) - (ty < y);
                x = std::max(0, std::min(7, x));
                y = std::max(0, std::min(7, y));
                if (random() % 4U == 0)
                    glow_[y * 8 + std::max(0, x - 1)] = 80;
            }
        }
        if (pulse_)
            --pulse_;
        break;
    default:
        break;
    }
}
void LedGalleryEngine::advance(std::chrono::milliseconds elapsed) {
    if (elapsed.count() <= 0)
        return;
    const float rawDt = elapsed.count() / 1000.f;
    const float dt = std::min(2.f, rawDt);
    time_ += rawDt;
    float interval = .15f;
    if (effect_ == LedGalleryEffect::TetrisDream)
        interval = .35f;
    if (effect_ == LedGalleryEffect::RuleMachine)
        interval = 1.f / ruleSpeed_;
    if (effect_ == LedGalleryEffect::FallingSand || effect_ == LedGalleryEffect::Fire ||
        effect_ == LedGalleryEffect::ReactionDiffusion ||
        effect_ == LedGalleryEffect::ElectricStorm)
        interval = .08f;
    if (effect_ >= LedGalleryEffect::GameOfLife && effect_ <= LedGalleryEffect::ElectricStorm) {
        stepElapsed_ += dt;
        const int steps = std::min(8, static_cast<int>(stepElapsed_ / interval));
        for (int i = 0; i < steps; ++i)
            stepSimulation();
        stepElapsed_ = std::fmod(stepElapsed_, interval);
    }
    if (effect_ == LedGalleryEffect::GravityWell || effect_ == LedGalleryEffect::Swarm ||
        effect_ == LedGalleryEffect::ElectricStorm) {
        const int n = effect_ == LedGalleryEffect::ElectricStorm ? 3
                      : effect_ == LedGalleryEffect::Swarm       ? 14
                                                                 : 20;
        const auto previous = particles_;
        const float repulsionFraction = dt > 0 ? std::min(dt, pulseRemainingSeconds_) / dt : 0;
        for (int i = 0; i < n; ++i) {
            auto& p = particles_[i];
            if (effect_ == LedGalleryEffect::ElectricStorm) {
                if (i == 0) {
                    p.x = static_cast<float>(targetX_);
                    p.y = static_cast<float>(targetY_);
                } else {
                    p.x = clamp(p.x + p.vx * dt * .3f, 0, 7);
                    p.y = clamp(p.y + p.vy * dt * .3f, 0, 7);
                    if (p.x == 0 || p.x == 7)
                        p.vx = -p.vx;
                    if (p.y == 0 || p.y == 7)
                        p.vy = -p.vy;
                }
                continue;
            }
            const float dx = targetX_ - previous[i].x, dy = targetY_ - previous[i].y;
            const float divisor =
                effect_ == LedGalleryEffect::GravityWell ? 1.f + dx * dx + dy * dy : 5.f;
            const float direction = 1.f - 2.8f * repulsionFraction;
            const float damping = std::exp(-dt * (effect_ == LedGalleryEffect::Swarm ? 3.f : .2f));
            p.vx = clamp(previous[i].vx * damping + direction * dx / divisor * dt * 5.f, -3.f, 3.f);
            p.vy = clamp(previous[i].vy * damping + direction * dy / divisor * dt * 5.f, -3.f, 3.f);
            if (effect_ == LedGalleryEffect::GravityWell) {
                p.vx += -dy / divisor * dt * .9f;
                p.vy += dx / divisor * dt * .9f;
                if (dx * dx + dy * dy < .25f) {
                    p.vx += std::cos(p.phase) * dt * 4.f;
                    p.vy += std::sin(p.phase) * dt * 4.f;
                }
            }
            if (effect_ == LedGalleryEffect::Swarm) {
                float averageX = 0, averageY = 0, averageVx = 0, averageVy = 0;
                int neighbors = 0;
                for (int j = 0; j < n; ++j)
                    if (i != j) {
                        const float sx = previous[i].x - previous[j].x;
                        const float sy = previous[i].y - previous[j].y;
                        if (sx * sx + sy * sy < 1.f) {
                            p.vx += sx * dt * .3f;
                            p.vy += sy * dt * .3f;
                        }
                        if (sx * sx + sy * sy < 9.f) {
                            averageX += previous[j].x;
                            averageY += previous[j].y;
                            averageVx += previous[j].vx;
                            averageVy += previous[j].vy;
                            ++neighbors;
                        }
                    }
                if (neighbors) {
                    const float flockWeight = 1.f - repulsionFraction;
                    p.vx += flockWeight * dt *
                            (.28f * (averageX / neighbors - previous[i].x) +
                             .38f * (averageVx / neighbors - previous[i].vx));
                    p.vy += flockWeight * dt *
                            (.28f * (averageY / neighbors - previous[i].y) +
                             .38f * (averageVy / neighbors - previous[i].vy));
                }
            }
            p.vx = clamp(p.vx, -3.f, 3.f);
            p.vy = clamp(p.vy, -3.f, 3.f);
            p.x = clamp(previous[i].x + p.vx * dt, 0, 7);
            p.y = clamp(previous[i].y + p.vy * dt, 0, 7);
        }
        pulseRemainingSeconds_ = std::max(0.f, pulseRemainingSeconds_ - dt);
    }
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
    if (effect_ <= LedGalleryEffect::ParticleStorm)
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
    if (effect_ >= LedGalleryEffect::GameOfLife && effect_ <= LedGalleryEffect::ElectricStorm) {
        const float sandBrightness =
            sandFadeSteps_ ? static_cast<float>(sandFadeSteps_) / sandFadeFrames : 1.f;
        for (int i = 0; i < 64; ++i) {
            core::RgbColor color{};
            switch (effect_) {
            case LedGalleryEffect::GameOfLife:
                if (cells_[i])
                    color = hsv(.48f - .45f * clamp(cells_[i] / 30.f), .9f, .8f);
                break;
            case LedGalleryEffect::ReactionDiffusion:
                color = hsv(.53f + .28f * fieldB_[i] - .12f * (1.f - fieldA_[i]), .9f,
                            .04f + .86f * clamp((fieldB_[i] - .06f) * 1.8f));
                break;
            case LedGalleryEffect::Fire: {
                const float v = cells_[i] / 255.f;
                color = hsv(.02f + v * .12f, 1.f - v * .3f, v);
                break;
            }
            case LedGalleryEffect::FallingSand:
                if (cells_[i])
                    color = {static_cast<std::uint8_t>(245.f * sandBrightness),
                             static_cast<std::uint8_t>(160.f * sandBrightness),
                             static_cast<std::uint8_t>(35.f * sandBrightness)};
                break;
            case LedGalleryEffect::LangtonsAnt:
                if (cells_[i])
                    color = {30, 100, 150};
                break;
            case LedGalleryEffect::TetrisDream:
                if (cells_[i])
                    color = hsv(cells_[i] * .16f, .85f, .75f);
                break;
            case LedGalleryEffect::RuleMachine:
                if (cells_[i])
                    color = hsv(.5f + (i / 8) * .055f + time_ * .02f, .8f, .7f);
                break;
            case LedGalleryEffect::ElectricStorm:
                if (glow_[i])
                    color = hsv(.58f, .55f, glow_[i] / 255.f);
                break;
            default:
                break;
            }
            out.pixels[i] = color;
        }
        if (effect_ == LedGalleryEffect::GravityWell || effect_ == LedGalleryEffect::Swarm ||
            effect_ == LedGalleryEffect::ElectricStorm) {
            const int n = effect_ == LedGalleryEffect::ElectricStorm ? 3
                          : effect_ == LedGalleryEffect::Swarm       ? 14
                                                                     : 20;
            for (int i = 0; i < n; ++i)
                detail::glow(
                    out, particles_[i].x, particles_[i].y,
                    hsv(effect_ == LedGalleryEffect::ElectricStorm ? .58f : particles_[i].hue, .8f,
                        1),
                    .8f);
            detail::glow(out, targetX_, targetY_, {255, 255, 255}, .8f);
        }
        if (effect_ == LedGalleryEffect::LangtonsAnt)
            for (int i = 0; i < antCount_; ++i)
                blend(out, ants_[i].x, ants_[i].y, hsv(i * .23f, .7f, 1));
        if (effect_ == LedGalleryEffect::TetrisDream) {
            constexpr std::uint8_t shapes[5][4] = {{0x4, 0x5, 0x6, 0x7},
                                                   {0x1, 0x5, 0x9, 0xd},
                                                   {0x4, 0x5, 0x8, 0x9},
                                                   {0x1, 0x4, 0x5, 0x6},
                                                   {0x0, 0x4, 0x5, 0x6}};
            for (auto point : shapes[pieceType_]) {
                int x = point % 4 - 1, y = point / 4 - 1;
                for (int j = 0; j < pieceRotation_; ++j) {
                    const int old = x;
                    x = -y;
                    y = old;
                }
                blend(out, pieceX_ + x, pieceY_ + y, hsv(pieceType_ * .16f, .8f, 1));
            }
            for (int i = 0; i < 64; ++i)
                if (glow_[i])
                    blend(out, i % 8, i / 8, {80, 80, 80}, glow_[i] / 255.f);
        }
        return out;
    }
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
