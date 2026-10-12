#pragma once
#include "services/led_canvas/led_canvas_service.h"
#include "services/web_access/web_app.h"
namespace cardputer_hub::services {
class LedCanvasWebEndpoint final : public IWebApp {
  public:
    explicit LedCanvasWebEndpoint(LedCanvasService& canvas) : canvas_(canvas) {}
    std::string_view id() const override { return "led"; }
    std::string_view title() const override { return "LED"; }
    std::string summaryJson() override;
    connectivity::HttpResponse handle(const connectivity::HttpRequest& request,
                                      std::string_view subpath) override;
    void reset() override { canvas_.endPointers(); }

  private:
    std::string statusJson(bool settings) const;
    LedCanvasService& canvas_;
};
} // namespace cardputer_hub::services
