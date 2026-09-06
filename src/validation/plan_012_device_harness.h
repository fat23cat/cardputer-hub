#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/logging/logger.h"
#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"
#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

namespace cardputer_hub::validation {

class Plan012DeviceHarness final {
  public:
    explicit Plan012DeviceHarness(core::Logger& logger) noexcept;

    void start();
    void update();

  private:
    enum class InputMode : std::uint8_t {
        Command,
        WifiSsid,
        WifiPassphrase,
    };

    enum class CyclePhase : std::uint8_t {
        Idle,
        DisabledHold,
        WaitingForAdvertising,
        AdvertisingHold,
    };

    void readSerial(std::uint32_t now);
    void handleLine(char* line, std::uint32_t now);
    void handleCommand(const char* line, std::uint32_t now);
    void printHelp() const;
    void printStatus() const;
    void printHeap() const;
    void observeStateChanges();
    void sampleHeap(std::uint32_t now);

    void startBluetoothCycle(std::size_t count, std::uint32_t now);
    void startNextBluetoothCycle(std::uint32_t now);
    void updateBluetoothCycle(std::uint32_t now);
    void stopBluetoothCycle(const char* outcome);

    void startCoexistenceWatch(std::size_t minutes, std::uint32_t now);
    void updateCoexistenceWatch(std::uint32_t now);
    void stopCoexistenceWatch(const char* outcome);

    hardware::Esp32BluetoothAdapter bluetoothAdapter_;
    connectivity::BluetoothService bluetoothService_;
    hardware::Esp32WifiAdapter wifiAdapter_;
    connectivity::WiFiService wifiService_;

    static constexpr std::size_t inputCapacity = 96;
    std::array<char, inputCapacity> input_{};
    std::size_t inputLength_ = 0;
    bool inputOverflow_ = false;
    InputMode inputMode_ = InputMode::Command;
    std::string pendingSsid_;

    CyclePhase cyclePhase_ = CyclePhase::Idle;
    std::size_t cycleTarget_ = 0;
    std::size_t cycleCompleted_ = 0;
    std::uint32_t cycleDeadline_ = 0;
    std::uint32_t cycleInitialHeap_ = 0;

    bool coexistencePending_ = false;
    bool coexistenceActive_ = false;
    std::uint32_t coexistenceDeadline_ = 0;
    std::uint32_t coexistenceNextReport_ = 0;
    std::size_t coexistenceMinutes_ = 0;
    std::size_t coexistenceWifiInterruptions_ = 0;
    std::size_t coexistenceBluetoothInterruptions_ = 0;

    connectivity::BluetoothState previousBluetoothState_ = connectivity::BluetoothState::Disabled;
    connectivity::WifiState previousWifiState_ = connectivity::WifiState::Idle;
    std::uint32_t lastUpdate_ = 0;
    std::uint32_t nextHeapSample_ = 0;
    std::uint32_t lowestObservedHeap_ = 0;
    bool started_ = false;
};

} // namespace cardputer_hub::validation
