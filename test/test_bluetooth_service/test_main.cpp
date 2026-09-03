#include <unity.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/lifecycle/system_runtime.h"

namespace {

using cardputer_hub::connectivity::BluetoothAdapterResult;
using cardputer_hub::connectivity::BluetoothAdvertisingResult;
using cardputer_hub::connectivity::BluetoothBondQueryResult;
using cardputer_hub::connectivity::BluetoothDeviceConfig;
using cardputer_hub::connectivity::BluetoothDisableResult;
using cardputer_hub::connectivity::BluetoothEnableResult;
using cardputer_hub::connectivity::BluetoothEvent;
using cardputer_hub::connectivity::BluetoothEventType;
using cardputer_hub::connectivity::BluetoothFailureClass;
using cardputer_hub::connectivity::BluetoothPeerHandle;
using cardputer_hub::connectivity::BluetoothPollResult;
using cardputer_hub::connectivity::BluetoothService;
using cardputer_hub::connectivity::BluetoothState;
using cardputer_hub::connectivity::IBluetoothAdapter;
using cardputer_hub::connectivity::IWifiAdapter;
using cardputer_hub::connectivity::WifiAdapterResult;
using cardputer_hub::connectivity::WifiAdapterState;
using cardputer_hub::connectivity::WifiConnectResult;
using cardputer_hub::connectivity::WifiNetworkConfig;
using cardputer_hub::connectivity::WiFiService;
using cardputer_hub::connectivity::WifiState;
using cardputer_hub::core::BuildInfo;
using cardputer_hub::core::IDisplayAdapter;
using cardputer_hub::core::IKeyboardAdapter;
using cardputer_hub::core::ILogSink;
using cardputer_hub::core::InputEvents;
using cardputer_hub::core::IPlatformAdapter;
using cardputer_hub::core::Logger;
using cardputer_hub::core::LogLevel;
using cardputer_hub::core::LogRecord;
using cardputer_hub::core::PixelPosition;
using cardputer_hub::core::RgbColor;
using cardputer_hub::core::SystemRuntime;
using cardputer_hub::core::TextStyle;

struct CapturedLog {
    LogLevel level;
    std::string component;
    std::string message;
};

class CapturingLogSink final : public ILogSink {
  public:
    void write(const LogRecord& record) override {
        records.push_back({record.level, record.component, record.message});
    }

    std::vector<CapturedLog> records;
};

class FakeBluetoothAdapter final : public IBluetoothAdapter {
  public:
    BluetoothAdapterResult initialize(const BluetoothDeviceConfig& config,
                                      std::uint32_t lifecycle) override {
        ++callCount;
        ++initializeCount;
        initializeLifecycles.push_back(lifecycle);
        initializedConfigs.push_back(lastConfig = config);
        return initializeResult;
    }
    BluetoothAdvertisingResult startAdvertising(std::uint32_t lifecycle) override {
        ++callCount;
        ++startAdvertisingCount;
        advertisingLifecycles.push_back(lifecycle);
        if (!startAdvertisingResults.empty()) {
            const auto result = startAdvertisingResults.front();
            startAdvertisingResults.pop_front();
            return result;
        }
        return startAdvertisingResult;
    }
    BluetoothAdapterResult shutdown() override {
        ++callCount;
        ++shutdownCount;
        auto result = shutdownResult;
        if (!shutdownResults.empty()) {
            result = shutdownResults.front();
            shutdownResults.pop_front();
        }
        if (result == BluetoothAdapterResult::Success) {
            livePeers.clear();
            physicalAdvertising = false;
        }
        return result;
    }
    BluetoothAdapterResult requestAdvertisingStop() override {
        ++callCount;
        ++stopAdvertisingCount;
        if (advertisingStopRequestResult == BluetoothAdapterResult::Success &&
            stopCompletesImmediately) {
            physicalAdvertising = false;
        }
        return advertisingStopRequestResult;
    }
    BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle peer) override {
        ++callCount;
        disconnectedPeers.push_back(peer);
        return disconnectResult;
    }
    BluetoothPollResult pollEvent() override {
        ++callCount;
        ++pollCount;
        if (events.empty()) {
            return pollResult;
        }
        const auto event = events.front();
        events.pop_front();
        return BluetoothPollResult::withEvent(event);
    }
    BluetoothBondQueryResult bondState(BluetoothPeerHandle) override {
        ++callCount;
        ++bondQueryCount;
        return bondResult;
    }

