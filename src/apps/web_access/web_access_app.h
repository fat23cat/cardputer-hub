#pragma once

#include "apps/runtime/mini_app.h"
#include "core/actions/action_bus.h"
#include "core/display/display_adapter.h"
#include "services/web_access/web_access_service.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace cardputer_hub::apps {

inline constexpr char webAccessAppId[] = "web-access";

// The size of one QR module on the full-screen join code, for a symbol of
// `modules` modules: as large as fits with a two-module light border.
[[nodiscard]] std::int32_t webAccessQrModulePixels(int modules);

// WEB ACCESS: starts and stops the local web UI and shows how to reach it:
// the network, its password (large, in groups of four), the address and the
// login. Enter starts or stops the service through ActionBus. While it runs, Q
// shows a full-screen QR code that joins the network from a phone camera; Q or
// Esc closes it. Esc otherwise closes the app and leaves the service running
// in the background; Home shows it.
class WebAccessApp final : public IMiniApp {
  public:
    WebAccessApp(const services::WebAccessService& web, core::ActionBus& actions,
                 core::IDisplayAdapter& display)
        : web_(web), actions_(actions), display_(display) {}
    void onActivate() override {
        drawn_.reset();
        showingQr_ = false;
    }
    void onDeactivate() override {
        drawn_.reset();
        showingQr_ = false;
    }
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;
    bool handleBack() override;

  private:
    struct Frame {
        services::WebAccessState state = services::WebAccessState::Off;
        std::string ssid;
        std::string password;
        std::size_t clients = 0;
    };

    void drawHeader(const Frame& frame);
    void drawBody(const Frame& frame);
    void drawQr(const Frame& frame);

    const services::WebAccessService& web_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    std::optional<Frame> drawn_;
    bool showingQr_ = false;
};

} // namespace cardputer_hub::apps
