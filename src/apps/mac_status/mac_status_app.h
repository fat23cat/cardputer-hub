#pragma once

#include "apps/mac_status/mac_status_graphics.h"
#include "apps/runtime/mini_app.h"

#include <optional>

namespace cardputer_hub::apps {

class MacStatusApp final : public IMiniApp {
  public:
    MacStatusApp(services::MacStatusService& service, core::IDisplayAdapter& display)
        : service_(service), display_(display) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents&, std::chrono::milliseconds) override;

  private:
    services::MacStatusService& service_;
    core::IDisplayAdapter& display_;
    std::optional<MacStatusPresentation> frame_;
};

} // namespace cardputer_hub::apps