    int callCount = 0;
    int initializeCount = 0;
    int startAdvertisingCount = 0;
    int shutdownCount = 0;
    int stopAdvertisingCount = 0;
    int pollCount = 0;
    int bondQueryCount = 0;
    BluetoothAdapterResult initializeResult = BluetoothAdapterResult::Success;
    BluetoothAdapterResult shutdownResult = BluetoothAdapterResult::Success;
    BluetoothAdvertisingResult startAdvertisingResult = BluetoothAdvertisingResult::Started;
    BluetoothAdapterResult advertisingStopRequestResult = BluetoothAdapterResult::Success;
    BluetoothAdapterResult disconnectResult = BluetoothAdapterResult::Success;
    BluetoothPollResult pollResult = BluetoothPollResult::noEvent();
    BluetoothBondQueryResult bondResult = BluetoothBondQueryResult::Bonded;
    bool physicalAdvertising = false;
    bool stopCompletesImmediately = true;
    BluetoothDeviceConfig lastConfig;
    std::vector<BluetoothDeviceConfig> initializedConfigs;
    std::vector<std::uint32_t> initializeLifecycles;
    std::vector<BluetoothPeerHandle> disconnectedPeers;
    std::vector<BluetoothPeerHandle> livePeers;
    std::deque<BluetoothEvent> events;
    std::deque<BluetoothAdvertisingResult> startAdvertisingResults;
    std::deque<BluetoothAdapterResult> shutdownResults;
    std::vector<std::uint32_t> advertisingLifecycles;
};

class IsolationWifiAdapter final : public IWifiAdapter {
  public:
    WifiAdapterResult initializeStation() override { return WifiAdapterResult::Success; }
    WifiAdapterResult connect(const WifiNetworkConfig&) override {
        return WifiAdapterResult::Success;
    }
    WifiAdapterResult disconnect() override { return WifiAdapterResult::Success; }
    WifiAdapterState state() const override { return linkState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return -45; }

    WifiAdapterState linkState = WifiAdapterState::Connecting;
};

class IsolationPlatform final : public IPlatformAdapter {
  public:
    void begin() override { ++beginCount; }
    void update() override { ++updateCount; }

    int beginCount = 0;
    int updateCount = 0;
};

class IsolationKeyboard final : public IKeyboardAdapter {
  public:
    void poll(InputEvents& events) override {
        ++pollCount;
        events.clear();
    }

    int pollCount = 0;
};

class IsolationDisplay final : public IDisplayAdapter {
  public:
    void clear(RgbColor) override { ++clearCount; }
    void drawText(PixelPosition, const char*, TextStyle) override { ++drawCount; }

    int clearCount = 0;
    int drawCount = 0;
};

BluetoothEvent advertisingStarted(std::uint32_t lifecycle = 1) {
    return {BluetoothEventType::AdvertisingStarted, {}, BluetoothFailureClass::Fatal, lifecycle};
}

BluetoothEvent advertisingFailed(BluetoothFailureClass failure, std::uint32_t lifecycle = 1) {
    return {BluetoothEventType::AdvertisingFailed, {}, failure, lifecycle};
}

BluetoothEvent peerConnected(std::uint32_t value, std::uint32_t lifecycle = 1) {
    return {BluetoothEventType::PeerConnected, {value}, BluetoothFailureClass::Fatal, lifecycle};
}

BluetoothEvent peerDisconnected(std::uint32_t value, std::uint32_t lifecycle = 1) {
    return {BluetoothEventType::PeerDisconnected, {value}, BluetoothFailureClass::Fatal, lifecycle};
}

} // namespace

void setUp() {}

void tearDown() {}

void test_construction_has_no_adapter_side_effects() {
    FakeBluetoothAdapter adapter;

    BluetoothService service(adapter);

    TEST_ASSERT_EQUAL_INT(0, adapter.callCount);
    (void)service;
}

