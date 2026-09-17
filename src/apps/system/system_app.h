#pragma once

#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/battery/battery_service.h"
#include "services/hosts/host_service.h"
#include "services/network/network_service.h"

#include <cstdint>
#include <optional>
#include <string>

namespace cardputer_hub::apps {

[[nodiscard]] const char* systemWifiStatusText(const services::WifiStatusSnapshot& status);

class SystemApp final : public IMiniApp {
  public:
    SystemApp(services::BatteryService& battery, services::HostService& hosts,
              services::NetworkService& network, core::IDisplayAdapter& display);

    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

  private:
    struct Frame {
        std::string battery;
        std::string bluetooth;
        std::string host;
        std::string hostStatus;
        std::string wifi;
        std::string network;
        std::string version;
    };

    Frame capture() const;
    void render(const Frame& next);

    services::BatteryService& battery_;
    services::HostService& hosts_;
    services::NetworkService& network_;
    core::IDisplayAdapter& display_;
    std::optional<Frame> frame_;
};

} // namespace cardputer_hub::apps
