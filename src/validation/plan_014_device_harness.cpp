#include "validation/plan_014_device_harness.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

namespace cardputer_hub::validation {
namespace {

constexpr const char* validationDeviceName = "Cardputer Hub 014 Test";
constexpr const char* validationPartition = "hub_config";
constexpr const char* validationNamespace = "validation014";
constexpr const char* savedReferenceKey = "expected_bond";
constexpr const char* storageProbePath = "plan014-probe.bin";
constexpr core::RgbColor black{0, 0, 0};
constexpr core::RgbColor white{255, 255, 255};
constexpr core::RgbColor cyan{0, 220, 220};
constexpr core::RgbColor yellow{255, 220, 0};
constexpr core::TextStyle headingStyle{cyan, black, 2};
constexpr core::TextStyle bodyStyle{white, black, 1};
constexpr core::TextStyle valueStyle{yellow, black, 3};

std::uint32_t millis() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000); }

class NativeSerialConsole {
  public:
    int read() const {
        unsigned char value = 0;
        return ::read(STDIN_FILENO, &value, 1) == 1 ? value : -1;
    }

    void println(const char* message) const { std::printf("%s\n", message); }

    template <typename... Arguments> void printf(const char* format, Arguments... arguments) const {
        std::printf(format, arguments...);
    }
};

constexpr NativeSerialConsole Serial;

const char* bluetoothStateName(connectivity::BluetoothState state) {
    switch (state) {
    case connectivity::BluetoothState::Disabled:
        return "disabled";
    case connectivity::BluetoothState::Idle:
        return "idle";
    case connectivity::BluetoothState::Advertising:
        return "advertising";
    case connectivity::BluetoothState::Connected:
        return "connected";
    case connectivity::BluetoothState::RetryWaiting:
        return "retry-waiting";
    case connectivity::BluetoothState::Error:
        return "error";
    }
    return "unknown";
}

const char* pairingStateName(connectivity::BluetoothPairingState state) {
    switch (state) {
    case connectivity::BluetoothPairingState::Closed:
        return "closed";
    case connectivity::BluetoothPairingState::Preparing:
        return "preparing";
    case connectivity::BluetoothPairingState::Advertising:
        return "advertising";
    case connectivity::BluetoothPairingState::AwaitingResponse:
        return "awaiting-response";
    case connectivity::BluetoothPairingState::Completing:
        return "completing";
    case connectivity::BluetoothPairingState::Succeeded:
        return "succeeded";
    case connectivity::BluetoothPairingState::Error:
        return "error";
    }
    return "unknown";
}

const char* wifiStateName(connectivity::WifiState state) {
    switch (state) {
    case connectivity::WifiState::Idle:
        return "idle";
    case connectivity::WifiState::Connecting:
        return "connecting";
    case connectivity::WifiState::Connected:
        return "connected";
    case connectivity::WifiState::RetryWaiting:
        return "retry-waiting";
    case connectivity::WifiState::Error:
        return "error";
    }
    return "unknown";
}

void clearString(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

} // namespace

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::initialize(const connectivity::BluetoothDeviceConfig& config,
                                    std::uint32_t lifecycle) {
    return adapter_.initialize(config, lifecycle);
}

connectivity::BluetoothAdapterResult Plan014BluetoothAdapter::shutdown() {
    return adapter_.shutdown();
}

connectivity::BluetoothAdvertisingResult
Plan014BluetoothAdapter::startAdvertising(std::uint32_t lifecycle) {
    return adapter_.startAdvertising(lifecycle);
}

connectivity::BluetoothAdapterResult Plan014BluetoothAdapter::requestAdvertisingStop() {
    return adapter_.requestAdvertisingStop();
}

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::disconnectPeer(connectivity::BluetoothPeerHandle peer) {
    return adapter_.disconnectPeer(peer);
}

connectivity::BluetoothPollResult Plan014BluetoothAdapter::pollEvent() {
    if (failNextPoll_) {
        failNextPoll_ = false;
        return connectivity::BluetoothPollResult::adapterError();
    }
    return adapter_.pollEvent();
}