void test_enable_initializes_once_starts_advertising_and_copies_config() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    BluetoothDeviceConfig config{"Cardputer Hub"};

    const auto firstResult = service.enable(config);
    config.deviceName = "mutated";
    const auto repeatedResult = service.enable({"ignored"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(firstResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::AlreadyEnabled),
                            static_cast<unsigned int>(repeatedResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.initializeLifecycles.front());
    TEST_ASSERT_EQUAL_STRING("Cardputer Hub", adapter.lastConfig.deviceName.c_str());
}

void test_disable_is_idempotent_and_stops_an_advertising_launch() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);

    const auto alreadyDisabled = service.disable();
    (void)service.enable({"Cardputer Hub"});
    const auto disabled = service.disable();
    const auto repeated = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AlreadyDisabled),
                            static_cast<unsigned int>(alreadyDisabled));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(disabled));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AlreadyDisabled),
                            static_cast<unsigned int>(repeated));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Disabled),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.stopAdvertisingCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.shutdownCount);
}

void test_reenable_starts_a_fresh_initialized_lifecycle() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);

    (void)service.enable({"Cardputer Hub"});
    (void)service.disable();
    const auto result = service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(2, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.initializeLifecycles.front());
    TEST_ASSERT_EQUAL_UINT32(2, adapter.initializeLifecycles.back());
}

void test_disable_shutdown_closes_a_peer_whose_connect_event_is_still_queued() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.livePeers.push_back({73});
    adapter.events.push_back(peerConnected(73));

    const auto result = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Disabled),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT32(0, adapter.livePeers.size());
    TEST_ASSERT_EQUAL_UINT32(1, adapter.events.size());
    TEST_ASSERT_EQUAL_INT(1, adapter.shutdownCount);
}

void test_disable_shutdown_is_definitive_when_stop_completion_would_fail() {
    FakeBluetoothAdapter adapter;
    adapter.physicalAdvertising = true;
    adapter.stopCompletesImmediately = false;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    const auto result = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_FALSE(adapter.physicalAdvertising);
    TEST_ASSERT_EQUAL_INT(1, adapter.stopAdvertisingCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.shutdownCount);
}

void test_disable_reports_error_when_definitive_shutdown_fails() {
    FakeBluetoothAdapter adapter;
    adapter.shutdownResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    const auto result = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.shutdownCount);
}

void test_disable_retries_owned_cleanup_until_shutdown_succeeds() {
    FakeBluetoothAdapter adapter;
    adapter.shutdownResults = {BluetoothAdapterResult::AdapterError,
                               BluetoothAdapterResult::Success};
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    const auto failed = service.disable();
    const auto recovered = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AdapterError),
                            static_cast<unsigned int>(failed));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(recovered));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Disabled),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.shutdownCount);
}

void test_enable_does_not_initialize_over_cleanup_that_still_fails() {
    FakeBluetoothAdapter adapter;
    adapter.shutdownResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.disable();

    const auto result = service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.shutdownCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
}

void test_failed_initialization_can_be_retried_only_by_later_explicit_enable() {
    FakeBluetoothAdapter adapter;
    adapter.initializeResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);

    const auto failed = service.enable({"Cardputer Hub"});
    service.update(std::chrono::hours(1));
    adapter.initializeResult = BluetoothAdapterResult::Success;
    const auto recovered = service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::AdapterError),
                            static_cast<unsigned int>(failed));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(recovered));
    TEST_ASSERT_EQUAL_INT(2, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
}

void test_fatal_advertising_launch_does_not_stop_an_unstarted_attempt() {
    FakeBluetoothAdapter adapter;
    adapter.startAdvertisingResult = BluetoothAdvertisingResult::AdapterError;
    BluetoothService service(adapter);

    const auto result = service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(0, adapter.stopAdvertisingCount);
}

void test_disable_reports_disconnect_error_for_an_active_connection() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(41));
    service.update(std::chrono::milliseconds::zero());
    adapter.disconnectResult = BluetoothAdapterResult::AdapterError;

    const auto result = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(0, adapter.stopAdvertisingCount);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.disconnectedPeers.size());
}

void test_disable_reports_stop_error_for_an_advertising_launch() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.advertisingStopRequestResult = BluetoothAdapterResult::AdapterError;

    const auto result = service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.stopAdvertisingCount);
}

void test_adapter_events_change_state_only_when_update_polls_them() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(advertisingStarted());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(0, adapter.pollCount);

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Advertising),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.pollCount);
}

