#pragma once

#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/indicator/indicator_service.h"
#include "services/microphone/microphone_service.h"

namespace cardputer_hub::apps {

class SoundReactiveApp final : public IMiniApp {
  public:
    SoundReactiveApp(services::MicrophoneService& microphone, services::IndicatorService& indicator,
                     core::IDisplayAdapter& display)
        : microphone_(microphone), indicator_(indicator), display_(display) {}

    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

    [[nodiscard]] float phase() const noexcept { return phase_; }
    [[nodiscard]] float colorLevel() const noexcept { return colorLevel_; }

  private:
    services::MicrophoneService& microphone_;
    services::IndicatorService& indicator_;
    core::IDisplayAdapter& display_;
    services::IndicatorClaim claim_;
    float phase_ = 0;
    float colorLevel_ = 0;
    std::chrono::milliseconds lcdElapsed_{0};
    std::chrono::milliseconds ledElapsed_{0};
    bool firstFrame_ = true;
    bool failureDisplayed_ = false;
    bool diagnosticsVisible_ = false;
};

} // namespace cardputer_hub::apps
