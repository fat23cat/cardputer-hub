#pragma once
#include "apps/shell/home_graphics.h"
#include <array>
#include <cstdint>

namespace cardputer_hub::apps {
enum class HomeAmbientTone : std::uint8_t { Far, Middle, Near };
struct HomeAmbientParticle {
    core::PixelPosition position{};
    std::uint8_t size = 1;
    HomeAmbientTone tone = HomeAmbientTone::Far;
};
using HomeAmbientFrame = std::array<HomeAmbientParticle, 40>;

HomeAmbientFrame homeAmbientFrame(std::uint64_t phaseMilliseconds);
void drawHomeAmbient(core::IDisplayAdapter& display, std::uint64_t phaseMilliseconds);
} // namespace cardputer_hub::apps
