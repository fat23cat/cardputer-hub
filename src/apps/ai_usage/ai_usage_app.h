#pragma once

#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/ai_usage/ai_usage_indicator_controller.h"

#include <string>

namespace cardputer_hub::apps {

class AiUsageApp final : public IMiniApp {
  public:
    AiUsageApp(services::AiUsageService& usage, services::AiUsageIndicatorController& indicator,
               core::IDisplayAdapter& display)
        : usage_(usage), indicator_(indicator), display_(display) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

  private:
    enum class View : std::uint8_t { Main, Limits, Resets };
    void draw();
    void drawMetric(const connectivity::AiUsageMetric& metric, int top, bool compact);
    void drawRows(const connectivity::AiUsageProvider& provider, int top, std::uint8_t first);
    const connectivity::AiUsageProvider* detailProvider() const;
    void drawExpanded(const connectivity::AiUsageProvider& provider);
    services::AiUsageService& usage_;
    services::AiUsageIndicatorController& indicator_;
    core::IDisplayAdapter& display_;
    std::uint32_t renderedRevision_ = 0;
    std::uint16_t renderedSession_ = 0;
    std::uint8_t selection_ = 0;
    bool rendered_ = false;
    bool selected_ = false;
    std::chrono::milliseconds selectionRemaining_{0};
    std::chrono::milliseconds countdownElapsed_{0};
    View view_ = View::Main;
    connectivity::AiProvider detail_ = connectivity::AiProvider::Codex;
    std::uint8_t resetScroll_ = 0;
};

} // namespace cardputer_hub::apps
