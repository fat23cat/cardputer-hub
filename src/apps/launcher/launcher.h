#pragma once

#include "apps/runtime/mini_app_runtime.h"
#include "core/actions/action_bus.h"
#include "core/app_registry/app_registry.h"
#include "core/display/display_adapter.h"
#include "core/input/input_event.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cardputer_hub::apps {

class Launcher {
  public:
    static constexpr std::size_t visibleRows = 3;
    static constexpr std::chrono::milliseconds overlayEnterDuration{120};
    static constexpr std::chrono::milliseconds overlayHoldDuration{1600};
    static constexpr std::chrono::milliseconds overlayExitDuration{120};

    Launcher(const core::AppRegistry& apps, MiniAppRuntime& runtime, core::ActionBus& actions,
             core::IDisplayAdapter& display);

    void activate();
    void deactivate();
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {});
    void showUnavailableOverlay();
    [[nodiscard]] bool animating() const;

  private:
    enum class OverlayPhase : std::uint8_t { Hidden, Entering, Visible, Exiting };

    struct Frame {
        std::size_t count = 0;
        std::size_t selected = 0;
        std::size_t windowStart = 0;
        std::vector<MiniAppEligibility> eligibility;
        OverlayPhase overlay = OverlayPhase::Hidden;
        int overlayY = -21;
        std::string overlayReason;
        int plateY = 0;
        bool empty = true;
    };

    void handle(const core::InputEvent& event);
    void moveSelection(int delta);
    void launchSelected();
    void clearOverlay();
    void startOverlay(std::string reason);
    void advanceOverlay(std::chrono::milliseconds elapsed);
    void advancePlate(std::chrono::milliseconds elapsed);
    void render();
    std::string overlayReasonFor(const MiniAppAvailability& availability) const;
    MiniAppAvailability selectedAvailability() const;
    std::vector<MiniAppEligibility> currentEligibility() const;

    const core::AppRegistry& apps_;
    MiniAppRuntime& runtime_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    std::size_t selected_ = 0;
    std::size_t windowStart_ = 0;
    OverlayPhase overlay_ = OverlayPhase::Hidden;
    float overlayY_ = -21;
    std::chrono::milliseconds holdElapsed_{0};
    std::string overlayReason_;
    float plateSlot_ = 0;
    float plateVelocity_ = 0;
    float plateTarget_ = 0;
    std::optional<Frame> frame_;
};

} // namespace cardputer_hub::apps
