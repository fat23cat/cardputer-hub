#pragma once

#include "services/hosts/host_service.h"
#include "services/web_access/web_access_service.h"

namespace cardputer_hub::services {

// Bluetooth and the WEB ACCESS access point share one radio. With BLE active,
// phones failed the WPA2 handshake, so Bluetooth is held off while the access
// point runs and restored from its saved setting when it stops.
class WebAccessBluetoothPause {
  public:
    WebAccessBluetoothPause(const WebAccessService& web, HostService& hosts) noexcept
        : web_(web), hosts_(hosts) {}

    // Called once per main loop, after WebAccessService::update().
    void update() {
        const bool hold = web_.status().state == WebAccessState::Running;
        if (hold == held_)
            return;
        held_ = hold;
        hosts_.setRadioHold(hold);
    }

  private:
    const WebAccessService& web_;
    HostService& hosts_;
    bool held_ = false;
};

} // namespace cardputer_hub::services
