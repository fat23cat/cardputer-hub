#pragma once

#include "apps/mac_control/mac_control_bindings.h"
#include "apps/mac_control/mac_control_graphics.h"
#include "apps/runtime/mini_app.h"
#include "core/actions/action_bus.h"
#include "core/display/display_adapter.h"
#include "services/host_control/host_control_service.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace cardputer_hub::apps {

enum class MacControlView : std::uint8_t {
    Grid,
    PendingTakeover,
    SuccessTakeover,
    FailureTakeover,
};

class MacControlApp final : public IMiniApp {
  public:
    MacControlApp(core::ActionBus& actions, services::HostControlService& hostControl,
                  core::IDisplayAdapter& display,
                  std::vector<MacControlPage> pages = productionMacControlPages());

    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

    [[nodiscard]] MacControlView view() const noexcept { return view_; }
    [[nodiscard]] std::size_t pageIndex() const noexcept { return pageIndex_; }
    [[nodiscard]] std::uint8_t sourceSlot() const noexcept { return sourceSlot_; }
    [[nodiscard]] MacControlTileRect takeoverRect() const noexcept { return takeover_; }
    [[nodiscard]] MacControlTileRect sourceRect() const noexcept { return source_; }
    [[nodiscard]] bool animating() const noexcept { return view_ != MacControlView::Grid; }
    [[nodiscard]] const MacControlPage& currentPage() const;

  private:
    struct Frame {
        MacControlView view = MacControlView::Grid;
        std::size_t page = 0;
        MacControlTileRect takeover{};
        services::HostControlCommandState command = services::HostControlCommandState::Idle;
        services::HostControlFailure failure = services::HostControlFailure::None;
    };

    void resetLocalState();
    void handle(const core::InputEvent& event);
    void activateSlot(std::uint8_t slot);
    bool turnPage(int delta);
    void observeCommand();
    void advanceAnimation(std::chrono::milliseconds elapsed);
    void render();
    core::RgbColor takeoverSurface() const;

    core::ActionBus& actions_;
    services::HostControlService& hostControl_;
    core::IDisplayAdapter& display_;
    std::vector<MacControlPage> pages_;
    std::size_t pageIndex_ = 0;
    MacControlView view_ = MacControlView::Grid;
    std::uint8_t sourceSlot_ = 0;
    MacControlTileRect source_{};
    MacControlTileRect takeover_{};
    std::chrono::milliseconds phaseElapsed_{0};
    std::uint32_t commandGeneration_ = 0;
    services::HostControlFailure failure_ = services::HostControlFailure::None;
    std::optional<Frame> frame_;
};

} // namespace cardputer_hub::apps
