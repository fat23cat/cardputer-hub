#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <esp_bt.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>

extern "C" void ble_store_config_init(void);

namespace cardputer_hub::hardware {
namespace {

#if defined(CORE_DEBUG_LEVEL)
constexpr int configuredFrameworkDebugLevel = CORE_DEBUG_LEVEL;
#else
constexpr int configuredFrameworkDebugLevel = 4;
#endif
static_assert(configuredFrameworkDebugLevel < 4,
              "ESP32 framework debug logging can expose Bluetooth peer identity");

#if defined(ESP_PLATFORM) && (!CONFIG_BT_NIMBLE_ENABLED || CONFIG_BT_BLUEDROID_ENABLED)
#error "Esp32BluetoothAdapter requires ESP-NimBLE with Bluedroid disabled"
#endif
#if defined(ESP_PLATFORM) && (CONFIG_BT_NIMBLE_ROLE_CENTRAL || CONFIG_BT_NIMBLE_ROLE_OBSERVER)
#error "Esp32BluetoothAdapter must not enable central or observer roles"
#endif
#if defined(ESP_PLATFORM) &&                                                                       \
    (!CONFIG_BT_NIMBLE_ROLE_PERIPHERAL || !CONFIG_BT_NIMBLE_ROLE_BROADCASTER)
#error "Esp32BluetoothAdapter requires peripheral and broadcaster roles"
#endif
#if defined(ESP_PLATFORM) && CONFIG_BT_NIMBLE_MAX_CONNECTIONS != 1
#error "Esp32BluetoothAdapter supports exactly one controller connection"
#endif

constexpr std::size_t eventQueueCapacity = 16;
constexpr std::size_t peerCapacity = 1;
constexpr std::size_t bondCapacity = 16;
constexpr std::size_t maximumBluetoothDeviceNameLength = 248;
constexpr std::size_t maximumLegacyAdvertisingNameLength = 26;
constexpr TickType_t hostStopTimeout = pdMS_TO_TICKS(5'000);
constexpr std::uint16_t invalidConnectionHandle = BLE_HS_CONN_HANDLE_NONE;

enum class RawEventType : std::uint8_t {
    HostSynchronized,
    HostFailed,
    AdvertisingStarted,
    AdvertisingCompleted,
    PeerConnected,
    PeerDisconnected,
};

struct RawEvent {
    RawEventType type = RawEventType::HostFailed;
    std::uint32_t lifecycle = 0;
    int status = 0;
    std::uint16_t connectionHandle = invalidConnectionHandle;
    ble_addr_t identityAddress{};
};

struct PeerRecord {
    bool used = false;
    connectivity::BluetoothPeerHandle handle{};
    std::uint16_t connectionHandle = invalidConnectionHandle;
    ble_addr_t identityAddress{};
};

struct AdapterContext {
    Esp32BluetoothAdapter* owner = nullptr;
    portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
    std::array<RawEvent, eventQueueCapacity> events{};
    std::size_t eventHead = 0;
    std::size_t eventTail = 0;
    std::size_t eventCount = 0;
    bool queueOverflow = false;
    bool controllerInitialized = false;
    bool controllerEnabled = false;
    bool hostInitialized = false;
    bool hostRunning = false;
    bool stackOwned = false;
    bool synchronized = false;
    bool advertisingRequested = false;
    bool advertisingActive = false;
    bool advertisingStopRequested = false;
    std::uint16_t activeConnectionHandle = invalidConnectionHandle;
    std::uint8_t ownAddressType = BLE_OWN_ADDR_PUBLIC;
    std::uint32_t lifecycle = 0;
    std::uint32_t nextPeerHandle = 1;
    std::string deviceName;
    std::array<PeerRecord, peerCapacity> peers{};
    StaticSemaphore_t hostStoppedStorage{};
    SemaphoreHandle_t hostStopped = nullptr;
};

AdapterContext context;

void enqueue(const RawEvent& event) {
    portENTER_CRITICAL(&context.mutex);
    if (context.eventCount == context.events.size()) {
        context.queueOverflow = true;
    } else {
        context.events[context.eventTail] = event;
        context.eventTail = (context.eventTail + 1) % context.events.size();
        ++context.eventCount;
    }
    portEXIT_CRITICAL(&context.mutex);
}

void latchQueueOverflow() {
    portENTER_CRITICAL(&context.mutex);
    context.queueOverflow = true;
    portEXIT_CRITICAL(&context.mutex);
}

std::uint32_t currentLifecycle() {
    portENTER_CRITICAL(&context.mutex);
    const auto lifecycle = context.lifecycle;
    portEXIT_CRITICAL(&context.mutex);
    return lifecycle;
}

void setLifecycle(std::uint32_t lifecycle) {
    portENTER_CRITICAL(&context.mutex);
    context.lifecycle = lifecycle;
    portEXIT_CRITICAL(&context.mutex);
}

bool isHostRunning() {
    portENTER_CRITICAL(&context.mutex);
    const bool running = context.hostRunning;
    portEXIT_CRITICAL(&context.mutex);
    return running;
}

std::uint16_t activeConnectionHandle() {
    portENTER_CRITICAL(&context.mutex);
    const auto connectionHandle = context.activeConnectionHandle;
    portEXIT_CRITICAL(&context.mutex);
    return connectionHandle;
}

void setActiveConnectionHandle(std::uint16_t connectionHandle) {
    portENTER_CRITICAL(&context.mutex);
    context.activeConnectionHandle = connectionHandle;
    portEXIT_CRITICAL(&context.mutex);
}

void clearActiveConnectionHandle(std::uint16_t connectionHandle) {
    portENTER_CRITICAL(&context.mutex);
    if (context.activeConnectionHandle == connectionHandle) {
        context.activeConnectionHandle = invalidConnectionHandle;
    }
    portEXIT_CRITICAL(&context.mutex);
}

Esp32BluetoothAdapter* currentOwner() {
    portENTER_CRITICAL(&context.mutex);
    auto* owner = context.owner;
    portEXIT_CRITICAL(&context.mutex);
    return owner;
}

void setOwner(Esp32BluetoothAdapter* owner) {
    portENTER_CRITICAL(&context.mutex);
    context.owner = owner;
    portEXIT_CRITICAL(&context.mutex);
}

bool takeOverflow() {
    portENTER_CRITICAL(&context.mutex);
    const bool overflow = context.queueOverflow;
    context.queueOverflow = false;
    portEXIT_CRITICAL(&context.mutex);
    return overflow;
}

bool dequeue(RawEvent& event) {
    portENTER_CRITICAL(&context.mutex);
    if (context.eventCount == 0) {
        portEXIT_CRITICAL(&context.mutex);
        return false;
    }
    event = context.events[context.eventHead];
    context.eventHead = (context.eventHead + 1) % context.events.size();
    --context.eventCount;
    portEXIT_CRITICAL(&context.mutex);
    return true;
}

void clearLifecycleState() {
    portENTER_CRITICAL(&context.mutex);
    context.eventHead = 0;
    context.eventTail = 0;
    context.eventCount = 0;
    context.queueOverflow = false;
    context.lifecycle = 0;
    context.activeConnectionHandle = invalidConnectionHandle;
    portEXIT_CRITICAL(&context.mutex);
    context.synchronized = false;
    context.advertisingRequested = false;
    context.advertisingActive = false;
    context.advertisingStopRequested = false;
    context.ownAddressType = BLE_OWN_ADDR_PUBLIC;
    context.deviceName.clear();
    context.peers = {};
}

void onHostSynchronized() {
    if (currentOwner() != nullptr) {
        enqueue({RawEventType::HostSynchronized, currentLifecycle()});
    }
}

void onHostReset(int reason) {
    if (currentOwner() != nullptr) {
        enqueue({RawEventType::HostFailed, currentLifecycle(), reason});
    }
}

int gapEventCallback(ble_gap_event* event, void*) {
    if (event == nullptr || currentOwner() == nullptr) {
        if (currentOwner() != nullptr) {
            latchQueueOverflow();
        }
        return 0;
    }

    RawEvent queued{};
    queued.lifecycle = currentLifecycle();
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            queued.type = RawEventType::AdvertisingCompleted;
            queued.status = event->connect.status;
            enqueue(queued);
            return 0;
        }
        queued.type = RawEventType::PeerConnected;
        queued.connectionHandle = event->connect.conn_handle;
        setActiveConnectionHandle(queued.connectionHandle);
        {
            ble_gap_conn_desc descriptor{};
            if (ble_gap_conn_find(queued.connectionHandle, &descriptor) != 0) {
                enqueue({RawEventType::HostFailed, queued.lifecycle});
                return 0;
            }
            queued.identityAddress = descriptor.peer_id_addr;
        }
        enqueue(queued);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        queued.type = RawEventType::PeerDisconnected;
        queued.connectionHandle = event->disconnect.conn.conn_handle;
        clearActiveConnectionHandle(queued.connectionHandle);
        enqueue(queued);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        queued.type = RawEventType::AdvertisingCompleted;
        queued.status = event->adv_complete.reason;
        enqueue(queued);
        return 0;
    default:
        return 0;
    }
}

void nimbleHostTask(void*) {
    nimble_port_run();
    portENTER_CRITICAL(&context.mutex);
    context.hostRunning = false;
    portEXIT_CRITICAL(&context.mutex);
    xSemaphoreGive(context.hostStopped);
    vTaskDelete(nullptr);
}

constexpr bool isRetryableAdvertisingError(int error) {
    switch (error) {
    case BLE_HS_EAGAIN:
    case BLE_HS_ENOMEM:
    case BLE_HS_ETIMEOUT:
    case BLE_HS_ETIMEOUT_HCI:
    case BLE_HS_ENOMEM_EVT:
    case BLE_HS_EBUSY:
        return true;
    default:
        return false;
    }
}

static_assert(isRetryableAdvertisingError(BLE_HS_EBUSY));
static_assert(isRetryableAdvertisingError(BLE_HS_ETIMEOUT));
static_assert(!isRetryableAdvertisingError(BLE_HS_EINVAL));
static_assert(!isRetryableAdvertisingError(BLE_HS_EALREADY));

connectivity::BluetoothFailureClass classifyAdvertisingError(int error) {
    return isRetryableAdvertisingError(error) ? connectivity::BluetoothFailureClass::Retryable
                                              : connectivity::BluetoothFailureClass::Fatal;
}

connectivity::BluetoothAdvertisingResult issueAdvertisingStart() {
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const auto advertisedNameLength =
        std::min(context.deviceName.size(), maximumLegacyAdvertisingNameLength);
    fields.name = reinterpret_cast<const std::uint8_t*>(context.deviceName.data());
    fields.name_len = static_cast<std::uint8_t>(advertisedNameLength);
    fields.name_is_complete = advertisedNameLength == context.deviceName.size();
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        context.advertisingRequested = false;
        return isRetryableAdvertisingError(result)
                   ? connectivity::BluetoothAdvertisingResult::RetryableFailure
                   : connectivity::BluetoothAdvertisingResult::AdapterError;
    }

