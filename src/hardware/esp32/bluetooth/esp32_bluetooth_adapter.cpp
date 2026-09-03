#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_err.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_defs.h>
#include <esp_gatts_api.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace cardputer_hub::hardware {
namespace {

#if defined(CORE_DEBUG_LEVEL)
constexpr int configuredFrameworkDebugLevel = CORE_DEBUG_LEVEL;
#else
constexpr int configuredFrameworkDebugLevel = 4;
#endif
static_assert(configuredFrameworkDebugLevel < 4,
              "ESP32 framework debug logging can expose Bluetooth peer identity");

constexpr std::size_t eventQueueCapacity = 16;
constexpr std::size_t peerCapacity = 4;
constexpr std::size_t bondCapacity = 16;
constexpr std::size_t maximumBluetoothDeviceNameLength = 248;
constexpr std::uint16_t applicationId = 0x011;

enum class RawEventType : std::uint8_t {
    GattsRegistered,
    AdvertisingDataConfigured,
    AdvertisingStarted,
    AdvertisingStopped,
    PeerConnected,
    PeerDisconnected,
};

struct RawEvent {
    RawEventType type = RawEventType::GattsRegistered;
    std::uint32_t lifecycle = 0;
    std::uint16_t status = 0;
    std::uint16_t connectionId = 0;
    std::uint8_t gattsInterface = ESP_GATT_IF_NONE;
    std::array<std::uint8_t, ESP_BD_ADDR_LEN> address{};
};

struct PeerRecord {
    bool used = false;
    connectivity::BluetoothPeerHandle handle{};
    std::uint16_t connectionId = 0;
    std::array<std::uint8_t, ESP_BD_ADDR_LEN> address{};
};

struct AdapterContext {
    Esp32BluetoothAdapter* owner = nullptr;
    portMUX_TYPE queueMutex = portMUX_INITIALIZER_UNLOCKED;
    std::array<RawEvent, eventQueueCapacity> events{};
    std::size_t eventHead = 0;
    std::size_t eventTail = 0;
    std::size_t eventCount = 0;
    bool queueOverflow = false;
    bool controllerInitialized = false;
    bool controllerEnabled = false;
    bool bluedroidInitialized = false;
    bool bluedroidEnabled = false;
    bool stackOwned = false;
    bool advertisingDataReady = false;
    bool advertisingRequested = false;
    bool advertisingLaunchIssued = false;
    bool advertisingStopPending = false;
    bool advertisingActive = false;
    std::uint32_t lifecycle = 0;
    std::uint32_t advertisingLaunchLifecycle = 0;
    std::uint32_t advertisingStopLifecycle = 0;
    std::uint32_t nextPeerHandle = 1;
    esp_gatt_if_t gattsInterface = ESP_GATT_IF_NONE;
    std::string deviceName;
    std::array<PeerRecord, peerCapacity> peers{};
};

AdapterContext context;

esp_ble_adv_params_t advertisingParameters{
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

esp_ble_adv_data_t advertisingData{
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0,
    .max_interval = 0,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 0,
    .p_service_uuid = nullptr,
    .flag = static_cast<std::uint8_t>(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

void enqueue(const RawEvent& event) {
    portENTER_CRITICAL(&context.queueMutex);
    if (context.eventCount == context.events.size()) {
        context.queueOverflow = true;
    } else {
        context.events[context.eventTail] = event;
        context.eventTail = (context.eventTail + 1) % context.events.size();
        ++context.eventCount;
    }
    portEXIT_CRITICAL(&context.queueMutex);
}

void latchQueueOverflow() {
    portENTER_CRITICAL(&context.queueMutex);
    context.queueOverflow = true;
    portEXIT_CRITICAL(&context.queueMutex);
}

std::uint32_t currentLifecycle() {
    portENTER_CRITICAL(&context.queueMutex);
    const auto lifecycle = context.lifecycle;
    portEXIT_CRITICAL(&context.queueMutex);
    return lifecycle;
}

void setLifecycle(std::uint32_t lifecycle) {
    portENTER_CRITICAL(&context.queueMutex);
    context.lifecycle = lifecycle;
    portEXIT_CRITICAL(&context.queueMutex);
}

Esp32BluetoothAdapter* currentOwner() {
    portENTER_CRITICAL(&context.queueMutex);
    auto* owner = context.owner;
    portEXIT_CRITICAL(&context.queueMutex);
    return owner;
}

void setOwner(Esp32BluetoothAdapter* owner) {
    portENTER_CRITICAL(&context.queueMutex);
    context.owner = owner;
    portEXIT_CRITICAL(&context.queueMutex);
}

bool snapshotCallbackLifecycle(std::uint32_t AdapterContext::* source, std::uint32_t& lifecycle) {
    portENTER_CRITICAL(&context.queueMutex);
    if (context.owner == nullptr) {
        portEXIT_CRITICAL(&context.queueMutex);
        return false;
    }
    lifecycle = context.*source;
    portEXIT_CRITICAL(&context.queueMutex);
    return true;
}

void recordAdvertisingLaunch(std::uint32_t lifecycle) {
    portENTER_CRITICAL(&context.queueMutex);
    context.advertisingLaunchIssued = true;
    context.advertisingLaunchLifecycle = lifecycle;
    portEXIT_CRITICAL(&context.queueMutex);
}

void clearAdvertisingLaunch() {
    portENTER_CRITICAL(&context.queueMutex);
    context.advertisingLaunchIssued = false;
    context.advertisingLaunchLifecycle = 0;
    portEXIT_CRITICAL(&context.queueMutex);
}

void recordAdvertisingStop(std::uint32_t lifecycle) {
    portENTER_CRITICAL(&context.queueMutex);
    context.advertisingStopPending = true;
    context.advertisingStopLifecycle = lifecycle;
    portEXIT_CRITICAL(&context.queueMutex);
}

void clearAdvertisingStop() {
    portENTER_CRITICAL(&context.queueMutex);
    context.advertisingStopPending = false;
    context.advertisingStopLifecycle = 0;
    portEXIT_CRITICAL(&context.queueMutex);
}

bool takeOverflow() {
    portENTER_CRITICAL(&context.queueMutex);
    const bool overflow = context.queueOverflow;
    context.queueOverflow = false;
    portEXIT_CRITICAL(&context.queueMutex);
    return overflow;
}

bool dequeue(RawEvent& event) {
    portENTER_CRITICAL(&context.queueMutex);
    if (context.eventCount == 0) {
        portEXIT_CRITICAL(&context.queueMutex);
        return false;
    }
    event = context.events[context.eventHead];
    context.eventHead = (context.eventHead + 1) % context.events.size();
    --context.eventCount;
    portEXIT_CRITICAL(&context.queueMutex);
    return true;
}

void gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* parameter) {
    if (parameter == nullptr) {
        if (currentOwner() != nullptr) {
            latchQueueOverflow();
        }
        return;
    }
    std::uint32_t lifecycle = 0;
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        if (!snapshotCallbackLifecycle(&AdapterContext::lifecycle, lifecycle)) {
            return;
        }
        enqueue({RawEventType::AdvertisingDataConfigured, lifecycle,
                 static_cast<std::uint16_t>(parameter->adv_data_cmpl.status)});
    } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        if (!snapshotCallbackLifecycle(&AdapterContext::advertisingLaunchLifecycle, lifecycle)) {
            return;
        }
        enqueue({RawEventType::AdvertisingStarted, lifecycle,
                 static_cast<std::uint16_t>(parameter->adv_start_cmpl.status)});
    } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
        if (!snapshotCallbackLifecycle(&AdapterContext::advertisingStopLifecycle, lifecycle)) {
            return;
        }
        enqueue({RawEventType::AdvertisingStopped, lifecycle,
                 static_cast<std::uint16_t>(parameter->adv_stop_cmpl.status)});
    }
}

