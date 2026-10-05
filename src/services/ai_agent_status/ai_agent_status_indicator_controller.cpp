#include "services/ai_agent_status/ai_agent_status_indicator_controller.h"

namespace cardputer_hub::services {
namespace {
using connectivity::AgentState;

core::RgbColor colorFor(AgentState state) noexcept {
    switch (state) {
    case AgentState::Working:
        return agentWorkingLed;
    case AgentState::NeedsYou:
        return agentNeedsYouLed;
    case AgentState::Done:
        return agentDoneLed;
    case AgentState::DoneEarlier:
        return agentDoneEarlierLed;
    case AgentState::Unknown:
        break;
    }
    return agentUnknownLed;
}

void fillRows(IndicatorFrame& frame, std::uint8_t first, std::uint8_t count,
              core::RgbColor color) noexcept {
    for (std::uint8_t row = first; row < first + count; ++row)
        for (std::uint8_t column = 0; column < 8; ++column)
            frame.pixels[row * 8 + column] = color;
}
} // namespace

IndicatorFrame aiAgentStatusFrame(const connectivity::CompanionAgentStatus& status) noexcept {
    AgentState running[3]{};
    std::uint8_t count = 0;
    for (const auto application : connectivity::agentStatusOrder)
        if (status.installed(application))
            running[count++] = status.state(application);
    // Bands follow the screen rows top to bottom with no gap: 8, 4+4 or 3+3+2.
    constexpr std::uint8_t heights[3][3] = {{8, 0, 0}, {4, 4, 0}, {3, 3, 2}};
    IndicatorFrame frame{};
    std::uint8_t row = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        const auto height = heights[count - 1][i];
        fillRows(frame, row, height, colorFor(running[i]));
        if (count > 1) {
            frame.pixels[row * 8] = agentBandMarkerLed;
            frame.pixels[(row + height) * 8 - 1] = agentBandMarkerLed;
        }
        row = static_cast<std::uint8_t>(row + height);
    }
    return frame;
}

void AiAgentStatusIndicatorController::activate() {
    active_ = true;
    generation_ = status_.snapshot().generation - 1;
}

void AiAgentStatusIndicatorController::deactivate() {
    active_ = false;
    claim_.release();
}

void AiAgentStatusIndicatorController::update() {
    if (!active_)
        return;
    const auto& snapshot = status_.snapshot();
    if (snapshot.generation == generation_)
        return;
    generation_ = snapshot.generation;
    // Stale or missing delivery never shows old colors, and with no hooks
    // installed the matrix goes back to its other owners.
    if (snapshot.freshness != AgentStatusFreshness::Fresh ||
        snapshot.status.installedCount() == 0) {
        claim_.release();
        return;
    }
    if (!claim_.valid())
        claim_ = indicator_.acquire(aiAgentStatusIndicatorOwner,
                                    IndicatorPriority::ForegroundApplication);
    claim_.setFrame(aiAgentStatusFrame(snapshot.status));
}

} // namespace cardputer_hub::services
