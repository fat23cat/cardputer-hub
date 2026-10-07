#pragma once

#include "apps/runtime/mini_app.h"
#include "apps/service_status/service_status_graphics.h"
#include "core/actions/action_bus.h"
#include "core/display/display_adapter.h"
#include "services/service_status/service_status_service.h"

#include <cstdint>
#include <optional>

namespace cardputer_hub::apps {

// SERVICES HEALTH: one row per status page (GitHub, Anthropic, OpenAI, Cursor) with
// its level, and below them one line for problems and one for the age. There
// is no selection; R checks every page again. Opening the app starts
// the checks and closing it stops them.
class ServiceStatusApp final : public IMiniApp {
  public:
    ServiceStatusApp(services::ServiceStatusService& status, core::ActionBus& actions,
                     core::IDisplayAdapter& display)
        : status_(status), actions_(actions), display_(display) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

  private:
    services::ServiceStatusService& status_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    std::optional<ServiceStatusFrame> drawn_;
    std::uint32_t drawnRevision_ = 0;
    long long drawnStep_ = -1;
};

} // namespace cardputer_hub::apps
