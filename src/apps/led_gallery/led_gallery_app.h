#pragma once

#include "apps/led_gallery/led_gallery_engine.h"
#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"

#include <memory>

namespace cardputer_hub::apps {

inline constexpr char ledGalleryAppId[] = "led-gallery";

class LedGalleryApp final : public IMiniApp {
  public:
    LedGalleryApp(services::IndicatorService& indicator, core::IDisplayAdapter& display,
                  std::uint32_t seed = 1);
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;
    [[nodiscard]] LedGalleryEffect currentEffect() const noexcept { return effect_; }

  private:
    void select(LedGalleryEffect effect);
    // Moves to the previous (-1) or next (+1) effect, wrapping at the ends.
    void step(int delta);
    void draw();
    services::IndicatorService& indicator_;
    core::IDisplayAdapter& display_;
    services::IndicatorClaim claim_;
    // Created on activation and released on deactivation; only the selected
    // effect survives between openings.
    std::unique_ptr<LedGalleryEngine> engine_;
    LedGalleryEffect effect_ = LedGalleryEffect::Plasma;
    const std::uint32_t seed_;
    std::chrono::milliseconds outputElapsed_{0};
    std::chrono::milliseconds feedbackElapsed_{0};
    char feedback_[32]{};
    bool redraw_ = true;
};

} // namespace cardputer_hub::apps