void test_bonded_peer_becomes_the_single_current_connection() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Connected),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_TRUE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_UINT32(17, service.currentConnection()->value);
    TEST_ASSERT_EQUAL_INT(1, adapter.bondQueryCount);
    TEST_ASSERT_EQUAL_UINT32(0, adapter.disconnectedPeers.size());
}

void test_additional_peer_is_rejected_without_replacing_active_peer() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerConnected(29));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Connected),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT32(17, service.currentConnection()->value);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.disconnectedPeers.size());
    TEST_ASSERT_EQUAL_UINT32(29, adapter.disconnectedPeers.front().value);
    TEST_ASSERT_EQUAL_INT(1, adapter.bondQueryCount);
}

void test_failed_additional_peer_rejection_is_fatal() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));
    service.update(std::chrono::milliseconds::zero());
    adapter.disconnectResult = BluetoothAdapterResult::AdapterError;
    adapter.events.push_back(peerConnected(29));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
}

void test_unbonded_peer_is_rejected_and_advertising_restarts_after_one_second() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(23));

    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::hours(1));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    adapter.events.push_back(peerDisconnected(23));
    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_UINT32(23, adapter.disconnectedPeers.front().value);
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_rejected_additional_peer_blocks_reconnect_after_current_peer_disconnects() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerConnected(29));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerDisconnected(17));

    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::hours(1));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    adapter.events.push_back(peerDisconnected(29));
    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);

    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_failed_unbonded_peer_rejection_is_fatal() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    adapter.disconnectResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(23));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
}

void test_bond_query_error_rejects_peer_and_enters_error() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(31));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT32(31, adapter.disconnectedPeers.front().value);
}

void test_unexpected_disconnect_waits_one_second_before_advertising() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerDisconnected(17));

    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::RetryWaiting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);

    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_disconnect_for_an_unknown_peer_does_not_disturb_active_connection() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(17));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerDisconnected(99));

    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Connected),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT32(17, service.currentConnection()->value);
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
}

void test_stale_events_after_disable_cannot_reactivate_service() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.disable();
    adapter.events.push_back(advertisingStarted());
    adapter.events.push_back(peerConnected(55));

    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Disabled),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_INT(0, adapter.bondQueryCount);
}

void test_stale_events_from_failed_generation_are_ignored_after_reenable() {
    FakeBluetoothAdapter adapter;
    adapter.initializeResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(55, 1));
    adapter.initializeResult = BluetoothAdapterResult::Success;

    (void)service.enable({"Cardputer Hub"});
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_INT(0, adapter.bondQueryCount);
    TEST_ASSERT_EQUAL_UINT32(2, adapter.advertisingLifecycles.back());
}

void test_retryable_launch_failure_enables_service_and_retries_at_one_second() {
    FakeBluetoothAdapter adapter;
    adapter.startAdvertisingResults = {BluetoothAdvertisingResult::RetryableFailure,
                                       BluetoothAdvertisingResult::Started};
    BluetoothService service(adapter);

    const auto result = service.enable({"Cardputer Hub"});
    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_advertising_failures_follow_capped_exponential_retry_schedule() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(advertisingFailed(BluetoothFailureClass::Retryable));
    service.update(std::chrono::milliseconds::zero());
    const std::chrono::seconds delays[] = {
        std::chrono::seconds(1),  std::chrono::seconds(2),  std::chrono::seconds(4),
        std::chrono::seconds(8),  std::chrono::seconds(16), std::chrono::seconds(30),
        std::chrono::seconds(30),
    };

    for (std::size_t index = 0; index < std::size(delays); ++index) {
        service.update(delays[index] - std::chrono::milliseconds(1));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(index) + 1, adapter.startAdvertisingCount);
        service.update(std::chrono::milliseconds(1));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(index) + 2, adapter.startAdvertisingCount);
        adapter.events.push_back(advertisingFailed(BluetoothFailureClass::Retryable));
        service.update(std::chrono::milliseconds::zero());
    }
}

void test_successful_advertising_resets_backoff_to_one_second() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(advertisingFailed(BluetoothFailureClass::Retryable));
    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::seconds(1));
    adapter.events.push_back(advertisingStarted());
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(advertisingFailed(BluetoothFailureClass::Retryable));
    service.update(std::chrono::milliseconds::zero());

    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_INT(3, adapter.startAdvertisingCount);
}

