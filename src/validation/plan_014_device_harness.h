#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/display/display_adapter.h"
#include "core/input/keyboard_adapter.h"
#include "core/logging/logger.h"
#include "core/platform/platform_adapter.h"
#include "core/storage/files/file_storage.h"
#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"
#include "hardware/esp32/wifi/esp32_wifi_adapter.h"
#include "hardware/storage/microsd/cardputer_microsd_file_storage_adapter.h"

namespace cardputer_hub::validation {

class Plan014BluetoothAdapter final : public connectivity::IBluetoothAdapter {
  public:
    connectivity::BluetoothAdapterResult
    initialize(const connectivity::BluetoothDeviceConfig& config, std::uint32_t lifecycle) override;
    connectivity::BluetoothAdapterResult shutdown() override;
    connectivity::BluetoothAdvertisingResult startAdvertising(std::uint32_t lifecycle) override;
    connectivity::BluetoothAdapterResult requestAdvertisingStop() override;
    connectivity::BluetoothAdapterResult
    disconnectPeer(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothPollResult pollEvent() override;
    connectivity::BluetoothBondQueryResult
    bondState(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    beginPairing(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    respondToPairing(connectivity::BluetoothPeerHandle peer,
                     connectivity::BluetoothPairingChallengeType type, bool accepted,
                     std::optional<std::uint32_t> passkey) override;
    connectivity::BluetoothBondListResult bonds() override;
    connectivity::BluetoothBondReferenceResult
    bondReference(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothAdapterResult
    deleteBond(const connectivity::BluetoothBondReference& reference) override;
    connectivity::BluetoothAdapterResult
    deleteBondForPeer(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothHidAdapterResult
    hidReadiness(connectivity::BluetoothPeerHandle peer) override;
    connectivity::BluetoothHidAdapterResult
    sendHidReport(connectivity::BluetoothPeerHandle peer,
                  const connectivity::HidReport& report) override;
    connectivity::BluetoothHidAdapterResult
    releaseHidReports(connectivity::BluetoothPeerHandle peer) override;

    void failNextPoll() noexcept;

  private:
    hardware::Esp32BluetoothAdapter adapter_;
    bool failNextPoll_ = false;
};

class Plan014DeviceHarness final {
  public:
    Plan014DeviceHarness(core::IPlatformAdapter& platform, core::IKeyboardAdapter& keyboard,
                         core::IDisplayAdapter& display, core::Logger& logger) noexcept;

    void start();
    void update();

  private:
    enum class InputMode : std::uint8_t { Command, WifiSsid, WifiPassphrase };

    void readSerial();
    void handleLine(char* line);
    void handleCommand(const char* line);
    void printHelp() const;
    void printStatus();
    void observeStateChanges();
    void observePairingChallenge();
    void handleKeyboard();
    void displayStatus(const char* detail);
    void displayPairingChallenge(const connectivity::BluetoothPairingChallenge& challenge);
    void displayPasskeyEntry();
    void answerChallenge(bool accepted);
    void refreshBonds();
    std::optional<std::size_t> parseBondIndex(const char* text) const;
    void saveReference(std::size_t index);
    void verifyReference();
    void clearSavedReference();
    void checkStorage();
    void sendHid(const connectivity::HidReport& report);
    void releaseHid();

    core::IPlatformAdapter& platform_;
    core::IKeyboardAdapter& keyboard_;
    core::IDisplayAdapter& display_;
    Plan014BluetoothAdapter bluetoothAdapter_;
    connectivity::BluetoothService bluetoothService_;
    hardware::Esp32WifiAdapter wifiAdapter_;
    connectivity::WiFiService wifiService_;
    hardware::CardputerMicroSdFileStorageAdapter storageAdapter_;
    core::FileStorage storage_;

    static constexpr std::size_t inputCapacity = 96;
    std::array<char, inputCapacity> input_{};
    std::size_t inputLength_ = 0;
    bool inputOverflow_ = false;
    InputMode inputMode_ = InputMode::Command;
    std::string pendingSsid_;

    std::vector<connectivity::BluetoothBondReference> bonds_;
    std::optional<connectivity::BluetoothPairingChallenge> activeChallenge_;
    std::array<char, 7> enteredPasskey_{};
    std::size_t enteredPasskeyLength_ = 0;
    connectivity::BluetoothState previousBluetoothState_ = connectivity::BluetoothState::Disabled;
    connectivity::BluetoothPairingState previousPairingState_ =
        connectivity::BluetoothPairingState::Closed;
    std::uint32_t lastUpdateMilliseconds_ = 0;
    std::size_t keyboardEvents_ = 0;
    bool displayPasskeyVisible_ = false;
    bool started_ = false;
};

} // namespace cardputer_hub::validation
