#pragma once

#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/ai_agent_status/ai_agent_status_indicator_controller.h"

#include <array>
#include <chrono>

namespace cardputer_hub::apps {

// AI STATUS: one full-width plate per application whose hooks Companion has
// installed (in the order CODEX, CLAUDE, CURSOR), filled with its state color
// and labelled with the name and WORKING / NEEDS YOU / DONE / --. The plates
// share the full display without a header. Stale delivery presents every
// plate as --. Before the first answer the screen reads CHECKING AI; with no
// hooks installed it reads NO AI HOOKS.
class AiAgentStatusApp final : public IMiniApp {
  public:
    AiAgentStatusApp(services::AiAgentStatusService& status,
                     services::AiAgentStatusIndicatorController& indicator,
                     core::IDisplayAdapter& display, bool manageMonitoring = true)
        : status_(status), indicator_(indicator), display_(display),
          manageMonitoring_(manageMonitoring) {}
    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;

  private:
    enum class Layout : std::uint8_t { None, Checking, NoHooks, Plates };

    // Per application, indexed by AiProvider value - 1.
    struct Plate {
        // Position among the shown plates, or -1.
        int index = -1;
        connectivity::AgentState drawn = connectivity::AgentState::Unknown;
    };

    void drawLayout(Layout layout, const connectivity::CompanionAgentStatus& plates);
    void drawPlate(connectivity::AiProvider application, connectivity::AgentState state);

    services::AiAgentStatusService& status_;
    services::AiAgentStatusIndicatorController& indicator_;
    core::IDisplayAdapter& display_;
    std::array<Plate, 3> plates_{};
    int plateCount_ = 0;
    Layout layout_ = Layout::None;
    // The applications that own plates; kept through stale periods.
    connectivity::CompanionAgentStatus shown_{};
    std::uint32_t generation_ = 0;
    // AiApp owns monitoring across both pages; a standalone view owns its own.
    bool manageMonitoring_;
};

} // namespace cardputer_hub::apps
