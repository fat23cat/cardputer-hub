#pragma once

#include "services/companion/companion_service.h"

#include <chrono>

namespace cardputer_hub::services {

class AiUsageService {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(30);
    static constexpr auto freshnessTimeout = std::chrono::seconds(90);
    explicit AiUsageService(CompanionService& companion) : companion_(companion) {}
    void update(std::chrono::milliseconds elapsed);
    const connectivity::CompanionAiUsage& snapshot() const noexcept { return snapshot_; }
    bool available() const noexcept { return available_; }
    std::uint32_t revision() const noexcept { return revision_; }
    std::uint16_t session() const noexcept { return session_; }

  private:
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
};

} // namespace cardputer_hub::services