    ble_gap_adv_params parameters{};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(context.ownAddressType, nullptr, BLE_HS_FOREVER, &parameters,
                               gapEventCallback, nullptr);
    if (result != 0) {
        context.advertisingRequested = false;
        return isRetryableAdvertisingError(result)
                   ? connectivity::BluetoothAdvertisingResult::RetryableFailure
                   : connectivity::BluetoothAdvertisingResult::AdapterError;
    }
    context.advertisingActive = true;
    enqueue({RawEventType::AdvertisingStarted, currentLifecycle()});
    return connectivity::BluetoothAdvertisingResult::Started;
}

PeerRecord* findPeer(connectivity::BluetoothPeerHandle handle) {
    const auto peer =
        std::find_if(context.peers.begin(), context.peers.end(), [handle](const auto& candidate) {
            return candidate.used && candidate.handle == handle;
        });
    return peer == context.peers.end() ? nullptr : &*peer;
}

PeerRecord* findPeer(std::uint16_t connectionHandle) {
    const auto peer = std::find_if(
        context.peers.begin(), context.peers.end(), [connectionHandle](const auto& candidate) {
            return candidate.used && candidate.connectionHandle == connectionHandle;
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
    peer->connectionHandle = event.connectionHandle;
    peer->identityAddress = event.identityAddress;
    return &*peer;
}

connectivity::BluetoothPollResult adapterFailure(std::uint32_t lifecycle) {
    return connectivity::BluetoothPollResult::withEvent(
        {connectivity::BluetoothEventType::AdapterFailed,
         {},
         connectivity::BluetoothFailureClass::Fatal,
         lifecycle});
}

bool waitForPeerDisconnection(std::uint16_t connectionHandle) {
    const TickType_t start = xTaskGetTickCount();
    ble_gap_conn_desc descriptor{};
    while (ble_gap_conn_find(connectionHandle, &descriptor) == 0) {
        if (xTaskGetTickCount() - start >= hostStopTimeout) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

bool terminateAndWaitForPeer(std::uint16_t connectionHandle) {
    const int terminateResult = ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
    if (terminateResult != 0 && terminateResult != BLE_HS_ENOTCONN &&
        terminateResult != BLE_HS_EALREADY) {
        return false;
    }
    return waitForPeerDisconnection(connectionHandle);
}

bool stopAdvertisingAndPeers() {
    context.advertisingRequested = false;
    if (ble_gap_adv_active() != 0) {
        const int stopResult = ble_gap_adv_stop();
        if (stopResult != 0 && stopResult != BLE_HS_EALREADY) {
            return false;
        }
    }
    context.advertisingActive = false;

    // GAP callbacks can precede Service polling. Track that connection separately
    // so shutdown also closes a peer whose queued connect event is not yet consumed.
    const auto callbackConnectionHandle = activeConnectionHandle();
    if (callbackConnectionHandle != invalidConnectionHandle &&
        !terminateAndWaitForPeer(callbackConnectionHandle)) {
        return false;
    }

    const bool processedPeersStopped =
        std::all_of(context.peers.begin(), context.peers.end(), [&](const auto& peer) {
            return !peer.used || peer.connectionHandle == callbackConnectionHandle ||
                   terminateAndWaitForPeer(peer.connectionHandle);
        });
    if (!processedPeersStopped) {
        return false;
    }
    context.peers = {};
    return true;
}

bool quiesceStack() {
    if (isHostRunning()) {
        if (!stopAdvertisingAndPeers() || nimble_port_stop() != 0 ||
            xSemaphoreTake(context.hostStopped, hostStopTimeout) != pdTRUE) {
            return false;
        }
    }
    if (context.hostInitialized) {
        if (esp_nimble_deinit() != ESP_OK) {
            return false;
        }
        context.hostInitialized = false;
    }
    if (context.controllerEnabled) {
        if (esp_bt_controller_disable() != ESP_OK) {
            return false;
        }
        context.controllerEnabled = false;
    }
    context.stackOwned = false;
    clearLifecycleState();
    return true;
}

void suppressIdentityBearingBluetoothLogTags() {
    constexpr std::array tags = {
        "BT", "BTDM_INIT", "NimBLE", "NIMBLE_PORT", "ble_hs", "BLE_HS", "BLE_ATT", "BLE_SMP",
    };
    std::for_each(tags.begin(), tags.end(),
                  [](const auto* tag) { esp_log_level_set(tag, ESP_LOG_NONE); });
}

} // namespace

Esp32BluetoothAdapter::~Esp32BluetoothAdapter() {
    if (currentOwner() == this && quiesceStack()) {
        setOwner(nullptr);
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
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        setOwner(this);
    } else {
        const auto expectedControllerStatus = context.controllerInitialized
                                                  ? ESP_BT_CONTROLLER_STATUS_INITED
                                                  : ESP_BT_CONTROLLER_STATUS_IDLE;
        if (context.stackOwned || context.hostInitialized || isHostRunning() ||
            context.controllerEnabled ||
            esp_bt_controller_get_status() != expectedControllerStatus) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
    }

    clearLifecycleState();
    setLifecycle(lifecycle);
    context.deviceName = config.deviceName;
    context.stackOwned = true;
    suppressIdentityBearingBluetoothLogTags();

    if (context.hostStopped == nullptr) {
        context.hostStopped = xSemaphoreCreateBinaryStatic(&context.hostStoppedStorage);
        if (context.hostStopped == nullptr) {
            context.stackOwned = false;
            clearLifecycleState();
            setOwner(nullptr);
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
    }
    while (xSemaphoreTake(context.hostStopped, 0) == pdTRUE) {
    }

    if (!context.controllerInitialized) {
        esp_bt_controller_config_t controllerConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&controllerConfig) != ESP_OK) {
            context.stackOwned = false;
            clearLifecycleState();
            setOwner(nullptr);
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        context.controllerInitialized = true;
    }
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.controllerEnabled = true;
    if (esp_nimble_init() != ESP_OK) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.hostInitialized = true;

    ble_hs_cfg.reset_cb = onHostReset;
    ble_hs_cfg.sync_cb = onHostSynchronized;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_store_config_init();
    if (ble_svc_gap_device_name_set(context.deviceName.c_str()) != 0) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }

    portENTER_CRITICAL(&context.mutex);
    context.hostRunning = true;
    portEXIT_CRITICAL(&context.mutex);
    if (xTaskCreate(nimbleHostTask, "nimble_host", 4'096, nullptr, configMAX_PRIORITIES - 4,
                    nullptr) != pdPASS) {
        portENTER_CRITICAL(&context.mutex);
        context.hostRunning = false;
        portEXIT_CRITICAL(&context.mutex);
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
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
        context.advertisingRequested || context.advertisingActive ||
        context.advertisingStopRequested) {
        return connectivity::BluetoothAdvertisingResult::AdapterError;
    }
    setLifecycle(lifecycle);
    context.advertisingRequested = true;
    if (!context.synchronized) {
        return connectivity::BluetoothAdvertisingResult::Started;
    }
    return issueAdvertisingStart();
}

connectivity::BluetoothAdapterResult Esp32BluetoothAdapter::requestAdvertisingStop() {
    if (currentOwner() != this || !context.stackOwned) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    context.advertisingRequested = false;
    if (!context.advertisingActive) {
        return connectivity::BluetoothAdapterResult::Success;
    }
    context.advertisingStopRequested = true;
    const int result = ble_gap_adv_stop();
    if (result != 0 && result != BLE_HS_EALREADY) {
        context.advertisingStopRequested = false;
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return connectivity::BluetoothAdapterResult::Success;
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::disconnectPeer(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    const int result = ble_gap_terminate(peer->connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
    return result == 0 || result == BLE_HS_EALREADY
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
        case RawEventType::HostSynchronized:
            if (ble_hs_util_ensure_addr(0) != 0 ||
                ble_hs_id_infer_auto(0, &context.ownAddressType) != 0) {
                return adapterFailure(event.lifecycle);
            }
            context.synchronized = true;
            if (context.advertisingRequested && !context.advertisingActive) {
                const auto result = issueAdvertisingStart();
                if (result == connectivity::BluetoothAdvertisingResult::RetryableFailure) {
                    return connectivity::BluetoothPollResult::withEvent(
                        {connectivity::BluetoothEventType::AdvertisingFailed,
                         {},
                         connectivity::BluetoothFailureClass::Retryable,
                         event.lifecycle});
                }
                if (result == connectivity::BluetoothAdvertisingResult::AdapterError) {
                    return connectivity::BluetoothPollResult::adapterError();
                }
            }
            break;
        case RawEventType::HostFailed:
            return adapterFailure(event.lifecycle);
        case RawEventType::AdvertisingStarted:
            return connectivity::BluetoothPollResult::withEvent(
                {connectivity::BluetoothEventType::AdvertisingStarted,
                 {},
                 connectivity::BluetoothFailureClass::Fatal,
                 event.lifecycle});
        case RawEventType::AdvertisingCompleted:
            context.advertisingActive = false;
            if (context.advertisingStopRequested) {
                context.advertisingStopRequested = false;
                break;
            }
            context.advertisingRequested = false;
            return connectivity::BluetoothPollResult::withEvent(
                {connectivity::BluetoothEventType::AdvertisingFailed,
                 {},
                 classifyAdvertisingError(event.status),
                 event.lifecycle});
        case RawEventType::PeerConnected: {
            context.advertisingRequested = false;
            context.advertisingActive = false;
            context.advertisingStopRequested = false;
            const auto* peer = addPeer(event);
            if (peer == nullptr) {
                return connectivity::BluetoothPollResult::adapterError();
            }
            return connectivity::BluetoothPollResult::withEvent(
                {connectivity::BluetoothEventType::PeerConnected, peer->handle,
                 connectivity::BluetoothFailureClass::Fatal, event.lifecycle});
        }
        case RawEventType::PeerDisconnected: {
            auto* peer = findPeer(event.connectionHandle);
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

    std::array<ble_addr_t, bondCapacity> bonds{};
    int bondCount = 0;
    if (ble_store_util_bonded_peers(bonds.data(), &bondCount, static_cast<int>(bonds.size())) !=
            0 ||
        bondCount < 0 || static_cast<std::size_t>(bondCount) > bonds.size()) {
        return connectivity::BluetoothBondQueryResult::AdapterError;
    }
    const auto match =
        std::find_if(bonds.begin(), bonds.begin() + bondCount, [&](const auto& bond) {
            return ble_addr_cmp(&bond, &peer->identityAddress) == 0;
        });
    return match == bonds.begin() + bondCount ? connectivity::BluetoothBondQueryResult::Unbonded
                                              : connectivity::BluetoothBondQueryResult::Bonded;
}

} // namespace cardputer_hub::hardware