void test_fatal_advertising_event_enters_error_without_retry() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(advertisingFailed(BluetoothFailureClass::Fatal));

    service.update(std::chrono::hours(1));
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
}

void test_polling_failure_and_adapter_failure_event_are_fatal() {
    FakeBluetoothAdapter pollingAdapter;
    BluetoothService pollingService(pollingAdapter);
    (void)pollingService.enable({"Cardputer Hub"});
    pollingAdapter.pollResult = BluetoothPollResult::adapterError();
    pollingService.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(pollingService.state()));

    FakeBluetoothAdapter eventAdapter;
    BluetoothService eventService(eventAdapter);
    (void)eventService.enable({"Cardputer Hub"});
    eventAdapter.events.push_back(
        {BluetoothEventType::AdapterFailed, {}, BluetoothFailureClass::Fatal, 1});
    eventService.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(eventService.state()));
}

void test_shutdown_failure_after_poll_error_stops_all_later_polling() {
    FakeBluetoothAdapter adapter;
    adapter.shutdownResult = BluetoothAdapterResult::AdapterError;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.pollResult = BluetoothPollResult::adapterError();

    service.update(std::chrono::milliseconds::zero());
    const auto pollCountAtError = adapter.pollCount;
    adapter.events.push_back(advertisingStarted());
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(pollCountAtError, adapter.pollCount);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.events.size());
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
}

void test_bluetooth_failure_leaves_wifi_and_system_core_operational() {
    FakeBluetoothAdapter bluetoothAdapter;
    bluetoothAdapter.initializeResult = BluetoothAdapterResult::AdapterError;
    BluetoothService bluetoothService(bluetoothAdapter);

    IsolationWifiAdapter wifiAdapter;
    WiFiService wifiService(wifiAdapter);

    IsolationPlatform platform;
    IsolationKeyboard keyboard;
    IsolationDisplay display;
    CapturingLogSink logSink;
    Logger logger(logSink, LogLevel::Info);
    const BuildInfo buildInfo{"Test Hub", "1.0.0", "test", "test"};
    SystemRuntime runtime(platform, keyboard, display, logger, buildInfo);

    runtime.start();
    const auto wifiResult = wifiService.connect({"test-network", ""});
    const auto bluetoothResult = bluetoothService.enable({"Cardputer Hub"});
    wifiAdapter.linkState = WifiAdapterState::Connected;
    wifiService.update(std::chrono::milliseconds::zero());
    (void)runtime.update();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::AdapterError),
                            static_cast<unsigned int>(bluetoothResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(bluetoothService.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::Started),
                            static_cast<unsigned int>(wifiResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connected),
                            static_cast<unsigned int>(wifiService.state()));
    TEST_ASSERT_EQUAL_INT(1, platform.beginCount);
    TEST_ASSERT_EQUAL_INT(1, platform.updateCount);
    TEST_ASSERT_EQUAL_INT(1, keyboard.pollCount);
    TEST_ASSERT_EQUAL_INT(1, display.clearCount);
    TEST_ASSERT_EQUAL_INT(2, display.drawCount);
}

void test_fatal_retry_launch_enters_error_and_later_explicit_enable_recovers() {
    FakeBluetoothAdapter adapter;
    adapter.startAdvertisingResults = {BluetoothAdvertisingResult::RetryableFailure,
                                       BluetoothAdvertisingResult::AdapterError,
                                       BluetoothAdvertisingResult::Started};
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    service.update(std::chrono::seconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothState::Error),
                            static_cast<unsigned int>(service.state()));
    service.update(std::chrono::hours(1));
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);

    const auto recovery = service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(recovery));
    TEST_ASSERT_EQUAL_INT(3, adapter.startAdvertisingCount);
    TEST_ASSERT_EQUAL_INT(2, adapter.initializeCount);
}

void test_disable_while_retry_waiting_cancels_retry_without_stop() {
    FakeBluetoothAdapter adapter;
    adapter.startAdvertisingResult = BluetoothAdvertisingResult::RetryableFailure;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    const auto result = service.disable();
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.stopAdvertisingCount);
}