void gattsCallback(esp_gatts_cb_event_t event, esp_gatt_if_t gattsInterface,
                   esp_ble_gatts_cb_param_t* parameter) {
    if (parameter == nullptr) {
        if (currentOwner() != nullptr) {
            latchQueueOverflow();
        }
        return;
    }

    RawEvent queued{};
    if (!snapshotCallbackLifecycle(&AdapterContext::lifecycle, queued.lifecycle)) {
        return;
    }
    queued.gattsInterface = gattsInterface;
    if (event == ESP_GATTS_REG_EVT) {
        queued.type = RawEventType::GattsRegistered;
        queued.status = static_cast<std::uint16_t>(parameter->reg.status);
        enqueue(queued);
    } else if (event == ESP_GATTS_CONNECT_EVT) {
        queued.type = RawEventType::PeerConnected;
        queued.connectionId = parameter->connect.conn_id;
        std::copy_n(parameter->connect.remote_bda, queued.address.size(), queued.address.begin());
        enqueue(queued);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        queued.type = RawEventType::PeerDisconnected;
        queued.connectionId = parameter->disconnect.conn_id;
        enqueue(queued);
    }
}

void clearRuntimeState() {
    portENTER_CRITICAL(&context.queueMutex);
    context.eventHead = 0;
    context.eventTail = 0;
    context.eventCount = 0;
    context.queueOverflow = false;
    context.lifecycle = 0;
    context.advertisingLaunchIssued = false;
    context.advertisingStopPending = false;
    context.advertisingLaunchLifecycle = 0;
    context.advertisingStopLifecycle = 0;
    portEXIT_CRITICAL(&context.queueMutex);
    context.advertisingDataReady = false;
    context.advertisingRequested = false;
    context.advertisingActive = false;
    context.gattsInterface = ESP_GATT_IF_NONE;
    context.deviceName.clear();
    context.peers = {};
}