connectivity::BluetoothBondQueryResult
Plan014BluetoothAdapter::bondState(connectivity::BluetoothPeerHandle peer) {
    return adapter_.bondState(peer);
}

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::beginPairing(connectivity::BluetoothPeerHandle peer) {
    return adapter_.beginPairing(peer);
}

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::respondToPairing(connectivity::BluetoothPeerHandle peer,
                                          connectivity::BluetoothPairingChallengeType type,
                                          bool accepted, std::optional<std::uint32_t> passkey) {
    return adapter_.respondToPairing(peer, type, accepted, passkey);
}

connectivity::BluetoothBondListResult Plan014BluetoothAdapter::bonds() { return adapter_.bonds(); }

connectivity::BluetoothBondReferenceResult
Plan014BluetoothAdapter::bondReference(connectivity::BluetoothPeerHandle peer) {
    return adapter_.bondReference(peer);
}

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::deleteBond(const connectivity::BluetoothBondReference& reference) {
    return adapter_.deleteBond(reference);
}

connectivity::BluetoothAdapterResult
Plan014BluetoothAdapter::deleteBondForPeer(connectivity::BluetoothPeerHandle peer) {
    return adapter_.deleteBondForPeer(peer);
}

void Plan014BluetoothAdapter::failNextPoll() noexcept { failNextPoll_ = true; }

Plan014DeviceHarness::Plan014DeviceHarness(core::IPlatformAdapter& platform,
                                           core::IKeyboardAdapter& keyboard,
                                           core::IDisplayAdapter& display,
                                           core::Logger& logger) noexcept
    : platform_(platform), keyboard_(keyboard), display_(display),
      bluetoothService_(bluetoothAdapter_, logger), wifiService_(wifiAdapter_, logger),
      storage_(storageAdapter_) {}

