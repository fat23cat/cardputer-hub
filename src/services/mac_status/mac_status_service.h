#pragma once

#include "services/companion/companion_service.h"
#include "services/mac_status/mac_status_history.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace cardputer_hub::services {

enum class MacStatusFreshness : std::uint8_t { Empty, Fresh, Stale };
enum class MacMemoryPressure : std::uint8_t { Unknown, Normal, Warning, Critical };
enum class MacThermalState : std::uint8_t { Unknown, Normal, Fair, Serious, Critical };
enum class MacPowerSource : std::uint8_t { Unknown, Battery, Charging, AcPower };
enum class MacDetailGroup : std::uint8_t { None, Cpu, Power, Network, Memory };

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
    MacPowerSource powerSource = MacPowerSource::Unknown;
    bool batteryMinutesAvailable = false;
    std::uint16_t batteryMinutes = 0;
};

struct MacTopApp {
    std::uint8_t percent = 0;
    std::string name;
};

// The visible detail page's group. Absent optionals are unavailable fields.
struct MacStatusDetails {
    std::uint32_t generation = 0;
    MacStatusFreshness freshness = MacStatusFreshness::Empty;
    MacDetailGroup group = MacDetailGroup::None;
    std::optional<std::uint8_t> performancePercent;
    std::optional<std::uint8_t> efficiencyPercent;
    std::optional<std::uint8_t> gpuPercent;
    bool appsAvailable = false;
    std::uint8_t appCount = 0;
    std::array<MacTopApp, 4> apps{};
    std::optional<std::uint16_t> systemDrawDeciwatts;
    std::optional<std::uint8_t> adapterWatts;
    std::optional<std::uint8_t> healthPercent;
    std::optional<std::uint16_t> cycleCount;
    std::optional<std::uint8_t> peripheralPercent;
    std::string peripheralName;
    std::optional<std::uint16_t> internetRttMs;
    std::optional<std::uint16_t> routerRttMs;
    std::optional<std::int8_t> wifiRssiDbm;
    std::optional<std::uint16_t> wifiLinkMbps;
    bool memorySplitAvailable = false;
    std::uint32_t appMiB = 0;
    std::uint32_t wiredMiB = 0;
    std::uint32_t compressedMiB = 0;
    std::optional<std::uint32_t> swapUsedMiB;
    bool ssdAvailable = false;
    std::uint16_t ssdFreeGB = 0;
    std::uint16_t ssdTotalGB = 0;
    bool diskRatesAvailable = false;
    std::uint32_t diskReadKiBps = 0;
    std::uint32_t diskWriteKiBps = 0;
};

class MacStatusService {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(1);
    static constexpr auto freshnessTimeout = std::chrono::seconds(3);
    static constexpr auto detailPollInterval = std::chrono::seconds(2);
    static constexpr auto detailFreshnessTimeout = std::chrono::seconds(6);

    explicit MacStatusService(CompanionService& companion) : companion_(companion) {}
    // All monitoring state lives on the heap from startMonitoring() to
    // stopMonitoring(), so a closed MAC STATUS keeps no history or details.
    void startMonitoring();
    void stopMonitoring();
    void update(std::chrono::milliseconds elapsed);
    MacStatusSnapshot snapshot() const noexcept {
        return state_ ? state_->snapshot : MacStatusSnapshot{};
    }
    // Null while not monitoring.
    const MacStatusHistory* history() const noexcept { return state_ ? &state_->history : nullptr; }
    bool monitoring() const noexcept { return state_ != nullptr; }

    // Detail pages exist whenever a Companion session is ready.
    bool detailsSupported() const noexcept { return companion_.hasLiveCompanion(); }
    // Polls one group every two seconds while monitoring; None stops detail polling.
    void setDetailGroup(MacDetailGroup group);
    // Null while not monitoring.
    const MacStatusDetails* details() const noexcept { return state_ ? &state_->details : nullptr; }

  private:
    struct State {
        MacStatusSnapshot snapshot{};
        MacStatusHistory history{};
        bool inFlight = false;
        std::uint8_t inFlightId = 0;
        std::uint16_t session = 0;
        std::chrono::milliseconds pollElapsed{0};
        std::chrono::milliseconds sinceSample{0};
        MacStatusDetails details{};
        MacDetailGroup detailGroup = MacDetailGroup::None;
        MacDetailGroup detailInFlightGroup = MacDetailGroup::None;
        bool detailInFlight = false;
        bool detailDue = false;
        std::uint8_t detailInFlightId = 0;
        std::chrono::milliseconds sinceDetailRequest{0};
        std::chrono::milliseconds sinceDetail{0};
    };

    bool request();
    void requestDetails();
    void accept(const connectivity::CompanionSystemMetrics& metrics);
    void acceptDetails(const connectivity::CompanionSystemDetails& details);
    void resetDetails();
    void processDetailCompletions();

    CompanionService& companion_;
    std::unique_ptr<State> state_;
};

} // namespace cardputer_hub::services
