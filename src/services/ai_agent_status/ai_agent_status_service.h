#pragma once

#include "services/companion/companion_service.h"

#include <chrono>
#include <cstdint>

namespace cardputer_hub::services {

enum class AgentStatusFreshness : std::uint8_t { Empty, Fresh, Stale };

struct AiAgentStatusSnapshot {
    // Changes whenever the presented states or freshness change.
    std::uint32_t generation = 0;
    AgentStatusFreshness freshness = AgentStatusFreshness::Empty;
    connectivity::CompanionAgentStatus status{};
};

// Polls the Companion's cached AI_AGENT_STATUS while AI is open, on either page. The Mac
// keeps observing agents on its own; nothing is polled while the app is closed.
class AiAgentStatusService {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(1);
    static constexpr auto freshnessTimeout = std::chrono::seconds(3);

    explicit AiAgentStatusService(CompanionService& companion) : companion_(companion) {}
    void startMonitoring();
    void stopMonitoring();
    bool monitoring() const noexcept { return monitoring_; }
    void update(std::chrono::milliseconds elapsed);
    const AiAgentStatusSnapshot& snapshot() const noexcept { return snapshot_; }

  private:
    void request();
    void clear();

    CompanionService& companion_;
    AiAgentStatusSnapshot snapshot_{};
    bool monitoring_ = false;
    bool inFlight_ = false;
    std::uint8_t inFlightId_ = 0;
    std::uint16_t session_ = 0;
    std::chrono::milliseconds pollElapsed_{0};
    std::chrono::milliseconds sinceSample_{0};
};

} // namespace cardputer_hub::services