void Plan014DeviceHarness::start() {
    if (started_) {
        return;
    }
    started_ = true;
    platform_.begin();
    lastUpdateMilliseconds_ = millis();
    previousBluetoothState_ = bluetoothService_.state();
    previousPairingState_ = bluetoothService_.pairingState();
    displayStatus("type help in serial monitor");
    Serial.println("[VALIDATION 014] authenticated pairing harness active");
    Serial.printf("[VALIDATION 014] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
    Serial.println("[VALIDATION 014] pairing values appear only on the Cardputer display");
    printStatus();
}

void Plan014DeviceHarness::update() {
    if (!started_) {
        return;
    }
    platform_.update();
    const auto now = millis();
    const auto elapsed = std::chrono::milliseconds(now - lastUpdateMilliseconds_);
    lastUpdateMilliseconds_ = now;
    bluetoothService_.update(elapsed);
    wifiService_.update(elapsed);
    observeStateChanges();
    observePairingChallenge();
    handleKeyboard();
    readSerial();
}

void Plan014DeviceHarness::readSerial() {
    for (;;) {
        const int next = Serial.read();
        if (next < 0) {
            return;
        }
        const char value = static_cast<char>(next);
        if (value == '\r') {
            continue;
        }
        if (value == '\n') {
            if (inputOverflow_) {
                Serial.println("[VALIDATION 014] input rejected: line too long");
            } else {
                input_[inputLength_] = '\0';
                handleLine(input_.data());
            }
            input_.fill('\0');
            inputLength_ = 0;
            inputOverflow_ = false;
            continue;
        }
        if (!inputOverflow_ && inputLength_ + 1 < input_.size()) {
            input_[inputLength_++] = value;
        } else {
            inputOverflow_ = true;
        }
    }
}

void Plan014DeviceHarness::handleLine(char* line) {
    if (inputMode_ == InputMode::WifiSsid) {
        if (std::strcmp(line, "/cancel") == 0) {
            inputMode_ = InputMode::Command;
            clearString(pendingSsid_);
            Serial.println("[VALIDATION 014] Wi-Fi entry cancelled");
        } else if (*line == '\0') {
            Serial.println("[VALIDATION 014] SSID must not be empty");
        } else {
            pendingSsid_ = line;
            inputMode_ = InputMode::WifiPassphrase;
            Serial.println("[VALIDATION 014] enter test passphrase, blank for open, or /cancel");
        }
        return;
    }
    if (inputMode_ == InputMode::WifiPassphrase) {
        if (std::strcmp(line, "/cancel") == 0) {
            Serial.println("[VALIDATION 014] Wi-Fi entry cancelled");
        } else {
            connectivity::WifiNetworkConfig config{pendingSsid_, line};
            const auto result = wifiService_.connect(config);
            Serial.printf("[VALIDATION 014] Wi-Fi connect result=%s\n",
                          result == connectivity::WifiConnectResult::Started ? "started"
                                                                             : "failed");
            clearString(config.passphrase);
            clearString(config.ssid);
        }
        clearString(pendingSsid_);
        inputMode_ = InputMode::Command;
        return;
    }
    if (*line != '\0') {
        handleCommand(line);
    }
}

void Plan014DeviceHarness::handleCommand(const char* line) {
    if (std::strcmp(line, "help") == 0) {
        printHelp();
    } else if (std::strcmp(line, "status") == 0) {
        printStatus();
    } else if (std::strcmp(line, "bt enable") == 0) {
        const auto result = bluetoothService_.enable({validationDeviceName});
        Serial.printf("[VALIDATION 014] Bluetooth enable result=%u\n",
                      static_cast<unsigned>(result));
    } else if (std::strcmp(line, "bt disable") == 0) {
        const auto result = bluetoothService_.disable();
        Serial.printf("[VALIDATION 014] Bluetooth disable result=%u\n",
                      static_cast<unsigned>(result));
    } else if (std::strcmp(line, "pair open") == 0) {
        const auto result = bluetoothService_.openPairing();
        Serial.printf("[VALIDATION 014] pairing open result=%u\n", static_cast<unsigned>(result));
    } else if (std::strcmp(line, "pair cancel") == 0) {
        const auto result = bluetoothService_.cancelPairing();
        Serial.printf("[VALIDATION 014] pairing cancel result=%u\n", static_cast<unsigned>(result));
    } else if (std::strcmp(line, "bonds") == 0) {
        refreshBonds();
    } else if (std::strncmp(line, "bond select ", 12) == 0) {
        const auto index = parseBondIndex(line + 12);
        if (!index.has_value()) {
            Serial.println("[VALIDATION 014] invalid bond index; run bonds first");
        } else {
            const auto result = bluetoothService_.selectBond(bonds_[*index]);
            Serial.printf("[VALIDATION 014] bond selection result=%u\n",
                          static_cast<unsigned>(result));
        }
    } else if (std::strcmp(line, "bond select none") == 0) {
        const auto result = bluetoothService_.selectBond(std::nullopt);
        Serial.printf("[VALIDATION 014] bond selection result=%u\n", static_cast<unsigned>(result));
    } else if (std::strncmp(line, "bond remove ", 12) == 0) {
        const auto index = parseBondIndex(line + 12);
        if (!index.has_value()) {
            Serial.println("[VALIDATION 014] invalid bond index; run bonds first");
        } else {
            const auto result = bluetoothService_.removeBond(bonds_[*index]);
            Serial.printf("[VALIDATION 014] bond removal result=%u\n",
                          static_cast<unsigned>(result));
        }
    } else if (std::strcmp(line, "bonds remove-all") == 0) {
        const auto result = bluetoothService_.removeAllBonds();
        Serial.printf("[VALIDATION 014] remove-all result=%u\n", static_cast<unsigned>(result));
    } else if (std::strncmp(line, "reference save ", 15) == 0) {
        const auto index = parseBondIndex(line + 15);
        if (index.has_value()) {
            saveReference(*index);
        } else {
            Serial.println("[VALIDATION 014] invalid bond index; run bonds first");
        }
    } else if (std::strcmp(line, "reference verify") == 0) {
        verifyReference();
    } else if (std::strcmp(line, "reference clear") == 0) {
        clearSavedReference();
    } else if (std::strcmp(line, "fault bluetooth") == 0) {
        bluetoothAdapter_.failNextPoll();
        Serial.println("[VALIDATION 014] next Bluetooth poll will fail");
    } else if (std::strcmp(line, "wifi connect") == 0) {
        inputMode_ = InputMode::WifiSsid;
        Serial.println(
            "[VALIDATION 014] enter test SSID or /cancel; input is not logged by firmware");
    } else if (std::strcmp(line, "wifi disconnect") == 0) {
        const auto result = wifiService_.disconnect();
        Serial.printf("[VALIDATION 014] Wi-Fi disconnect result=%u\n",
                      static_cast<unsigned>(result));
    } else if (std::strcmp(line, "storage check") == 0) {
        checkStorage();
    } else {
        Serial.println("[VALIDATION 014] unknown command; type help");
    }
}

void Plan014DeviceHarness::printHelp() const {
    Serial.println("[VALIDATION 014] commands:");
    Serial.println("  status | bt enable | bt disable | pair open | pair cancel");
    Serial.println("  bonds | bond select <index> | bond select none");
    Serial.println("  bond remove <index> | bonds remove-all");
    Serial.println("  reference save <index> | reference verify | reference clear");
    Serial.println("  wifi connect | wifi disconnect | storage check | fault bluetooth");
    Serial.println("  Pairing responses use Cardputer Enter/Esc; passkeys use digit keys");
}

void Plan014DeviceHarness::printStatus() {
    const auto result = bluetoothService_.bonds();
    const bool bondQuerySucceeded = result.status == connectivity::BluetoothBondListStatus::Success;
    Serial.printf("[VALIDATION 014] status bluetooth=%s pairing=%s connected=%s bonds=%s",
                  bluetoothStateName(bluetoothService_.state()),
                  pairingStateName(bluetoothService_.pairingState()),
                  bluetoothService_.currentConnection().has_value() ? "yes" : "no",
                  bondQuerySucceeded ? "available" : "unavailable");
    if (bondQuerySucceeded) {
        Serial.printf(" count=%u", static_cast<unsigned>(result.bonds.size()));
    }
    Serial.printf(" wifi=%s storage=%u keyboard_events=%u last_remove=%u last_remove_all=%u\n",
                  wifiStateName(wifiService_.state()), static_cast<unsigned>(storage_.state()),
                  static_cast<unsigned>(keyboardEvents_),
                  static_cast<unsigned>(bluetoothService_.lastBondRemovalResult()),
                  static_cast<unsigned>(bluetoothService_.lastRemoveAllResult()));
    if (!activeChallenge_.has_value() && !displayPasskeyVisible_) {
        displayStatus("status refreshed");
    }
}

void Plan014DeviceHarness::observeStateChanges() {
    const auto bluetoothState = bluetoothService_.state();
    if (bluetoothState != previousBluetoothState_) {
        Serial.printf("[VALIDATION 014] Bluetooth state=%s\n", bluetoothStateName(bluetoothState));
        previousBluetoothState_ = bluetoothState;
    }
    const auto pairingState = bluetoothService_.pairingState();
    if (pairingState != previousPairingState_) {
        Serial.printf("[VALIDATION 014] pairing state=%s\n", pairingStateName(pairingState));
        previousPairingState_ = pairingState;
        if (pairingState == connectivity::BluetoothPairingState::Succeeded ||
            pairingState == connectivity::BluetoothPairingState::Error ||
            pairingState == connectivity::BluetoothPairingState::Closed) {
            activeChallenge_.reset();
            displayPasskeyVisible_ = false;
            enteredPasskey_.fill('\0');
            enteredPasskeyLength_ = 0;
            displayStatus(pairingStateName(pairingState));
        }
    }
}

void Plan014DeviceHarness::observePairingChallenge() {
    const auto challenge = bluetoothService_.pairingChallenge();
    if (!challenge.has_value() ||
        (activeChallenge_.has_value() && activeChallenge_->generation == challenge->generation)) {
        return;
    }
    activeChallenge_ = challenge;
    enteredPasskey_.fill('\0');
    enteredPasskeyLength_ = 0;
    displayPairingChallenge(*challenge);
    Serial.printf("[VALIDATION 014] pairing challenge type=%u generation=%u\n",
                  static_cast<unsigned>(challenge->type),
                  static_cast<unsigned>(challenge->generation));
    if (challenge->type == connectivity::BluetoothPairingChallengeType::DisplayPasskey) {
        displayPasskeyVisible_ = true;
        answerChallenge(true);
    }
}

void Plan014DeviceHarness::handleKeyboard() {
    core::InputEvents events;
    keyboard_.poll(events);
    keyboardEvents_ += events.size();
    if (!activeChallenge_.has_value()) {
        return;
    }
    for (const auto& event : events) {
        if (event.type == core::InputEventType::NamedKey &&
            event.namedKey == core::NamedKey::Escape) {
            answerChallenge(false);
            return;
        }
        if (activeChallenge_->type ==
            connectivity::BluetoothPairingChallengeType::ConfirmComparison) {
            if (event.type == core::InputEventType::NamedKey &&
                event.namedKey == core::NamedKey::Enter) {
                answerChallenge(true);
                return;
            }
            continue;
        }
        if (activeChallenge_->type != connectivity::BluetoothPairingChallengeType::EnterPasskey) {
            continue;
        }
        if (event.type == core::InputEventType::PrintableCharacter && event.character >= '0' &&
            event.character <= '9' && enteredPasskeyLength_ < 6) {
            enteredPasskey_[enteredPasskeyLength_++] = event.character;
            displayPasskeyEntry();
        } else if (event.type == core::InputEventType::NamedKey &&
                   event.namedKey == core::NamedKey::Backspace && enteredPasskeyLength_ > 0) {
            enteredPasskey_[--enteredPasskeyLength_] = '\0';
            displayPasskeyEntry();
        } else if (event.type == core::InputEventType::NamedKey &&
                   event.namedKey == core::NamedKey::Enter && enteredPasskeyLength_ == 6) {
            answerChallenge(true);
            return;
        }
    }
}

void Plan014DeviceHarness::displayStatus(const char* detail) {
    display_.clear(black);
    display_.drawText({8, 8}, "Validation 014", headingStyle);
    display_.drawText({8, 38}, detail, bodyStyle);
    display_.drawText({8, 58}, "Pairing values never use serial", bodyStyle);
}

void Plan014DeviceHarness::displayPairingChallenge(
    const connectivity::BluetoothPairingChallenge& challenge) {
    display_.clear(black);
    display_.drawText({8, 8}, "BLE pairing", headingStyle);
    char value[7] = {};
    if (challenge.type == connectivity::BluetoothPairingChallengeType::EnterPasskey) {
        display_.drawText({8, 38}, "Enter host passkey", bodyStyle);
        display_.drawText({8, 105}, "Enter=accept  Esc=reject", bodyStyle);
        displayPasskeyEntry();
        return;
    }
    if (challenge.value.has_value()) {
        std::snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(*challenge.value));
    }
    if (challenge.type == connectivity::BluetoothPairingChallengeType::DisplayPasskey) {
        display_.drawText({8, 38}, "Type this on the host", bodyStyle);
        display_.drawText({55, 62}, value, valueStyle);
    } else {
        display_.drawText({8, 38}, "Does the host match?", bodyStyle);
        display_.drawText({55, 62}, value, valueStyle);
        display_.drawText({8, 105}, "Enter=yes  Esc=no", bodyStyle);
    }
}

