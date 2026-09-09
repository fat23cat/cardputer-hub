#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <esp_bt.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <mbedtls/md.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nvs.h>
#include <nvs_flash.h>
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
#if defined(ESP_PLATFORM) &&                                                                       \
    (!CONFIG_BT_NIMBLE_SECURITY_ENABLE || CONFIG_BT_NIMBLE_SM_LEGACY || !CONFIG_BT_NIMBLE_SM_SC || \
     CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS || !CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION ||           \
     CONFIG_BT_NIMBLE_SM_LVL != 3 || CONFIG_BT_NIMBLE_SM_SC_ONLY != 1 ||                           \
     CONFIG_BT_NIMBLE_MAX_BONDS != 16 || !CONFIG_BT_NIMBLE_NVS_PERSIST)
#error "Esp32BluetoothAdapter requires authenticated persistent LE Secure Connections"
#endif

constexpr std::size_t eventQueueCapacity = 16;
constexpr std::size_t peerCapacity = 1;
constexpr std::size_t bondCapacity = 16;
constexpr std::size_t maximumBluetoothDeviceNameLength = 248;
constexpr std::size_t maximumLegacyAdvertisingNameLength = 18;
constexpr std::size_t referenceKeySize = 32;
constexpr const char* configurationPartition = "hub_config";
constexpr const char* bluetoothMetadataNamespace = "bluetooth";
constexpr const char* referenceKeyName = "bond_ref_key";
constexpr TickType_t hostStopTimeout = pdMS_TO_TICKS(5'000);
constexpr std::uint16_t invalidConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
constexpr std::uint16_t hidServiceUuidValue = 0x1812;
constexpr std::uint16_t hidInformationUuidValue = 0x2A4A;
constexpr std::uint16_t hidReportMapUuidValue = 0x2A4B;
constexpr std::uint16_t hidControlPointUuidValue = 0x2A4C;
constexpr std::uint16_t hidReportUuidValue = 0x2A4D;
constexpr std::uint16_t hidProtocolModeUuidValue = 0x2A4E;
constexpr std::uint16_t hidReportReferenceUuidValue = 0x2908;
constexpr std::uint16_t keyboardAppearance = 0x03C1;
constexpr std::uint8_t keyboardReportId = connectivity::keyboardHidReportId;
constexpr std::uint8_t consumerReportId = connectivity::consumerHidReportId;
const auto& hidReportMap = connectivity::hidReportDescriptor;

enum class RawEventType : std::uint8_t {
    HostSynchronized,
    HostFailed,
    AdvertisingStarted,
    AdvertisingCompleted,
    PeerConnected,
    PeerDisconnected,
    PeerIdentityResolved,
    SecurityChallenge,
    SecurityCompleted,
    HidReadinessChanged,
};

struct RawEvent {
    RawEventType type = RawEventType::HostFailed;
    std::uint32_t lifecycle = 0;
    int status = 0;
    std::uint16_t connectionHandle = invalidConnectionHandle;
    ble_addr_t identityAddress{};
    std::uint8_t securityAction = BLE_SM_IOACT_NONE;
    std::uint32_t securityValue = 0;
    connectivity::BluetoothSecurityProperties security{};
    bool keyboardSubscribed = false;
    bool consumerSubscribed = false;
    bool reportProtocol = false;
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
    std::array<std::uint8_t, referenceKeySize> referenceKey{};
    bool referenceKeyLoaded = false;
    bool hidServiceRegistered = false;
    std::uint16_t keyboardInputHandle = 0;
    std::uint16_t keyboardOutputHandle = 0;
    std::uint16_t consumerInputHandle = 0;
    bool keyboardSubscribed = false;
    bool consumerSubscribed = false;
    connectivity::BluetoothSecurityProperties hidSecurity{};
    std::uint8_t hidProtocolMode = 1;
    std::uint8_t hidControlPoint = 1;
    StaticSemaphore_t hostStoppedStorage{};
    SemaphoreHandle_t hostStopped = nullptr;
};

AdapterContext context;

const ble_uuid16_t hidServiceUuid = BLE_UUID16_INIT(hidServiceUuidValue);
const ble_uuid16_t hidInformationUuid = BLE_UUID16_INIT(hidInformationUuidValue);
const ble_uuid16_t hidReportMapUuid = BLE_UUID16_INIT(hidReportMapUuidValue);
const ble_uuid16_t hidControlPointUuid = BLE_UUID16_INIT(hidControlPointUuidValue);
const ble_uuid16_t hidReportUuid = BLE_UUID16_INIT(hidReportUuidValue);
const ble_uuid16_t hidProtocolModeUuid = BLE_UUID16_INIT(hidProtocolModeUuidValue);
const ble_uuid16_t hidReportReferenceUuid = BLE_UUID16_INIT(hidReportReferenceUuidValue);

constexpr std::array<std::uint8_t, 4> hidInformation{0x11, 0x01, 0x00, 0x02};
constexpr std::array<std::uint8_t, 2> keyboardInputReference{keyboardReportId, 0x01};
constexpr std::array<std::uint8_t, 2> keyboardOutputReference{keyboardReportId, 0x02};
constexpr std::array<std::uint8_t, 2> consumerInputReference{consumerReportId, 0x01};
std::uint8_t hidInformationTag = 0;
std::uint8_t hidReportMapTag = 0;
std::uint8_t hidProtocolModeTag = 0;
std::uint8_t hidControlPointTag = 0;
std::uint8_t keyboardInputTag = 0;
std::uint8_t keyboardOutputTag = 0;
std::uint8_t consumerInputTag = 0;
std::array<ble_gatt_dsc_def, 2> keyboardInputDescriptors{};
std::array<ble_gatt_dsc_def, 2> keyboardOutputDescriptors{};
std::array<ble_gatt_dsc_def, 2> consumerInputDescriptors{};
std::array<ble_gatt_chr_def, 8> hidCharacteristics{};
std::array<ble_gatt_svc_def, 2> hidServices{};

void enqueue(const RawEvent& event);
RawEvent hidReadinessEvent(std::uint16_t connectionHandle);

int appendGattValue(os_mbuf* destination, const void* value, std::size_t size) {
    return os_mbuf_append(destination, value, static_cast<std::uint16_t>(size)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int readOneByte(os_mbuf* source, std::uint8_t& value) {
    if (OS_MBUF_PKTLEN(source) != 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return ble_hs_mbuf_to_flat(source, &value, sizeof(value), nullptr) == 0 ? 0
                                                                            : BLE_ATT_ERR_UNLIKELY;
}

int hidGattAccess(std::uint16_t connectionHandle, std::uint16_t, ble_gatt_access_ctxt* access,
                  void* tag) {
    if (access == nullptr) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (access->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        if (tag == keyboardInputDescriptors[0].arg) {
            return appendGattValue(access->om, keyboardInputReference.data(),
                                   keyboardInputReference.size());
        }
        if (tag == keyboardOutputDescriptors[0].arg) {
            return appendGattValue(access->om, keyboardOutputReference.data(),
                                   keyboardOutputReference.size());
        }
        if (tag == consumerInputDescriptors[0].arg) {
            return appendGattValue(access->om, consumerInputReference.data(),
                                   consumerInputReference.size());
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (access->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (tag == &hidInformationTag) {
            return appendGattValue(access->om, hidInformation.data(), hidInformation.size());
        }
        if (tag == &hidReportMapTag) {
            return appendGattValue(access->om, hidReportMap.data(), hidReportMap.size());
        }
        if (tag == &hidProtocolModeTag) {
            return appendGattValue(access->om, &context.hidProtocolMode,
                                   sizeof(context.hidProtocolMode));
        }
        if (tag == &keyboardInputTag) {
            constexpr std::array<std::uint8_t, 8> neutral{};
            return appendGattValue(access->om, neutral.data(), neutral.size());
        }
        if (tag == &keyboardOutputTag) {
            constexpr std::uint8_t neutral = 0;
            return appendGattValue(access->om, &neutral, sizeof(neutral));
        }
        if (tag == &consumerInputTag) {
            constexpr std::array<std::uint8_t, 2> neutral{};
            return appendGattValue(access->om, neutral.data(), neutral.size());
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    if (access->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (tag == &hidProtocolModeTag) {
            std::uint8_t mode = 0;
            const auto result = readOneByte(access->om, mode);
            if (result != 0) {
                return result;
            }
            if (mode > 1) {
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            portENTER_CRITICAL(&context.mutex);
            context.hidProtocolMode = mode;
            portEXIT_CRITICAL(&context.mutex);
            enqueue(hidReadinessEvent(connectionHandle));
            return 0;
        }
        if (tag == &hidControlPointTag) {
            std::uint8_t control = 0;
            const auto result = readOneByte(access->om, control);
            if (result != 0) {
                return result;
            }
            if (control > 1) {
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            context.hidControlPoint = control;
            return 0;
        }
        if (tag == &keyboardOutputTag) {
            std::uint8_t ignored = 0;
            return readOneByte(access->om, ignored);
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

void configureReportDescriptor(std::array<ble_gatt_dsc_def, 2>& descriptors) {
    descriptors = {};
    descriptors[0].uuid = &hidReportReferenceUuid.u;
    descriptors[0].att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC | BLE_ATT_F_READ_AUTHEN;
    descriptors[0].access_cb = hidGattAccess;
    descriptors[0].arg = &descriptors[0];
}

void configureHidCharacteristic(std::size_t index, const ble_uuid_t* uuid, void* tag,
                                ble_gatt_chr_flags flags, std::uint16_t* valueHandle,
                                ble_gatt_dsc_def* descriptors = nullptr) {
    auto& characteristic = hidCharacteristics[index];
    characteristic = {};
    characteristic.uuid = uuid;
    characteristic.access_cb = hidGattAccess;
    characteristic.arg = tag;
    characteristic.descriptors = descriptors;
    characteristic.flags = flags;
    characteristic.val_handle = valueHandle;
}

bool initializeHidService() {
    context.keyboardInputHandle = 0;
    context.keyboardOutputHandle = 0;
    context.consumerInputHandle = 0;
    context.keyboardSubscribed = false;
    context.consumerSubscribed = false;
    context.hidProtocolMode = 1;
    context.hidControlPoint = 1;
    configureReportDescriptor(keyboardInputDescriptors);
    configureReportDescriptor(keyboardOutputDescriptors);
    configureReportDescriptor(consumerInputDescriptors);

    constexpr ble_gatt_chr_flags secureRead =
        BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN;
    constexpr ble_gatt_chr_flags secureWrite =
        BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN;
    constexpr ble_gatt_chr_flags secureNotify = secureRead | BLE_GATT_CHR_F_NOTIFY |
                                                BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
                                                BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN;
    configureHidCharacteristic(0, &hidProtocolModeUuid.u, &hidProtocolModeTag,
                               secureRead | secureWrite, nullptr);
    configureHidCharacteristic(1, &hidReportMapUuid.u, &hidReportMapTag, secureRead, nullptr);
    configureHidCharacteristic(2, &hidReportUuid.u, &keyboardInputTag, secureNotify,
                               &context.keyboardInputHandle, keyboardInputDescriptors.data());
    configureHidCharacteristic(3, &hidReportUuid.u, &keyboardOutputTag,
                               secureRead | secureWrite | BLE_GATT_CHR_F_WRITE,
                               &context.keyboardOutputHandle, keyboardOutputDescriptors.data());
    configureHidCharacteristic(4, &hidReportUuid.u, &consumerInputTag, secureNotify,
                               &context.consumerInputHandle, consumerInputDescriptors.data());
    configureHidCharacteristic(5, &hidInformationUuid.u, &hidInformationTag, secureRead, nullptr);
    configureHidCharacteristic(6, &hidControlPointUuid.u, &hidControlPointTag, secureWrite,
                               nullptr);
    hidCharacteristics[7] = {};

    hidServices = {};
    hidServices[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    hidServices[0].uuid = &hidServiceUuid.u;
    hidServices[0].characteristics = hidCharacteristics.data();
    if (ble_gatts_count_cfg(hidServices.data()) != 0 ||
        ble_gatts_add_svcs(hidServices.data()) != 0 ||
        ble_svc_gap_device_appearance_set(keyboardAppearance) != 0) {
        return false;
    }
    context.hidServiceRegistered = true;
    return true;
}

void deinitializeHidService() {
    context.hidServiceRegistered = false;
    context.keyboardSubscribed = false;
    context.consumerSubscribed = false;
    context.hidSecurity = {};
    context.keyboardInputHandle = 0;
    context.keyboardOutputHandle = 0;
    context.consumerInputHandle = 0;
}

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

void resetHidPeerState() {
    portENTER_CRITICAL(&context.mutex);
    context.keyboardSubscribed = false;
    context.consumerSubscribed = false;
    context.hidSecurity = {};
    portEXIT_CRITICAL(&context.mutex);
}

void updateHidSecurity(const connectivity::BluetoothSecurityProperties& security) {
    portENTER_CRITICAL(&context.mutex);
    context.hidSecurity = security;
    portEXIT_CRITICAL(&context.mutex);
}

void updateHidSubscription(std::uint16_t attributeHandle, bool subscribed) {
    portENTER_CRITICAL(&context.mutex);
    if (attributeHandle == context.keyboardInputHandle) {
        context.keyboardSubscribed = subscribed;
    } else if (attributeHandle == context.consumerInputHandle) {
        context.consumerSubscribed = subscribed;
    }
    portEXIT_CRITICAL(&context.mutex);
}

RawEvent hidReadinessEvent(std::uint16_t connectionHandle) {
    RawEvent event{};
    event.type = RawEventType::HidReadinessChanged;
    event.lifecycle = currentLifecycle();
    event.connectionHandle = connectionHandle;
    portENTER_CRITICAL(&context.mutex);
    event.security = context.hidSecurity;
    event.keyboardSubscribed = context.keyboardSubscribed;
    event.consumerSubscribed = context.consumerSubscribed;
    event.reportProtocol = context.hidProtocolMode == 1;
    portEXIT_CRITICAL(&context.mutex);
    return event;
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
    context.referenceKey.fill(0);
    context.referenceKeyLoaded = false;
    deinitializeHidService();
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
        resetHidPeerState();
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
        resetHidPeerState();
        return 0;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        queued.type = RawEventType::PeerIdentityResolved;
        queued.connectionHandle = event->identity_resolved.conn_handle;
        queued.identityAddress = event->identity_resolved.peer_id_addr;
        enqueue(queued);
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        queued.connectionHandle = event->passkey.conn_handle;
        queued.securityAction = event->passkey.params.action;
        if (queued.securityAction == BLE_SM_IOACT_DISP) {
            queued.securityValue = esp_random() % 1'000'000U;
        } else if (queued.securityAction == BLE_SM_IOACT_NUMCMP) {
            queued.securityValue = event->passkey.params.numcmp;
        } else if (queued.securityAction != BLE_SM_IOACT_INPUT) {
            queued.type = RawEventType::SecurityCompleted;
            enqueue(queued);
            return 0;
        }
        queued.type = RawEventType::SecurityChallenge;
        enqueue(queued);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        queued.type = RawEventType::SecurityCompleted;
        queued.connectionHandle = event->enc_change.conn_handle;
        queued.status = event->enc_change.status;
        ble_gap_conn_desc descriptor{};
        if (queued.status == 0 && ble_gap_conn_find(queued.connectionHandle, &descriptor) == 0) {
            queued.security.encrypted = descriptor.sec_state.encrypted != 0;
            queued.security.authenticated = descriptor.sec_state.authenticated != 0;
            queued.security.bonded = descriptor.sec_state.bonded != 0;
        }
        updateHidSecurity(queued.security);
        enqueue(queued);
        enqueue(hidReadinessEvent(queued.connectionHandle));
        return 0;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == context.keyboardInputHandle ||
            event->subscribe.attr_handle == context.consumerInputHandle) {
            updateHidSubscription(event->subscribe.attr_handle, event->subscribe.cur_notify != 0);
            enqueue(hidReadinessEvent(event->subscribe.conn_handle));
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
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
    fields.uuids16 = &hidServiceUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.appearance = keyboardAppearance;
    fields.appearance_is_present = 1;
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

bool ensureReferenceKey() {
    if (nvs_flash_init_partition(configurationPartition) != ESP_OK) {
        return false;
    }
    nvs_handle_t handle{};
    if (nvs_open_from_partition(configurationPartition, bluetoothMetadataNamespace, NVS_READWRITE,
                                &handle) != ESP_OK) {
        return false;
    }

    std::size_t size = context.referenceKey.size();
    const auto readResult =
        nvs_get_blob(handle, referenceKeyName, context.referenceKey.data(), &size);
    if (readResult == ESP_OK) {
        nvs_close(handle);
        context.referenceKeyLoaded = size == context.referenceKey.size();
        return context.referenceKeyLoaded;
    }
    if (readResult != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return false;
    }

    esp_fill_random(context.referenceKey.data(), context.referenceKey.size());
    const bool stored = nvs_set_blob(handle, referenceKeyName, context.referenceKey.data(),
                                     context.referenceKey.size()) == ESP_OK &&
                        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (!stored) {
        context.referenceKey.fill(0);
        return false;
    }
    context.referenceKeyLoaded = true;
    return true;
}

bool deriveReference(const ble_addr_t& identityAddress,
                     connectivity::BluetoothBondReference& reference) {
    if (!context.referenceKeyLoaded) {
        return false;
    }
    std::array<std::uint8_t, sizeof(identityAddress.type) + sizeof(identityAddress.val)> input{};
    input.front() = identityAddress.type;
    std::copy(std::begin(identityAddress.val), std::end(identityAddress.val), input.begin() + 1);
    std::array<std::uint8_t, 32> digest{};
    const auto* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == nullptr ||
        mbedtls_md_hmac(sha256, context.referenceKey.data(), context.referenceKey.size(),
                        input.data(), input.size(), digest.data()) != 0) {
        return false;
    }
    std::copy_n(digest.begin(), reference.bytes.size(), reference.bytes.begin());
    return true;
}

bool enumerateBondAddresses(std::array<ble_addr_t, bondCapacity>& addresses, int& count) {
    count = 0;
    return ble_store_util_bonded_peers(addresses.data(), &count,
                                       static_cast<int>(addresses.size())) == 0 &&
           count >= 0 && static_cast<std::size_t>(count) <= addresses.size();
}

connectivity::BluetoothBondListResult enumerateBondReferences() {
    std::array<ble_addr_t, bondCapacity> addresses{};
    int count = 0;
    if (!context.referenceKeyLoaded || !enumerateBondAddresses(addresses, count)) {
        return {connectivity::BluetoothBondListStatus::AdapterError, {}};
    }
    std::vector<connectivity::BluetoothBondReference> references;
    references.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        connectivity::BluetoothBondReference reference{};
        if (!deriveReference(addresses[static_cast<std::size_t>(index)], reference) ||
            std::find(references.begin(), references.end(), reference) != references.end()) {
            return {connectivity::BluetoothBondListStatus::AdapterError, {}};
        }
        references.push_back(reference);
    }
    std::sort(references.begin(), references.end());
    return {connectivity::BluetoothBondListStatus::Success, std::move(references)};
}

std::optional<connectivity::BluetoothSecurityProperties>
storedBondSecurity(const PeerRecord& peer) {
    ble_store_key_sec key{};
    key.peer_addr = peer.identityAddress;
    ble_store_value_sec value{};
    if (ble_store_read_peer_sec(&key, &value) != 0) {
        return std::nullopt;
    }
    return connectivity::BluetoothSecurityProperties{value.sc != 0, false, value.authenticated != 0,
                                                     true};
}

bool findBondAddress(const connectivity::BluetoothBondReference& requested, ble_addr_t& address) {
    std::array<ble_addr_t, bondCapacity> addresses{};
    int count = 0;
    if (!enumerateBondAddresses(addresses, count)) {
        return false;
    }
    bool found = false;
    for (int index = 0; index < count; ++index) {
        connectivity::BluetoothBondReference candidate{};
        if (!deriveReference(addresses[static_cast<std::size_t>(index)], candidate)) {
            return false;
        }
        if (candidate == requested) {
            if (found) {
                return false;
            }
            address = addresses[static_cast<std::size_t>(index)];
            found = true;
        }
    }
    return found;
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
        if (!stopAdvertisingAndPeers()) {
            return false;
        }
        deinitializeHidService();
        if (nimble_port_stop() != 0 ||
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
        "BT",       "BTDM_INIT", "BLE_INIT",    "NimBLE",      "NIMBLE_PORT", "NIMBLE_NVS",
        "ble_hs",   "BLE_HS",    "BLE_ATT",     "BLE_SMP",     "ble_store",   "BT_HIDD",
        "BLE_HIDD", "ESP_HIDH",  "NIMBLE_HIDD", "NIMBLE_HIDH", "hid_parser",
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

    if (nvs_flash_init() != ESP_OK) {
        context.stackOwned = false;
        clearLifecycleState();
        setOwner(nullptr);
        return connectivity::BluetoothAdapterResult::AdapterError;
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
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_DISPLAY;
    ble_hs_cfg.sm_oob_data_flag = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_sec_lvl = CONFIG_BT_NIMBLE_SM_LVL;
    ble_hs_cfg.sm_our_key_dist = BLE_HS_KEY_DIST_ENC_KEY | BLE_HS_KEY_DIST_ID_KEY;
    ble_hs_cfg.sm_their_key_dist = BLE_HS_KEY_DIST_ENC_KEY | BLE_HS_KEY_DIST_ID_KEY;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_store_config_init();
    if (!initializeHidService()) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    if (!ensureReferenceKey()) {
        (void)quiesceStack();
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
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
    return result == 0 || result == BLE_HS_EALREADY || result == BLE_HS_ENOTCONN
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
        case RawEventType::PeerIdentityResolved: {
            auto* peer = findPeer(event.connectionHandle);
            if (peer != nullptr) {
                peer->identityAddress = event.identityAddress;
            }
            break;
        }
        case RawEventType::SecurityChallenge: {
            const auto* peer = findPeer(event.connectionHandle);
            if (peer == nullptr) {
                return connectivity::BluetoothPollResult::adapterError();
            }
            connectivity::BluetoothEvent challenge{
                connectivity::BluetoothEventType::PairingChallenge, peer->handle,
                connectivity::BluetoothFailureClass::Fatal, event.lifecycle};
            switch (event.securityAction) {
            case BLE_SM_IOACT_DISP:
                challenge.challengeType =
                    connectivity::BluetoothPairingChallengeType::DisplayPasskey;
                challenge.challengeValue = event.securityValue;
                break;
            case BLE_SM_IOACT_INPUT:
                challenge.challengeType = connectivity::BluetoothPairingChallengeType::EnterPasskey;
                break;
            case BLE_SM_IOACT_NUMCMP:
                challenge.challengeType =
                    connectivity::BluetoothPairingChallengeType::ConfirmComparison;
                challenge.challengeValue = event.securityValue;
                break;
            default:
                return connectivity::BluetoothPollResult::adapterError();
            }
            return connectivity::BluetoothPollResult::withEvent(challenge);
        }
        case RawEventType::SecurityCompleted: {
            const auto* peer = findPeer(event.connectionHandle);
            if (peer == nullptr) {
                break;
            }
            connectivity::BluetoothEvent completed{
                connectivity::BluetoothEventType::PairingCompleted, peer->handle,
                connectivity::BluetoothFailureClass::Fatal, event.lifecycle};
            completed.security = event.security;
            if (const auto stored = storedBondSecurity(*peer); stored.has_value()) {
                completed.security.bonded = true;
                completed.security.secureConnections = stored->secureConnections;
                completed.security.authenticated =
                    completed.security.authenticated && stored->authenticated;
            }
            return connectivity::BluetoothPollResult::withEvent(completed);
        }
        case RawEventType::HidReadinessChanged: {
            const auto* peer = findPeer(event.connectionHandle);
            if (peer == nullptr) {
                break;
            }
            connectivity::BluetoothEvent readiness{
                connectivity::BluetoothEventType::HidReadinessChanged, peer->handle,
                connectivity::BluetoothFailureClass::Fatal, event.lifecycle};
            readiness.security = event.security;
            if (const auto stored = storedBondSecurity(*peer); stored.has_value()) {
                readiness.security.bonded = true;
                readiness.security.secureConnections = stored->secureConnections;
                readiness.security.authenticated =
                    readiness.security.authenticated && stored->authenticated;
            }
            readiness.keyboardSubscribed = event.keyboardSubscribed;
            readiness.consumerSubscribed = event.consumerSubscribed;
            readiness.reportProtocol = event.reportProtocol;
            return connectivity::BluetoothPollResult::withEvent(readiness);
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

    std::array<ble_addr_t, bondCapacity> bondAddresses{};
    int bondCount = 0;
    if (ble_store_util_bonded_peers(bondAddresses.data(), &bondCount,
                                    static_cast<int>(bondAddresses.size())) != 0 ||
        bondCount < 0 || static_cast<std::size_t>(bondCount) > bondAddresses.size()) {
        return connectivity::BluetoothBondQueryResult::AdapterError;
    }
    const auto match = std::find_if(
        bondAddresses.begin(), bondAddresses.begin() + bondCount, [&](const auto& bondAddress) {
            return ble_addr_cmp(&bondAddress, &peer->identityAddress) == 0;
        });
    return match == bondAddresses.begin() + bondCount
               ? connectivity::BluetoothBondQueryResult::Unbonded
               : connectivity::BluetoothBondQueryResult::Bonded;
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::beginPairing(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr ||
        !context.referenceKeyLoaded) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    const auto knownBonds = enumerateBondReferences();
    if (knownBonds.status != connectivity::BluetoothBondListStatus::Success ||
        knownBonds.bonds.size() >= bondCapacity) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return ble_gap_security_initiate(peer->connectionHandle) == 0
               ? connectivity::BluetoothAdapterResult::Success
               : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::respondToPairing(connectivity::BluetoothPeerHandle handle,
                                        connectivity::BluetoothPairingChallengeType type,
                                        bool accepted, std::optional<std::uint32_t> passkey) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    if (!accepted && type != connectivity::BluetoothPairingChallengeType::ConfirmComparison) {
        return disconnectPeer(handle);
    }

    ble_sm_io response{};
    switch (type) {
    case connectivity::BluetoothPairingChallengeType::DisplayPasskey:
        if (!accepted || !passkey.has_value() || *passkey > 999'999) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        response.action = BLE_SM_IOACT_DISP;
        response.passkey = *passkey;
        break;
    case connectivity::BluetoothPairingChallengeType::EnterPasskey:
        if (!accepted || !passkey.has_value() || *passkey > 999'999) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        response.action = BLE_SM_IOACT_INPUT;
        response.passkey = *passkey;
        break;
    case connectivity::BluetoothPairingChallengeType::ConfirmComparison:
        if (passkey.has_value()) {
            return connectivity::BluetoothAdapterResult::AdapterError;
        }
        response.action = BLE_SM_IOACT_NUMCMP;
        response.numcmp_accept = accepted ? 1 : 0;
        break;
    }
    return ble_sm_inject_io(peer->connectionHandle, &response) == 0
               ? connectivity::BluetoothAdapterResult::Success
               : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothBondListResult Esp32BluetoothAdapter::bonds() {
    if (currentOwner() != this || !context.stackOwned) {
        return {connectivity::BluetoothBondListStatus::AdapterError, {}};
    }
    return enumerateBondReferences();
}

connectivity::BluetoothBondReferenceResult
Esp32BluetoothAdapter::bondReference(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr) {
        return {connectivity::BluetoothBondReferenceStatus::AdapterError, {}};
    }
    if (bondState(handle) != connectivity::BluetoothBondQueryResult::Bonded) {
        return {connectivity::BluetoothBondReferenceStatus::NotFound, {}};
    }
    connectivity::BluetoothBondReference reference{};
    if (!deriveReference(peer->identityAddress, reference)) {
        return {connectivity::BluetoothBondReferenceStatus::AdapterError, {}};
    }
    const auto knownBonds = enumerateBondReferences();
    if (knownBonds.status != connectivity::BluetoothBondListStatus::Success ||
        std::count(knownBonds.bonds.begin(), knownBonds.bonds.end(), reference) != 1) {
        return {connectivity::BluetoothBondReferenceStatus::AdapterError, {}};
    }
    return {connectivity::BluetoothBondReferenceStatus::Found, reference};
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::deleteBond(const connectivity::BluetoothBondReference& reference) {
    if (currentOwner() != this || !context.stackOwned || !context.referenceKeyLoaded) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    ble_addr_t address{};
    if (!findBondAddress(reference, address)) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return ble_store_util_delete_peer(&address) == 0
               ? connectivity::BluetoothAdapterResult::Success
               : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothAdapterResult
Esp32BluetoothAdapter::deleteBondForPeer(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || peer == nullptr) {
        return connectivity::BluetoothAdapterResult::AdapterError;
    }
    return ble_store_util_delete_peer(&peer->identityAddress) == 0
               ? connectivity::BluetoothAdapterResult::Success
               : connectivity::BluetoothAdapterResult::AdapterError;
}

connectivity::BluetoothHidAdapterResult
Esp32BluetoothAdapter::hidReadiness(connectivity::BluetoothPeerHandle handle) {
    const auto* peer = findPeer(handle);
    if (currentOwner() != this || !context.stackOwned || !context.hidServiceRegistered ||
        peer == nullptr) {
        return connectivity::BluetoothHidAdapterResult::AdapterError;
    }
    ble_gap_conn_desc descriptor{};
    const auto findResult = ble_gap_conn_find(peer->connectionHandle, &descriptor);
    if (findResult == BLE_HS_ENOTCONN) {
        return connectivity::BluetoothHidAdapterResult::Disconnected;
    }
    if (findResult != 0) {
        return connectivity::BluetoothHidAdapterResult::AdapterError;
    }

    portENTER_CRITICAL(&context.mutex);
    const bool subscribed = context.keyboardSubscribed && context.consumerSubscribed;
    const bool reportProtocol = context.hidProtocolMode == 1;
    portEXIT_CRITICAL(&context.mutex);
    return descriptor.sec_state.encrypted != 0 && descriptor.sec_state.authenticated != 0 &&
                   descriptor.sec_state.bonded != 0 && subscribed && reportProtocol
               ? connectivity::BluetoothHidAdapterResult::Ready
               : connectivity::BluetoothHidAdapterResult::NotReady;
}

namespace {

connectivity::BluetoothHidAdapterResult classifyHidSendResult(int result) {
    switch (result) {
    case 0:
        return connectivity::BluetoothHidAdapterResult::Sent;
    case BLE_HS_EAGAIN:
    case BLE_HS_EBUSY:
    case BLE_HS_ENOMEM:
    case BLE_HS_ENOMEM_EVT:
        return connectivity::BluetoothHidAdapterResult::Busy;
    case BLE_HS_ENOTCONN:
        return connectivity::BluetoothHidAdapterResult::Disconnected;
    default:
        return connectivity::BluetoothHidAdapterResult::AdapterError;
    }
}

connectivity::BluetoothHidAdapterResult sendHidPayload(std::uint16_t connectionHandle,
                                                       std::uint16_t attributeHandle,
                                                       const void* data, std::size_t size) {
    auto* payload = ble_hs_mbuf_from_flat(data, static_cast<std::uint16_t>(size));
    if (payload == nullptr) {
        return connectivity::BluetoothHidAdapterResult::Busy;
    }
    return classifyHidSendResult(
        ble_gatts_notify_custom(connectionHandle, attributeHandle, payload));
}

} // namespace

connectivity::BluetoothHidAdapterResult
Esp32BluetoothAdapter::sendHidReport(connectivity::BluetoothPeerHandle handle,
                                     const connectivity::HidReport& report) {
    const auto readiness = hidReadiness(handle);
    if (readiness != connectivity::BluetoothHidAdapterResult::Ready) {
        return readiness;
    }
    const auto* peer = findPeer(handle);
    if (peer == nullptr) {
        return connectivity::BluetoothHidAdapterResult::Disconnected;
    }
    if (const auto* keyboard = std::get_if<connectivity::HidKeyboardReport>(&report);
        keyboard != nullptr) {
        std::array<std::uint8_t, 8> payload{};
        payload[0] = keyboard->modifiers;
        std::copy(keyboard->usages.begin(), keyboard->usages.end(), payload.begin() + 2);
        return sendHidPayload(peer->connectionHandle, context.keyboardInputHandle, payload.data(),
                              payload.size());
    }
    const auto consumer = std::get<connectivity::HidConsumerReport>(report);
    const std::array<std::uint8_t, 2> payload{
        static_cast<std::uint8_t>(consumer.usage & 0xFF),
        static_cast<std::uint8_t>((consumer.usage >> 8) & 0xFF),
    };
    return sendHidPayload(peer->connectionHandle, context.consumerInputHandle, payload.data(),
                          payload.size());
}

connectivity::BluetoothHidAdapterResult
Esp32BluetoothAdapter::releaseHidReports(connectivity::BluetoothPeerHandle handle) {
    const auto readiness = hidReadiness(handle);
    if (readiness != connectivity::BluetoothHidAdapterResult::Ready) {
        return readiness;
    }
    const auto keyboardResult = sendHidReport(handle, connectivity::HidKeyboardReport::neutral());
    if (keyboardResult != connectivity::BluetoothHidAdapterResult::Sent) {
        return keyboardResult;
    }
    return sendHidReport(handle, connectivity::HidConsumerReport::neutral());
}

} // namespace cardputer_hub::hardware
