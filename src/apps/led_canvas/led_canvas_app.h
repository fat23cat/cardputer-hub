#pragma once
#include "apps/led_canvas/led_canvas_engine.h"
#include "apps/runtime/mini_app.h"
#include "services/indicator/indicator_service.h"
#include "services/led_canvas/led_canvas_service.h"
#include "services/web_access/web_access_service.h"
#include <memory>
#include <string>
namespace cardputer_hub::apps {
inline constexpr char ledCanvasAppId[] = "led-canvas";
class LedCanvasApp final : public IMiniApp {
  public:
    LedCanvasApp(services::LedCanvasService& canvas, const services::WebAccessService& web,
                 core::ActionBus& actions, services::IndicatorService& indicator,
                 core::IDisplayAdapter& display)
        : canvas_(canvas), web_(web), actions_(actions), indicator_(indicator), display_(display) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;
    bool handleBack() override;

  private:
    void draw();
    services::LedCanvasService& canvas_;
    const services::WebAccessService& web_;
    core::ActionBus& actions_;
    services::IndicatorService& indicator_;
    core::IDisplayAdapter& display_;
    services::IndicatorClaim claim_;
    std::unique_ptr<LedCanvasEngine> engine_;
    std::chrono::milliseconds outputElapsed_{0};
    std::uint32_t clearGeneration_ = 0;
    bool showingQr_ = false, wasLive_ = false;
    std::string drawn_;
};
} // namespace cardputer_hub::apps
