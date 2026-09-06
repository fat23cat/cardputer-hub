#include "validation/plan_012_device_harness.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <esp_system.h>
#include <esp_timer.h>

namespace cardputer_hub::validation {
namespace {

constexpr const char* validationDeviceName = "Cardputer Hub 012 Test";
constexpr std::uint32_t cycleHoldMilliseconds = 200;
constexpr std::uint32_t cycleTransitionTimeoutMilliseconds = 10'000;
constexpr std::uint32_t heapSampleMilliseconds = 1'000;
constexpr std::uint32_t coexistenceReportMilliseconds = 60'000;
constexpr std::size_t maximumCycleCount = 1'000;
constexpr std::size_t maximumCoexistenceMinutes = 240;

std::uint32_t millis() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000); }

class NativeSerialConsole {
  public:
    int read() const {
        unsigned char value = 0;
        return ::read(STDIN_FILENO, &value, 1) == 1 ? value : -1;
    }

    void println() const { std::printf("\n"); }
    void println(const char* message) const { std::printf("%s\n", message); }

    void printf(const char* format, ...) const {
        std::va_list arguments;
        va_start(arguments, format);
        std::vprintf(format, arguments);
        va_end(arguments);
    }
};

constexpr NativeSerialConsole Serial;

bool deadlineReached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

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

bool parsePositiveCount(const char* input, std::size_t defaultValue, std::size_t maximum,
                        std::size_t& result) {
    while (*input == ' ') {
        ++input;
    }
    if (*input == '\0') {
        result = defaultValue;
        return true;
    }

    char* end = nullptr;
    const auto parsed = std::strtoul(input, &end, 10);
    while (end != nullptr && *end == ' ') {
        ++end;
    }
    if (end == input || end == nullptr || *end != '\0' || parsed == 0 || parsed > maximum) {
        return false;
    }
    result = static_cast<std::size_t>(parsed);
    return true;
}

void clearString(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

} // namespace

Plan012DeviceHarness::Plan012DeviceHarness(core::Logger& logger) noexcept
    : bluetoothService_(bluetoothAdapter_, logger), wifiService_(wifiAdapter_, logger) {}