void Plan014DeviceHarness::displayPasskeyEntry() {
    char masked[7] = {};
    std::fill_n(masked, enteredPasskeyLength_, '*');
    display_.drawText({55, 62}, "      ", valueStyle);
    display_.drawText({55, 62}, masked, valueStyle);
}

void Plan014DeviceHarness::answerChallenge(bool accepted) {
    if (!activeChallenge_.has_value()) {
        return;
    }
    connectivity::BluetoothPairingResponse response;
    response.generation = activeChallenge_->generation;
    response.type = activeChallenge_->type;
    response.accepted = accepted;
    if (response.type == connectivity::BluetoothPairingChallengeType::EnterPasskey) {
        response.passkey.assign(enteredPasskey_.data(), enteredPasskeyLength_);
    }
    const auto result = bluetoothService_.respondToPairing(response);
    std::fill(response.passkey.begin(), response.passkey.end(), '\0');
    response.passkey.clear();
    enteredPasskey_.fill('\0');
    enteredPasskeyLength_ = 0;
    activeChallenge_.reset();
    Serial.printf("[VALIDATION 014] pairing response accepted=%s result=%u\n",
                  accepted ? "yes" : "no", static_cast<unsigned>(result));
}

void Plan014DeviceHarness::refreshBonds() {
    const auto result = bluetoothService_.bonds();
    if (result.status != connectivity::BluetoothBondListStatus::Success) {
        bonds_.clear();
        Serial.println("[VALIDATION 014] bond registry unavailable");
        return;
    }
    bonds_ = result.bonds;
    Serial.printf("[VALIDATION 014] bond count=%u indexes=1..%u\n",
                  static_cast<unsigned>(bonds_.size()), static_cast<unsigned>(bonds_.size()));
}

