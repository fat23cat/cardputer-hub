#pragma once

#include "services/ai_agent_status/ai_agent_status_service.h"
#include "services/indicator/indicator_service.h"

namespace cardputer_hub::services {

inline constexpr char aiAgentStatusIndicatorOwner[] = "ai-status";

// Muted matrix tones: the Pomodoro work blue and break green, plus a matching
// vermilion, because the Puzzle diodes are harsh even at low brightness.
inline constexpr core::RgbColor agentWorkingLed{0x38, 0x4C, 0x7A};
inline constexpr core::RgbColor agentDoneLed{0x4C, 0x7A, 0x30};
// Done more than ten minutes ago: a dimmer green.
inline constexpr core::RgbColor agentDoneEarlierLed{0x1E, 0x30, 0x13};
inline constexpr core::RgbColor agentNeedsYouLed{0x8A, 0x3A, 0x22};
// A dim neutral band keeps an installed application with no known state visible.
inline constexpr core::RgbColor agentUnknownLed{0x24, 0x22, 0x1E};
// The AI USAGE band-end purple; marks the first and last pixel of each band
// when the matrix is split, so equal neighbouring states stay apart.
inline constexpr core::RgbColor agentBandMarkerLed{0xA0, 0x50, 0xD0};

// Shows the applications whose hooks are installed as bands in the screen row
// order Codex, Claude, Cursor with no gap: one fills the matrix, two use rows
// 0-3 and 4-7, three use rows 0-2, 3-5 and 6-7. Split bands start and end
// with a purple pixel, as in AI USAGE; one application has no markers. With
// none installed the frame is empty and the controller holds no claim.
IndicatorFrame aiAgentStatusFrame(const connectivity::CompanionAgentStatus& status) noexcept;

// Owns the Puzzle only between activate() and deactivate(), while AI's STATUS
// page is visible.
class AiAgentStatusIndicatorController {
  public:
    AiAgentStatusIndicatorController(AiAgentStatusService& status, IndicatorService& indicator)
        : status_(status), indicator_(indicator) {}
    void activate();
    void deactivate();
    void update();

  private:
    AiAgentStatusService& status_;
    IndicatorService& indicator_;
    IndicatorClaim claim_;
    bool active_ = false;
    std::uint32_t generation_ = 0;
};

} // namespace cardputer_hub::services