void test_logs_never_contain_peer_handle_or_device_name() {
    FakeBluetoothAdapter adapter;
    CapturingLogSink sink;
    Logger logger(sink, LogLevel::Debug);
    BluetoothService service(adapter, logger);
    const std::string secretName = "SECRET_DEVICE_NAME";
    const std::string peerValue = "987654321";
    (void)service.enable({secretName});
    adapter.events.push_back(peerConnected(987654321));
    service.update(std::chrono::milliseconds::zero());
    adapter.events.push_back(peerDisconnected(987654321));
    service.update(std::chrono::milliseconds::zero());
    (void)service.disable();

    TEST_ASSERT_GREATER_THAN_UINT32(0, sink.records.size());
    for (const auto& record : sink.records) {
        TEST_ASSERT_EQUAL_STRING("bluetooth", record.component.c_str());
        TEST_ASSERT_NULL(std::strstr(record.message.c_str(), secretName.c_str()));
        TEST_ASSERT_NULL(std::strstr(record.message.c_str(), peerValue.c_str()));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_construction_has_no_adapter_side_effects);
    RUN_TEST(test_enable_initializes_once_starts_advertising_and_copies_config);
    RUN_TEST(test_disable_is_idempotent_and_stops_an_advertising_launch);
    RUN_TEST(test_reenable_starts_a_fresh_initialized_lifecycle);
    RUN_TEST(test_disable_shutdown_closes_a_peer_whose_connect_event_is_still_queued);
    RUN_TEST(test_disable_shutdown_is_definitive_when_stop_completion_would_fail);
    RUN_TEST(test_disable_reports_error_when_definitive_shutdown_fails);
    RUN_TEST(test_disable_retries_owned_cleanup_until_shutdown_succeeds);
    RUN_TEST(test_enable_does_not_initialize_over_cleanup_that_still_fails);
    RUN_TEST(test_failed_initialization_can_be_retried_only_by_later_explicit_enable);
    RUN_TEST(test_fatal_advertising_launch_does_not_stop_an_unstarted_attempt);
    RUN_TEST(test_disable_reports_disconnect_error_for_an_active_connection);
    RUN_TEST(test_disable_reports_stop_error_for_an_advertising_launch);
    RUN_TEST(test_adapter_events_change_state_only_when_update_polls_them);
    RUN_TEST(test_bonded_peer_becomes_the_single_current_connection);
    RUN_TEST(test_additional_peer_is_rejected_without_replacing_active_peer);
    RUN_TEST(test_failed_additional_peer_rejection_is_fatal);
    RUN_TEST(test_unbonded_peer_is_rejected_and_advertising_restarts_after_one_second);
    RUN_TEST(test_rejected_additional_peer_blocks_reconnect_after_current_peer_disconnects);
    RUN_TEST(test_failed_unbonded_peer_rejection_is_fatal);
    RUN_TEST(test_bond_query_error_rejects_peer_and_enters_error);
    RUN_TEST(test_unexpected_disconnect_waits_one_second_before_advertising);
    RUN_TEST(test_disconnect_for_an_unknown_peer_does_not_disturb_active_connection);
    RUN_TEST(test_stale_events_after_disable_cannot_reactivate_service);
    RUN_TEST(test_stale_events_from_failed_generation_are_ignored_after_reenable);
    RUN_TEST(test_retryable_launch_failure_enables_service_and_retries_at_one_second);
    RUN_TEST(test_advertising_failures_follow_capped_exponential_retry_schedule);
    RUN_TEST(test_successful_advertising_resets_backoff_to_one_second);
    RUN_TEST(test_fatal_advertising_event_enters_error_without_retry);
    RUN_TEST(test_polling_failure_and_adapter_failure_event_are_fatal);
    RUN_TEST(test_shutdown_failure_after_poll_error_stops_all_later_polling);
    RUN_TEST(test_bluetooth_failure_leaves_wifi_and_system_core_operational);
    RUN_TEST(test_fatal_retry_launch_enters_error_and_later_explicit_enable_recovers);
    RUN_TEST(test_disable_while_retry_waiting_cancels_retry_without_stop);
    RUN_TEST(test_logs_never_contain_peer_handle_or_device_name);
    return UNITY_END();
}
