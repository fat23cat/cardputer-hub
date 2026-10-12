#pragma once

#include "core/actions/action_bus.h"
#include "services/companion/companion_service.h"

#include <array>
#include <chrono>

namespace cardputer_hub::services {

// Asks the Mac to refresh its providers now, then reads quickly for the refresh
// window. AI dispatches it for R.
inline constexpr char aiUsageRefreshActionId[] = "ai-usage.refresh";

struct AiUsageMetricRef {
    std::uint8_t provider = 0;
    std::uint8_t metric = 0;
};

// Provider metrics flattened in snapshot order. AI USAGE rows, hover and the
// Puzzle bands share this order.
struct AiUsageVisibleMetrics {
    std::uint8_t count = 0;
    std::array<AiUsageMetricRef, 4> items{};
};

AiUsageVisibleMetrics aiUsageVisibleMetrics(const connectivity::CompanionAiUsage& usage) noexcept;

class AiUsageService final : public core::IActionHandler {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(10);
    // Discovery and a requested refresh both read at this interval.
    static constexpr auto discoveryPollInterval = std::chrono::seconds(2);
    // The Mac publishes each provider as it finishes and never says when its
    // refresh is complete, so a refresh reads quickly for this whole window.
    static constexpr auto refreshWindow = std::chrono::seconds(20);
    static constexpr auto freshnessTimeout = std::chrono::seconds(90);
    explicit AiUsageService(CompanionService& companion) : companion_(companion) {}
    void update(std::chrono::milliseconds elapsed);
    core::ActionHandlingResult handle(const core::Action& action) override;
    const connectivity::CompanionAiUsage& snapshot() const noexcept { return snapshot_; }
    bool available() const noexcept { return available_; }
    std::uint32_t revision() const noexcept { return revision_; }
    std::uint16_t session() const noexcept { return session_; }

  private:
    // Pending: the next request carries the refresh flag. Reading: it was
    // sent, and reads stay quick until the refresh window ends.
    enum class Refresh : std::uint8_t { None, Pending, Reading };

    void clear();
    void request();
    CompanionService& companion_;
    connectivity::CompanionAiUsage snapshot_{};
    std::uint32_t revision_ = 0;
    std::uint16_t session_ = 0;
    bool available_ = false;
    bool inFlight_ = false;
    std::uint8_t requestId_ = 0;
    std::chrono::milliseconds sincePoll_{0};
    std::chrono::milliseconds sinceSample_{0};
    std::chrono::milliseconds countdownElapsed_{0};
    Refresh refresh_ = Refresh::None;
    std::chrono::milliseconds refreshElapsed_{0};
};

} // namespace cardputer_hub::services