void Plan012DeviceHarness::start() {
    if (started_) {
        return;
    }
    started_ = true;
    lastUpdate_ = millis();
    nextHeapSample_ = lastUpdate_ + heapSampleMilliseconds;
    lowestObservedHeap_ = esp_get_free_heap_size();
    previousBluetoothState_ = bluetoothService_.state();
    previousWifiState_ = wifiService_.state();

    Serial.println();
    Serial.println("[VALIDATION 012] local Wi-Fi and ESP-NimBLE harness active");
    Serial.printf("[VALIDATION 012] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
    Serial.println("[VALIDATION 012] type 'help' for commands");
    printStatus();
}

void Plan012DeviceHarness::update() {
    if (!started_) {
        return;
    }

    const auto now = millis();
    const auto elapsed = std::chrono::milliseconds(now - lastUpdate_);
    lastUpdate_ = now;
    bluetoothService_.update(elapsed);
    wifiService_.update(elapsed);

    observeStateChanges();
    sampleHeap(now);
    readSerial(now);
    updateBluetoothCycle(now);
    updateCoexistenceWatch(now);
}

void Plan012DeviceHarness::readSerial(std::uint32_t now) {
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
                Serial.println("[VALIDATION 012] input rejected: line too long");
            } else {
                input_[inputLength_] = '\0';
                handleLine(input_.data(), now);
            }
            std::fill(input_.begin(), input_.end(), '\0');
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

void Plan012DeviceHarness::handleLine(char* line, std::uint32_t now) {
    if (inputMode_ == InputMode::WifiSsid) {
        if (std::strcmp(line, "/cancel") == 0) {
            inputMode_ = InputMode::Command;
            clearString(pendingSsid_);
            Serial.println("[VALIDATION 012] Wi-Fi entry cancelled");
            return;
        }
        if (*line == '\0') {
            Serial.println("[VALIDATION 012] SSID must not be empty; enter SSID or '/cancel'");
            return;
        }
        pendingSsid_ = line;
        inputMode_ = InputMode::WifiPassphrase;
        Serial.println("[VALIDATION 012] enter passphrase, blank for open network, or '/cancel'");
        return;
    }

    if (inputMode_ == InputMode::WifiPassphrase) {
        if (std::strcmp(line, "/cancel") == 0) {
            inputMode_ = InputMode::Command;
            clearString(pendingSsid_);
            Serial.println("[VALIDATION 012] Wi-Fi entry cancelled");
            return;
        }
        connectivity::WifiNetworkConfig config{pendingSsid_, line};
        const auto result = wifiService_.connect(config);
        clearString(config.passphrase);
        clearString(config.ssid);
        clearString(pendingSsid_);
        inputMode_ = InputMode::Command;
        Serial.printf("[VALIDATION 012] Wi-Fi connect result=%s\n",
                      result == connectivity::WifiConnectResult::Started         ? "started"
                      : result == connectivity::WifiConnectResult::InvalidConfig ? "invalid-config"
                                                                                 : "adapter-error");
        return;
    }

    if (*line != '\0') {
        handleCommand(line, now);
    }
}

void Plan012DeviceHarness::handleCommand(const char* line, std::uint32_t now) {
    if (std::strcmp(line, "help") == 0) {
        printHelp();
        return;
    }
    if (std::strcmp(line, "status") == 0) {
        printStatus();
        return;
    }
    if (std::strcmp(line, "heap") == 0) {
        printHeap();
        return;
    }
    if (std::strcmp(line, "bt enable") == 0 || std::strcmp(line, "enable") == 0) {
        if (cyclePhase_ != CyclePhase::Idle) {
            Serial.println("[VALIDATION 012] stop the Bluetooth cycle first");
            return;
        }
        const auto result = bluetoothService_.enable({validationDeviceName});
        Serial.printf("[VALIDATION 012] Bluetooth enable result=%s\n",
                      result == connectivity::BluetoothEnableResult::Enabled ? "enabled"
                      : result == connectivity::BluetoothEnableResult::AlreadyEnabled
                          ? "already-enabled"
                          : "adapter-error");
        return;
    }
    if (std::strcmp(line, "bt disable") == 0 || std::strcmp(line, "disable") == 0) {
        if (cyclePhase_ != CyclePhase::Idle) {
            stopBluetoothCycle("cancelled");
        }
        if (coexistencePending_ || coexistenceActive_) {
            stopCoexistenceWatch("cancelled");
        }
        const auto result = bluetoothService_.disable();
        Serial.printf("[VALIDATION 012] Bluetooth disable result=%s\n",
                      result == connectivity::BluetoothDisableResult::Disabled ? "disabled"
                      : result == connectivity::BluetoothDisableResult::AlreadyDisabled
                          ? "already-disabled"
                          : "adapter-error");
        return;
    }
    if ((std::strncmp(line, "bt cycle", 8) == 0 && (line[8] == '\0' || line[8] == ' ')) ||
        (std::strncmp(line, "cycle", 5) == 0 && (line[5] == '\0' || line[5] == ' '))) {
        const char* countText = line[0] == 'b' ? line + 8 : line + 5;
        std::size_t count = 0;
        if (!parsePositiveCount(countText, 100, maximumCycleCount, count)) {
            Serial.println("[VALIDATION 012] cycle count must be between 1 and 1000");
            return;
        }
        startBluetoothCycle(count, now);
        return;
    }
    if (std::strcmp(line, "bt stop") == 0) {
        stopBluetoothCycle("cancelled");
        (void)bluetoothService_.disable();
        return;
    }
    if (std::strcmp(line, "wifi connect") == 0) {
        if (coexistencePending_ || coexistenceActive_) {
            Serial.println("[VALIDATION 012] stop the coexistence watch first");
            return;
        }
        inputMode_ = InputMode::WifiSsid;
        Serial.println("[VALIDATION 012] enter SSID or '/cancel' (value will not be printed)");
        return;
    }
    if (std::strcmp(line, "wifi disconnect") == 0) {
        if (coexistencePending_ || coexistenceActive_) {
            stopCoexistenceWatch("cancelled");
        }
        const auto result = wifiService_.disconnect();
        Serial.printf("[VALIDATION 012] Wi-Fi disconnect result=%s\n",
                      result == connectivity::WifiDisconnectResult::Disconnected ? "disconnected"
                                                                                 : "adapter-error");
        return;
    }
    if (std::strcmp(line, "coexist stop") == 0) {
        stopCoexistenceWatch("cancelled");
        return;
    }
    if (std::strncmp(line, "coexist", 7) == 0 && (line[7] == '\0' || line[7] == ' ')) {
        std::size_t minutes = 0;
        if (!parsePositiveCount(line + 7, 60, maximumCoexistenceMinutes, minutes)) {
            Serial.println("[VALIDATION 012] coexist minutes must be between 1 and 240");
            return;
        }
        startCoexistenceWatch(minutes, now);
        return;
    }

    Serial.println("[VALIDATION 012] unknown command; type 'help'");
}

void Plan012DeviceHarness::printHelp() const {
    Serial.println("[VALIDATION 012] commands:");
    Serial.println("  status                 show Wi-Fi, Bluetooth, cycle, and heap state");
    Serial.println("  heap                   show current and minimum free heap");
    Serial.println("  bt enable | enable     enable BLE and advertise");
    Serial.println("  bt disable | disable   disable BLE");
    Serial.println("  bt cycle [count]       run enable/advertise/disable cycles (default 100)");
    Serial.println("  bt stop                cancel an active cycle and disable BLE");
    Serial.println("  wifi connect           enter Wi-Fi credentials without logging them");
    Serial.println("  wifi disconnect        clear Wi-Fi connection intent");
    Serial.println("  coexist [minutes]      watch connected Wi-Fi plus BLE advertising");
    Serial.println("  coexist stop           stop the coexistence watch");
}

void Plan012DeviceHarness::printStatus() const {
    Serial.printf("[VALIDATION 012] status wifi=%s bluetooth=%s cycle=%u/%u free_heap=%u "
                  "minimum_free_heap=%u\n",
                  wifiStateName(wifiService_.state()),
                  bluetoothStateName(bluetoothService_.state()),
                  static_cast<unsigned>(cycleCompleted_), static_cast<unsigned>(cycleTarget_),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(esp_get_minimum_free_heap_size()));
    const auto rssi = wifiService_.signalStrengthDbm();
    if (rssi.has_value()) {
        Serial.printf("[VALIDATION 012] Wi-Fi RSSI=%ld dBm\n", static_cast<long>(*rssi));
    }
}

void Plan012DeviceHarness::printHeap() const {
    Serial.printf("[VALIDATION 012] heap current=%u observed_low=%u boot_minimum=%u\n",
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(lowestObservedHeap_),
                  static_cast<unsigned>(esp_get_minimum_free_heap_size()));
}

void Plan012DeviceHarness::observeStateChanges() {
    const auto bluetoothState = bluetoothService_.state();
    if (bluetoothState != previousBluetoothState_) {
        if (coexistenceActive_ &&
            previousBluetoothState_ == connectivity::BluetoothState::Advertising) {
            ++coexistenceBluetoothInterruptions_;
        }
        Serial.printf("[VALIDATION 012] Bluetooth state=%s\n", bluetoothStateName(bluetoothState));
        previousBluetoothState_ = bluetoothState;
    }

    const auto wifiState = wifiService_.state();
    if (wifiState != previousWifiState_) {
        if (coexistenceActive_ && previousWifiState_ == connectivity::WifiState::Connected) {
            ++coexistenceWifiInterruptions_;
        }
        Serial.printf("[VALIDATION 012] Wi-Fi state=%s\n", wifiStateName(wifiState));
        previousWifiState_ = wifiState;
    }
}

void Plan012DeviceHarness::sampleHeap(std::uint32_t now) {
    if (!deadlineReached(now, nextHeapSample_)) {
        return;
    }
    const auto freeHeap = esp_get_free_heap_size();
    lowestObservedHeap_ = std::min(lowestObservedHeap_, freeHeap);
    nextHeapSample_ = now + heapSampleMilliseconds;
}

void Plan012DeviceHarness::startBluetoothCycle(std::size_t count, std::uint32_t now) {
    if (coexistencePending_ || coexistenceActive_) {
        Serial.println("[VALIDATION 012] stop the coexistence watch first");
        return;
    }
    if (cyclePhase_ != CyclePhase::Idle) {
        Serial.println("[VALIDATION 012] a Bluetooth cycle is already active");
        return;
    }
    const auto disableResult = bluetoothService_.disable();
    if (disableResult == connectivity::BluetoothDisableResult::AdapterError) {
        Serial.println("[VALIDATION 012] CYCLE FAIL: initial disable failed");
        return;
    }
    cycleTarget_ = count;
    cycleCompleted_ = 0;
    cycleInitialHeap_ = esp_get_free_heap_size();
    cyclePhase_ = CyclePhase::DisabledHold;
    cycleDeadline_ = now + cycleHoldMilliseconds;
    Serial.printf("[VALIDATION 012] cycle started target=%u\n", static_cast<unsigned>(count));
}

void Plan012DeviceHarness::startNextBluetoothCycle(std::uint32_t now) {
    const auto result = bluetoothService_.enable({validationDeviceName});
    if (result != connectivity::BluetoothEnableResult::Enabled) {
        stopBluetoothCycle("FAIL: enable failed");
        return;
    }
    cyclePhase_ = CyclePhase::WaitingForAdvertising;
    cycleDeadline_ = now + cycleTransitionTimeoutMilliseconds;
}

void Plan012DeviceHarness::updateBluetoothCycle(std::uint32_t now) {
    switch (cyclePhase_) {
    case CyclePhase::Idle:
        return;
    case CyclePhase::DisabledHold:
        if (!deadlineReached(now, cycleDeadline_)) {
            return;
        }
        if (cycleCompleted_ == cycleTarget_) {
            stopBluetoothCycle("PASS");
            return;
        }
        startNextBluetoothCycle(now);
        return;
    case CyclePhase::WaitingForAdvertising:
        if (bluetoothService_.state() == connectivity::BluetoothState::Error) {
            stopBluetoothCycle("FAIL: Bluetooth entered error");
            return;
        }
        if (bluetoothService_.state() == connectivity::BluetoothState::Advertising) {
            cyclePhase_ = CyclePhase::AdvertisingHold;
            cycleDeadline_ = now + cycleHoldMilliseconds;
            return;
        }
        if (deadlineReached(now, cycleDeadline_)) {
            stopBluetoothCycle("FAIL: advertising timeout");
        }
        return;
    case CyclePhase::AdvertisingHold:
        if (bluetoothService_.state() != connectivity::BluetoothState::Advertising) {
            stopBluetoothCycle("FAIL: advertising stopped unexpectedly");
            return;
        }
        if (!deadlineReached(now, cycleDeadline_)) {
            return;
        }
        if (bluetoothService_.disable() == connectivity::BluetoothDisableResult::AdapterError) {
            stopBluetoothCycle("FAIL: disable failed");
            return;
        }
        ++cycleCompleted_;
        if (cycleCompleted_ % 10 == 0 || cycleCompleted_ == cycleTarget_) {
            Serial.printf("[VALIDATION 012] cycle progress=%u/%u free_heap=%u\n",
                          static_cast<unsigned>(cycleCompleted_),
                          static_cast<unsigned>(cycleTarget_),
                          static_cast<unsigned>(esp_get_free_heap_size()));
        }
        cyclePhase_ = CyclePhase::DisabledHold;
        cycleDeadline_ = now + cycleHoldMilliseconds;
        return;
    }
}

void Plan012DeviceHarness::stopBluetoothCycle(const char* outcome) {
    if (cyclePhase_ == CyclePhase::Idle) {
        Serial.println("[VALIDATION 012] no Bluetooth cycle is active");
        return;
    }
    Serial.printf("[VALIDATION 012] CYCLE %s completed=%u/%u initial_heap=%u final_heap=%u "
                  "boot_minimum=%u\n",
                  outcome, static_cast<unsigned>(cycleCompleted_),
                  static_cast<unsigned>(cycleTarget_), static_cast<unsigned>(cycleInitialHeap_),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(esp_get_minimum_free_heap_size()));
    cyclePhase_ = CyclePhase::Idle;
}

void Plan012DeviceHarness::startCoexistenceWatch(std::size_t minutes, std::uint32_t now) {
    if (cyclePhase_ != CyclePhase::Idle) {
        Serial.println("[VALIDATION 012] stop the Bluetooth cycle first");
        return;
    }
    if (coexistencePending_ || coexistenceActive_) {
        Serial.println("[VALIDATION 012] a coexistence watch is already active");
        return;
    }
    if (wifiService_.state() != connectivity::WifiState::Connected) {
        Serial.println("[VALIDATION 012] connect Wi-Fi before starting coexistence watch");
        return;
    }
    if (bluetoothService_.state() == connectivity::BluetoothState::Disabled) {
        if (bluetoothService_.enable({validationDeviceName}) !=
            connectivity::BluetoothEnableResult::Enabled) {
            Serial.println("[VALIDATION 012] COEXIST FAIL: Bluetooth enable failed");
            return;
        }
    } else if (bluetoothService_.state() == connectivity::BluetoothState::Error) {
        Serial.println("[VALIDATION 012] recover Bluetooth before starting coexistence watch");
        return;
    }
    coexistencePending_ = true;
    coexistenceMinutes_ = minutes;
    coexistenceDeadline_ = now + cycleTransitionTimeoutMilliseconds;
    coexistenceWifiInterruptions_ = 0;
    coexistenceBluetoothInterruptions_ = 0;
    Serial.println("[VALIDATION 012] coexistence watch waiting for BLE advertising");
}

void Plan012DeviceHarness::updateCoexistenceWatch(std::uint32_t now) {
    if (coexistencePending_) {
        if (wifiService_.state() != connectivity::WifiState::Connected ||
            bluetoothService_.state() == connectivity::BluetoothState::Error) {
            stopCoexistenceWatch("FAIL: service state changed before start");
            return;
        }
        if (bluetoothService_.state() == connectivity::BluetoothState::Advertising) {
            coexistencePending_ = false;
            coexistenceActive_ = true;
            coexistenceDeadline_ = now + static_cast<std::uint32_t>(coexistenceMinutes_) * 60'000;
            coexistenceNextReport_ = now + coexistenceReportMilliseconds;
            Serial.printf("[VALIDATION 012] coexistence watch started minutes=%u\n",
                          static_cast<unsigned>(coexistenceMinutes_));
            return;
        }
        if (deadlineReached(now, coexistenceDeadline_)) {
            stopCoexistenceWatch("FAIL: advertising timeout");
        }
        return;
    }
    if (!coexistenceActive_) {
        return;
    }
    if (wifiService_.state() == connectivity::WifiState::Error ||
        bluetoothService_.state() == connectivity::BluetoothState::Error) {
        stopCoexistenceWatch("FAIL: service entered error");
        return;
    }
    if (deadlineReached(now, coexistenceDeadline_)) {
        const bool uninterrupted =
            coexistenceWifiInterruptions_ == 0 && coexistenceBluetoothInterruptions_ == 0 &&
            wifiService_.state() == connectivity::WifiState::Connected &&
            bluetoothService_.state() == connectivity::BluetoothState::Advertising;
        stopCoexistenceWatch(uninterrupted ? "PASS" : "DONE WITH INTERRUPTIONS");
        return;
    }
    if (deadlineReached(now, coexistenceNextReport_)) {
        Serial.printf("[VALIDATION 012] coexist progress wifi_interruptions=%u "
                      "bluetooth_interruptions=%u free_heap=%u\n",
                      static_cast<unsigned>(coexistenceWifiInterruptions_),
                      static_cast<unsigned>(coexistenceBluetoothInterruptions_),
                      static_cast<unsigned>(esp_get_free_heap_size()));
        coexistenceNextReport_ = now + coexistenceReportMilliseconds;
    }
}

void Plan012DeviceHarness::stopCoexistenceWatch(const char* outcome) {
    if (!coexistencePending_ && !coexistenceActive_) {
        Serial.println("[VALIDATION 012] no coexistence watch is active");
        return;
    }
    Serial.printf("[VALIDATION 012] COEXIST %s wifi_interruptions=%u "
                  "bluetooth_interruptions=%u free_heap=%u boot_minimum=%u\n",
                  outcome, static_cast<unsigned>(coexistenceWifiInterruptions_),
                  static_cast<unsigned>(coexistenceBluetoothInterruptions_),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(esp_get_minimum_free_heap_size()));
    coexistencePending_ = false;
    coexistenceActive_ = false;
}

} // namespace cardputer_hub::validation