std::optional<std::size_t> Plan014DeviceHarness::parseBondIndex(const char* text) const {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (end == text || end == nullptr || *end != '\0' || value == 0 || value > bonds_.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(value - 1);
}

void Plan014DeviceHarness::saveReference(std::size_t index) {
    if (index >= bonds_.size()) {
        Serial.println("[VALIDATION 014] invalid bond index");
        return;
    }
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open_from_partition(validationPartition, validationNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, savedReferenceKey, &bonds_[index], sizeof(bonds_[index]));
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    Serial.printf("[VALIDATION 014] reference saved=%s; reboot before verify\n",
                  result == ESP_OK ? "yes" : "no");
}

void Plan014DeviceHarness::verifyReference() {
    connectivity::BluetoothBondReference saved;
    std::size_t size = sizeof(saved);
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open_from_partition(validationPartition, validationNamespace, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        result = nvs_get_blob(handle, savedReferenceKey, &saved, &size);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    const auto current = bluetoothService_.bonds();
    const bool stable =
        result == ESP_OK && size == sizeof(saved) &&
        current.status == connectivity::BluetoothBondListStatus::Success &&
        std::find(current.bonds.begin(), current.bonds.end(), saved) != current.bonds.end();
    saved.bytes.fill(0);
    Serial.printf("[VALIDATION 014] reference stable=%s\n", stable ? "yes" : "no");
}

void Plan014DeviceHarness::clearSavedReference() {
    nvs_handle_t handle = 0;
    esp_err_t result =
        nvs_open_from_partition(validationPartition, validationNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_erase_key(handle, savedReferenceKey);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            result = ESP_OK;
        }
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    Serial.printf("[VALIDATION 014] saved reference cleared=%s\n", result == ESP_OK ? "yes" : "no");
}

void Plan014DeviceHarness::checkStorage() {
    const auto state = storage_.refresh();
    if (state != core::FileStorageState::Ready) {
        Serial.printf("[VALIDATION 014] storage check unavailable state=%u\n",
                      static_cast<unsigned>(state));
        return;
    }
    const core::FileStorageBytes expected{'0', '1', '4'};
    const auto write = storage_.replace(storageProbePath, expected);
    const auto read = storage_.read(storageProbePath, expected.size());
    const auto remove = storage_.remove(storageProbePath);
    const bool passed = write == core::FileWriteStatus::Stored &&
                        read.status == core::FileReadStatus::Found && read.data == expected &&
                        remove == core::FileRemoveStatus::Removed;
    Serial.printf("[VALIDATION 014] storage check=%s\n", passed ? "pass" : "fail");
}

} // namespace cardputer_hub::validation
