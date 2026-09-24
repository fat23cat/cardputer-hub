#pragma once

#include "apps/led_gallery/led_gallery_engine.h"
#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"

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
    void draw();
    services::IndicatorService& indicator_;
    core::IDisplayAdapter& display_;
    services::IndicatorClaim claim_;
    LedGalleryEngine engine_;
    LedGalleryEffect effect_ = LedGalleryEffect::Plasma;
    const std::uint32_t seed_;
    std::chrono::milliseconds outputElapsed_{0};
    bool redraw_ = true;
};

} // namespace cardputer_hub::apps