bool quiesceStack() {
    if (context.bluedroidEnabled) {
        if (esp_bluedroid_disable() != ESP_OK) {
            return false;
        }
        context.bluedroidEnabled = false;
    }
    if (context.bluedroidInitialized) {
        if (esp_bluedroid_deinit() != ESP_OK) {
            return false;
        }
        context.bluedroidInitialized = false;
    }
    if (context.controllerEnabled) {
        if (esp_bt_controller_disable() != ESP_OK) {
            return false;
        }
        context.controllerEnabled = false;
    }
    context.stackOwned = false;
    clearRuntimeState();
    return true;
}

constexpr bool isRetryableAdvertisingDispatchError(esp_err_t error) { return error == ESP_FAIL; }

static_assert(isRetryableAdvertisingDispatchError(ESP_FAIL));
static_assert(!isRetryableAdvertisingDispatchError(ESP_ERR_INVALID_ARG));
static_assert(!isRetryableAdvertisingDispatchError(ESP_ERR_INVALID_STATE));

connectivity::BluetoothAdvertisingResult issueAdvertisingStart() {
    const auto lifecycle = currentLifecycle();
    recordAdvertisingLaunch(lifecycle);
    const auto result = esp_ble_gap_start_advertising(&advertisingParameters);
    if (result != ESP_OK) {
        context.advertisingRequested = false;
        clearAdvertisingLaunch();
        return isRetryableAdvertisingDispatchError(result)
                   ? connectivity::BluetoothAdvertisingResult::RetryableFailure
                   : connectivity::BluetoothAdvertisingResult::AdapterError;
    }
    return connectivity::BluetoothAdvertisingResult::Started;
}

