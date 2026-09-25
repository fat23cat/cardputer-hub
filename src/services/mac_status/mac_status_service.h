#pragma once

#include "services/companion/companion_service.h"

#include <chrono>
#include <cstdint>

namespace cardputer_hub::services {

enum class MacStatusFreshness : std::uint8_t { Empty, Fresh, Stale };
enum class MacMemoryPressure : std::uint8_t { Unknown, Normal, Warning, Critical };
enum class MacThermalState : std::uint8_t { Unknown, Normal, Fair, Serious, Critical };

struct MacStatusSnapshot {
    std::uint32_t generation = 0;
    MacStatusFreshness freshness = MacStatusFreshness::Empty;
    bool cpuAvailable = false;
    std::uint8_t cpuPercent = 0;
    bool memoryAvailable = false;
    std::uint32_t memoryUsedMiB = 0;
    std::uint32_t memoryTotalMiB = 0;
    MacMemoryPressure memoryPressure = MacMemoryPressure::Unknown;
    bool diskAvailable = false;
    std::uint8_t diskUsedPercent = 0;
    bool batteryAvailable = false;
    std::uint8_t batteryPercent = 0;
    bool networkAvailable = false;
    std::uint32_t downloadKiBps = 0;
    std::uint32_t uploadKiBps = 0;
    MacThermalState thermalState = MacThermalState::Unknown;
};

class MacStatusService {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(1);
    static constexpr auto freshnessTimeout = std::chrono::seconds(3);

    explicit MacStatusService(CompanionService& companion) : companion_(companion) {}
    void startMonitoring();
    void stopMonitoring();
    void update(std::chrono::milliseconds elapsed);
    MacStatusSnapshot snapshot() const noexcept { return snapshot_; }
    bool monitoring() const noexcept { return monitoring_; }

  private:
    void request();
    void accept(const connectivity::CompanionSystemMetrics& metrics);

    CompanionService& companion_;
    MacStatusSnapshot snapshot_{};
    bool monitoring_ = false;
    bool inFlight_ = false;
    std::uint8_t inFlightId_ = 0;
    std::chrono::milliseconds pollElapsed_{0};
    std::chrono::milliseconds sinceSample_{0};
};

} // namespace cardputer_hub::services