constexpr bool isRetryableAdvertisingStatus(std::uint16_t status) {
    switch (static_cast<esp_bt_status_t>(status)) {
    case ESP_BT_STATUS_NOT_READY:
    case ESP_BT_STATUS_NOMEM:
    case ESP_BT_STATUS_BUSY:
    case ESP_BT_STATUS_PENDING:
    case ESP_BT_STATUS_TIMEOUT:
    case ESP_BT_STATUS_MEMORY_FULL:
        return true;
    default:
        return false;
    }
}

static_assert(isRetryableAdvertisingStatus(ESP_BT_STATUS_BUSY));
static_assert(isRetryableAdvertisingStatus(ESP_BT_STATUS_TIMEOUT));
static_assert(!isRetryableAdvertisingStatus(ESP_BT_STATUS_PARM_INVALID));
static_assert(!isRetryableAdvertisingStatus(ESP_BT_STATUS_UNSUPPORTED));

PeerRecord* findPeer(connectivity::BluetoothPeerHandle handle) {
    const auto peer =
        std::find_if(context.peers.begin(), context.peers.end(), [handle](const auto& candidate) {
            return candidate.used && candidate.handle == handle;
        });
    return peer == context.peers.end() ? nullptr : &*peer;
}

PeerRecord* findPeer(std::uint16_t connectionId) {
    const auto peer = std::find_if(
        context.peers.begin(), context.peers.end(), [connectionId](const auto& candidate) {
            return candidate.used && candidate.connectionId == connectionId;
        });
    return peer == context.peers.end() ? nullptr : &*peer;
}

PeerRecord* addPeer(const RawEvent& event) {
    const auto peer = std::find_if(context.peers.begin(), context.peers.end(),
                                   [](const auto& candidate) { return !candidate.used; });
    if (peer == context.peers.end()) {
        return nullptr;
    }
    peer->used = true;
    peer->handle = {context.nextPeerHandle++};
    if (context.nextPeerHandle == 0) {
        context.nextPeerHandle = 1;
    }
    peer->connectionId = event.connectionId;
    peer->address = event.address;
    return &*peer;
}

connectivity::BluetoothPollResult adapterFailure(std::uint32_t lifecycle) {
    return connectivity::BluetoothPollResult::withEvent(
        {connectivity::BluetoothEventType::AdapterFailed,
         {},
         connectivity::BluetoothFailureClass::Fatal,
         lifecycle});
}

void suppressIdentityBearingBluetoothLogTags() {
    constexpr const char* tags[] = {"BT",      "BT_HCI",   "BT_BTM",  "BT_SMP",      "BT_GATT",
                                    "BT_APPL", "BT_L2CAP", "BT_BTIF", "BTC_GAP_BLE", "GATTS"};
    for (const auto* tag : tags) {
        esp_log_level_set(tag, ESP_LOG_NONE);
    }
}

} // namespace

Esp32BluetoothAdapter::~Esp32BluetoothAdapter() {
    if (currentOwner() == this) {
        if (quiesceStack()) {
            setOwner(nullptr);
        }
    }
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::initialize(const connectivity::BluetoothDeviceConfig& config,
                                  std::uint32_t lifecycle) {
    if (lifecycle == 0 || config.deviceName.empty() ||
        config.deviceName.size() > maximumBluetoothDeviceNameLength ||
        config.deviceName.find('\0') != std::string::npos) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }

    const auto* owner = currentOwner();
    if (owner != nullptr && owner != this) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    if (owner == nullptr) {
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
            esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        setOwner(this);
    } else {
        const auto expectedControllerStatus =
            context.controllerEnabled
                ? ESP_BT_CONTROLLER_STATUS_ENABLED
                : (context.controllerInitialized ? ESP_BT_CONTROLLER_STATUS_INITED
                                                 : ESP_BT_CONTROLLER_STATUS_IDLE);
        if (context.stackOwned || context.bluedroidEnabled || context.bluedroidInitialized ||
            esp_bt_controller_get_status() != expectedControllerStatus ||
            esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
    }

    clearRuntimeState();
    setLifecycle(lifecycle);
    context.deviceName = config.deviceName;
    suppressIdentityBearingBluetoothLogTags();
    if (!context.controllerInitialized) {
        esp_bt_controller_config_t controllerConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&controllerConfig) != ESP_OK) {
            clearRuntimeState();
            setOwner(nullptr);
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        context.controllerInitialized = true;
    }
    if (!context.controllerEnabled) {
        if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
            (void)quiesceStack();
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        context.controllerEnabled = true;
    }
    if (esp_bluedroid_init() != ESP_OK) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.bluedroidInitialized = true;
    if (esp_bluedroid_enable() != ESP_OK) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.bluedroidEnabled = true;
    if (esp_ble_gap_register_callback(gapCallback) != ESP_OK ||
        esp_ble_gatts_register_callback(gattsCallback) != ESP_OK ||
        esp_ble_gatts_app_register(applicationId) != ESP_OK) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.stackOwned = true;
    return connectivity::BluetoothAdapterResult::Success;
}

connectivity::BluetoothAdapterResult Esp32BluetoothAdapter::shutdown() {
    const auto* owner = currentOwner();
    if (owner == nullptr) {
        return connectivity::BluetoothAdapterResult::Success;
    }
    if (owner != this) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return quiesceStack() ? connectivity::BluetoothAdapterResult::Success
                          : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothAdvertisingResult
Esp32BluetoothAdapter::startAdvertising(std::uint32_t lifecycle) {
    if (currentOwner() != this || !context.stackOwned || lifecycle == 0 ||
        context.advertisingRequested || context.advertisingLaunchIssued ||
        context.advertisingStopPending || context.advertisingActive) {
        return connectivity::BluetoothAdvertisingResult::AdapterError;
    }
    setLifecycle(lifecycle);
    context.advertisingRequested = true;
    if (!context.advertisingDataReady) {
        return connectivity::BluetoothAdvertisingResult::Started;
    }
    return issueAdvertisingStart();
}

connectivity::BluetoothAdapterResult Esp32BluetoothAdapter::requestAdvertisingStop() {
    if (currentOwner() != this || !context.stackOwned) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.advertisingRequested = false;
    if (context.advertisingStopPending) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    if (!context.advertisingLaunchIssued && !context.advertisingActive) {
        return connectivity::BluetoothAdapterResult::Success;
    }
    recordAdvertisingStop(currentLifecycle());
    if (esp_ble_gap_stop_advertising() != ESP_OK) {
        clearAdvertisingStop();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return connectivity::BluetoothAdapterResult::Success;
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::disconnectPeer(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr ||
        context.gattsInterface == ESP_GATT_IF_NONE) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return esp_ble_gatts_close(context.gattsInterface, peer->connectionId) == ESP_OK
               ? connectivity::BluetoothAdapterResult::Success
               : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothPollResult Esp32BluetoothAdapter::pollEvent() {
    if (currentOwner() != this || !context.stackOwned || takeOverflow()) {
        return connectivity::BluetoothPollResult::adapterError();
    }

    RawEvent event{};
    while (dequeue(event)) {
        if (event.lifecycle != currentLifecycle()) {
            continue;
        }
        switch (event.type) {
        case RawEventType::GattsRegistered:
            if (event.status != ESP_GATT_OK) {
                return adapterFailure(currentLifecycle());
            }
            context.gattsInterface = event.gattsInterface;
            if (esp_ble_gap_set_device_name(context.deviceName.c_str()) != ESP_OK ||
                esp_ble_gap_config_adv_data(&advertisingData) != ESP_OK) {
                return connectivity::BluetoothPollResult::adapterError();
            }
            break;
        case RawEventType::AdvertisingDataConfigured:
            if (event.status != ESP_BT_STATUS_SUCCESS) {
                return adapterFailure(currentLifecycle());
            }
            context.advertisingDataReady = true;
            if (context.advertisingRequested && !context.advertisingLaunchIssued &&
                !context.advertisingActive) {
                const auto launchResult = issueAdvertisingStart();
                if (launchResult == connectivity::BluetoothAdvertisingResult::RetryableFailure) {
                    return connectivity::BluetoothPollResult::withEvent(
                        {connectivity::BluetoothEventType::AdvertisingFailed,
                         {},
                         connectivity::BluetoothFailureClass::Retryable,
                         event.lifecycle});
                }
                if (launchResult == connectivity::BluetoothAdvertisingResult::AdapterError) {
                    return connectivity::BluetoothPollResult::adapterError();
                }
            }
            break;
        case RawEventType::AdvertisingStarted:
            clearAdvertisingLaunch();
            if (event.status == ESP_BT_STATUS_SUCCESS) {
                context.advertisingActive = true;
                return connectivity::BluetoothPollResult::withEvent(
                    {connectivity::BluetoothEventType::AdvertisingStarted,
                     {},
                     connectivity::BluetoothFailureClass::Fatal,
                     event.lifecycle});
            }
            context.advertisingRequested = false;
            context.advertisingActive = false;
            return connectivity::BluetoothPollResult::withEvent({
                connectivity::BluetoothEventType::AdvertisingFailed,
                {},
                isRetryableAdvertisingStatus(event.status)
                    ? connectivity::BluetoothFailureClass::Retryable
                    : connectivity::BluetoothFailureClass::Fatal,
                event.lifecycle,
            });
        case RawEventType::AdvertisingStopped:
            clearAdvertisingStop();
            if (event.status != ESP_BT_STATUS_SUCCESS) {
                return adapterFailure(event.lifecycle);
            }
            clearAdvertisingLaunch();
            context.advertisingActive = false;
            break;
        case RawEventType::PeerConnected: {
            context.advertisingRequested = false;
            context.advertisingLaunchIssued = false;
            context.advertisingActive = false;
            const auto* peer = addPeer(event);
            if (peer == nullptr) {
                return connectivity::BluetoothPollResult::adapterError();
            }
            return connectivity::BluetoothPollResult::withEvent(
                {connectivity::BluetoothEventType::PeerConnected, peer->handle,
                 connectivity::BluetoothFailureClass::Fatal, event.lifecycle});
        }
        case RawEventType::PeerDisconnected: {
            auto* peer = findPeer(event.connectionId);
            if (peer == nullptr) {
                break;
            }
            const auto handle = peer->handle;
            *peer = {};
            return connectivity::BluetoothPollResult::withEvent(
                {connectivity::BluetoothEventType::PeerDisconnected, handle,
                 connectivity::BluetoothFailureClass::Fatal, event.lifecycle});
        }
        }
    }
    return connectivity::BluetoothPollResult::noEvent();
}

connectivity::BluetoothBondQueryResult
Esp32BluetoothAdapter::bondState(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr) {
        return connectivity::BluetoothBondQueryResult::AdapterError;
    }
    int bondCount = esp_ble_get_bond_device_num();
    if (bondCount < 0 || static_cast<std::size_t>(bondCount) > bondCapacity) {
        return connectivity::BluetoothBondQueryResult::AdapterError;
    }
    if (bondCount == 0) {
        return connectivity::BluetoothBondQueryResult::Unbonded;
    }
    std::array<esp_ble_bond_dev_t, bondCapacity> bonds{};
    if (esp_ble_get_bond_device_list(&bondCount, bonds.data()) != ESP_OK) {
        return connectivity::BluetoothBondQueryResult::AdapterError;
    }
    for (int index = 0; index < bondCount; ++index) {
        if (std::memcmp(bonds[static_cast<std::size_t>(index)].bd_addr, peer->address.data(),
                        peer->address.size()) == 0) {
            return connectivity::BluetoothBondQueryResult::Bonded;
        }
    }
    return connectivity::BluetoothBondQueryResult::Unbonded;
}

} // namespace cardputer_hub::hardware
