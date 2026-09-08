#include <unity.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/lifecycle/system_runtime.h"

namespace {

using cardputer_hub::connectivity::BluetoothAdapterResult;
using cardputer_hub::connectivity::BluetoothAdvertisingResult;
using cardputer_hub::connectivity::BluetoothBondListResult;
using cardputer_hub::connectivity::BluetoothBondListStatus;
using cardputer_hub::connectivity::BluetoothBondQueryResult;
using cardputer_hub::connectivity::BluetoothBondReference;
using cardputer_hub::connectivity::BluetoothBondReferenceResult;
using cardputer_hub::connectivity::BluetoothBondReferenceStatus;
using cardputer_hub::connectivity::BluetoothBondRemovalResult;
using cardputer_hub::connectivity::BluetoothBondSelectionResult;
using cardputer_hub::connectivity::BluetoothDeviceConfig;
using cardputer_hub::connectivity::BluetoothDisableResult;
using cardputer_hub::connectivity::BluetoothEnableResult;
using cardputer_hub::connectivity::BluetoothEvent;
using cardputer_hub::connectivity::BluetoothEventType;
using cardputer_hub::connectivity::BluetoothFailureClass;
using cardputer_hub::connectivity::BluetoothHidAdapterResult;
using cardputer_hub::connectivity::BluetoothPairingCancelResult;
using cardputer_hub::connectivity::BluetoothPairingChallengeType;
using cardputer_hub::connectivity::BluetoothPairingOpenResult;
using cardputer_hub::connectivity::BluetoothPairingResponse;
using cardputer_hub::connectivity::BluetoothPairingResponseResult;
using cardputer_hub::connectivity::BluetoothPairingState;
using cardputer_hub::connectivity::BluetoothPeerHandle;
using cardputer_hub::connectivity::BluetoothPollResult;
using cardputer_hub::connectivity::BluetoothRemoveAllBondsResult;
using cardputer_hub::connectivity::BluetoothService;
using cardputer_hub::connectivity::BluetoothState;
using cardputer_hub::connectivity::HidConsumerReport;
using cardputer_hub::connectivity::HidKeyboardReport;
using cardputer_hub::connectivity::HidReport;
using cardputer_hub::connectivity::HidSendResult;
using cardputer_hub::connectivity::HidTransportState;
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
    BluetoothAdapterResult beginPairing(BluetoothPeerHandle peer) override {
        pairingPeers.push_back(peer);
        return beginPairingResult;
    }
    BluetoothAdapterResult respondToPairing(BluetoothPeerHandle peer,
                                            BluetoothPairingChallengeType type, bool accepted,
                                            std::optional<std::uint32_t> passkey) override {
        pairingResponses.push_back({peer, type, accepted, passkey});
        return pairingResponseResult;
    }
    BluetoothBondListResult bonds() override { return bondListResult; }
    BluetoothBondReferenceResult bondReference(BluetoothPeerHandle peer) override {
        const auto found = peerReferences.find(peer.value);
        if (found != peerReferences.end()) {
            return {BluetoothBondReferenceStatus::Found, found->second};
        }
        return defaultReferenceResult;
    }
    BluetoothAdapterResult deleteBond(const BluetoothBondReference& reference) override {
        deletedBonds.push_back(reference);
        if (!deleteResults.empty()) {
            const auto result = deleteResults.front();
            deleteResults.pop_front();
            return result;
        }
        return deleteResult;
    }
    BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle peer) override {
        deletedPeerBonds.push_back(peer);
        return deletePeerBondResult;
    }
    BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle peer) override {
        hidReadinessPeers.push_back(peer);
        return hidReadinessResult;
    }
    BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle peer,
                                            const HidReport& report) override {
        hidSendPeers.push_back(peer);
        sentHidReports.push_back(report);
        return hidSendResult;
    }
    BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle peer) override {
        hidReleasePeers.push_back(peer);
        return hidReleaseResult;
    }

    struct PairingResponseCall {
        BluetoothPeerHandle peer;
        BluetoothPairingChallengeType type;
        bool accepted;
        std::optional<std::uint32_t> passkey;
    };

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
    BluetoothAdapterResult beginPairingResult = BluetoothAdapterResult::Success;
    BluetoothAdapterResult pairingResponseResult = BluetoothAdapterResult::Success;
    BluetoothAdapterResult deleteResult = BluetoothAdapterResult::Success;
    BluetoothAdapterResult deletePeerBondResult = BluetoothAdapterResult::Success;
    BluetoothHidAdapterResult hidReadinessResult = BluetoothHidAdapterResult::Ready;
    BluetoothHidAdapterResult hidSendResult = BluetoothHidAdapterResult::Sent;
    BluetoothHidAdapterResult hidReleaseResult = BluetoothHidAdapterResult::Sent;
    BluetoothBondListResult bondListResult{BluetoothBondListStatus::Success, {}};
    BluetoothBondReferenceResult defaultReferenceResult{BluetoothBondReferenceStatus::Found, {{1}}};
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
    std::deque<BluetoothAdapterResult> deleteResults;
    std::vector<std::uint32_t> advertisingLifecycles;
    std::vector<BluetoothPeerHandle> pairingPeers;
    std::vector<PairingResponseCall> pairingResponses;
    std::vector<BluetoothBondReference> deletedBonds;
    std::vector<BluetoothPeerHandle> deletedPeerBonds;
    std::vector<BluetoothPeerHandle> hidReadinessPeers;
    std::vector<BluetoothPeerHandle> hidSendPeers;
    std::vector<BluetoothPeerHandle> hidReleasePeers;
    std::vector<HidReport> sentHidReports;
    std::map<std::uint32_t, BluetoothBondReference> peerReferences;
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

BluetoothEvent pairingChallenge(std::uint32_t peer, BluetoothPairingChallengeType type,
                                std::optional<std::uint32_t> value = std::nullopt,
                                std::uint32_t lifecycle = 1) {
    BluetoothEvent event{
        BluetoothEventType::PairingChallenge, {peer}, BluetoothFailureClass::Fatal, lifecycle};
    event.challengeType = type;
    event.challengeValue = value;
    return event;
}

BluetoothEvent pairingCompleted(std::uint32_t peer,
                                cardputer_hub::connectivity::BluetoothSecurityProperties security,
                                std::uint32_t lifecycle = 1) {
    BluetoothEvent event{
        BluetoothEventType::PairingCompleted, {peer}, BluetoothFailureClass::Fatal, lifecycle};
    event.security = security;
    return event;
}

BluetoothEvent hidReadinessChanged(std::uint32_t peer, bool keyboardSubscribed,
                                   bool consumerSubscribed, std::uint32_t lifecycle = 1,
                                   bool reportProtocol = true) {
    BluetoothEvent event{
        BluetoothEventType::HidReadinessChanged, {peer}, BluetoothFailureClass::Fatal, lifecycle};
    event.security = {true, true, true, true};
    event.keyboardSubscribed = keyboardSubscribed;
    event.consumerSubscribed = consumerSubscribed;
    event.reportProtocol = reportProtocol;
    return event;
}

BluetoothBondReference bond(std::uint8_t discriminator) {
    BluetoothBondReference reference{};
    reference.bytes.front() = discriminator;
    return reference;
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

void test_pairing_is_closed_until_explicitly_opened() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);

    TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned int>(service.pairingState()));
    TEST_ASSERT_FALSE(service.pairingChallenge().has_value());
    TEST_ASSERT_FALSE(service.completedPairing().has_value());
}

void test_pairing_requires_enable_is_idempotent_and_times_out_from_advertising_start() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingOpenResult::Disabled),
                            static_cast<unsigned int>(service.openPairing()));
    (void)service.enable({"Cardputer Hub"});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingOpenResult::Opened),
                            static_cast<unsigned int>(service.openPairing()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingOpenResult::AlreadyOpen),
                            static_cast<unsigned int>(service.openPairing()));
    service.update(std::chrono::hours(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::Preparing),
                            static_cast<unsigned int>(service.pairingState()));

    adapter.events.push_back(advertisingStarted());
    service.update(std::chrono::milliseconds::zero());
    service.update(BluetoothService::pairingWindowDuration - std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::Advertising),
                            static_cast<unsigned int>(service.pairingState()));
    service.update(std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::Closed),
                            static_cast<unsigned int>(service.pairingState()));
}

void test_open_pairing_disconnects_current_bond_without_deleting_it() {
    FakeBluetoothAdapter adapter;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(7));
    service.update(std::chrono::milliseconds::zero());

    const auto result = service.openPairing();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingOpenResult::Opened),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT32(7, adapter.disconnectedPeers.back().value);
    TEST_ASSERT_EQUAL_UINT32(0, adapter.deletedBonds.size());
    adapter.events.push_back(peerDisconnected(7));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_pairing_admits_one_unbonded_peer_and_publishes_each_authenticated_challenge() {
    const struct Scenario {
        BluetoothPairingChallengeType type;
        std::optional<std::uint32_t> value;
    } scenarios[] = {
        {BluetoothPairingChallengeType::DisplayPasskey, 42},
        {BluetoothPairingChallengeType::EnterPasskey, std::nullopt},
        {BluetoothPairingChallengeType::ConfirmComparison, 654321},
    };

    for (const auto& scenario : scenarios) {
        FakeBluetoothAdapter adapter;
        adapter.bondResult = BluetoothBondQueryResult::Unbonded;
        BluetoothService service(adapter);
        (void)service.enable({"Cardputer Hub"});
        adapter.events.push_back(advertisingStarted());
        service.update(std::chrono::milliseconds::zero());
        (void)service.openPairing();
        adapter.events.push_back(peerConnected(8));
        adapter.events.push_back(pairingChallenge(8, scenario.type, scenario.value));

        service.update(std::chrono::milliseconds::zero());

        TEST_ASSERT_EQUAL_UINT32(1, adapter.pairingPeers.size());
        TEST_ASSERT_TRUE(service.pairingChallenge().has_value());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(scenario.type),
                                static_cast<unsigned int>(service.pairingChallenge()->type));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::AwaitingResponse),
                                static_cast<unsigned int>(service.pairingState()));
    }
}

void test_pairing_response_validates_generation_kind_and_six_digit_entry() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.openPairing();
    adapter.events.push_back(peerConnected(8));
    adapter.events.push_back(pairingChallenge(8, BluetoothPairingChallengeType::EnterPasskey));
    service.update(std::chrono::milliseconds::zero());
    const auto generation = service.pairingChallenge()->generation;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothPairingResponseResult::StaleGeneration),
        static_cast<unsigned int>(service.respondToPairing(
            {generation + 1, BluetoothPairingChallengeType::EnterPasskey, true, "012345"})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothPairingResponseResult::WrongKind),
        static_cast<unsigned int>(service.respondToPairing(
            {generation, BluetoothPairingChallengeType::ConfirmComparison, true, ""})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothPairingResponseResult::MalformedPasskey),
        static_cast<unsigned int>(service.respondToPairing(
            {generation, BluetoothPairingChallengeType::EnterPasskey, true, "12345"})));
    TEST_ASSERT_EQUAL_UINT32(0, adapter.pairingResponses.size());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothPairingResponseResult::Accepted),
        static_cast<unsigned int>(service.respondToPairing(
            {generation, BluetoothPairingChallengeType::EnterPasskey, true, "012345"})));
    TEST_ASSERT_EQUAL_UINT32(12345, *adapter.pairingResponses.front().passkey);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothPairingResponseResult::RepeatedResponse),
        static_cast<unsigned int>(service.respondToPairing(
            {generation, BluetoothPairingChallengeType::EnterPasskey, true, "012345"})));
    TEST_ASSERT_EQUAL_UINT32(1, adapter.pairingResponses.size());
}

void test_disable_forgets_the_previous_pairing_response_generation() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.openPairing();
    adapter.events.push_back(peerConnected(8));
    adapter.events.push_back(
        pairingChallenge(8, BluetoothPairingChallengeType::ConfirmComparison, 123456));
    service.update(std::chrono::milliseconds::zero());
    const auto challenge = *service.pairingChallenge();
    const BluetoothPairingResponse response{challenge.generation, challenge.type, true, ""};
    (void)service.respondToPairing(response);

    (void)service.disable();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingResponseResult::NoChallenge),
                            static_cast<unsigned int>(service.respondToPairing(response)));
}

void test_cancel_pairing_rejects_incomplete_peer_and_waits_for_disconnect_to_reconnect() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.openPairing();
    adapter.events.push_back(peerConnected(8));
    service.update(std::chrono::milliseconds::zero());

    const auto result = service.cancelPairing();
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingCancelResult::Cancelled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(1, adapter.startAdvertisingCount);
    adapter.events.push_back(peerDisconnected(8));
    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::seconds(1));
    TEST_ASSERT_EQUAL_INT(2, adapter.startAdvertisingCount);
}

void test_pairing_rejects_every_insecure_completion_and_deletes_a_created_bond() {
    const cardputer_hub::connectivity::BluetoothSecurityProperties insecure[] = {
        {false, true, true, true},
        {true, false, true, true},
        {true, true, false, true},
        {true, true, true, false},
    };
    for (const auto properties : insecure) {
        FakeBluetoothAdapter adapter;
        adapter.bondResult = BluetoothBondQueryResult::Unbonded;
        BluetoothService service(adapter);
        (void)service.enable({"Cardputer Hub"});
        (void)service.openPairing();
        adapter.events.push_back(peerConnected(8));
        adapter.events.push_back(pairingCompleted(8, properties));
        service.update(std::chrono::milliseconds::zero());

        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::Error),
                                static_cast<unsigned int>(service.pairingState()));
        TEST_ASSERT_FALSE(service.completedPairing().has_value());
        if (properties.bonded) {
            TEST_ASSERT_EQUAL_UINT32(1, adapter.deletedPeerBonds.size());
        }
    }
}

void test_successful_pairing_returns_one_opaque_reference_and_keeps_connection() {
    FakeBluetoothAdapter adapter;
    adapter.bondResult = BluetoothBondQueryResult::Unbonded;
    adapter.peerReferences.emplace(8, bond(44));
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.openPairing();
    adapter.events.push_back(peerConnected(8));
    adapter.events.push_back(pairingCompleted(8, {true, true, true, true}));

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingState::Succeeded),
                            static_cast<unsigned int>(service.pairingState()));
    TEST_ASSERT_TRUE(service.completedPairing().has_value());
    TEST_ASSERT_TRUE(*service.completedPairing() == bond(44));
    TEST_ASSERT_EQUAL_UINT32(8, service.currentConnection()->value);
}

void test_full_bond_registry_refuses_pairing_without_eviction() {
    FakeBluetoothAdapter adapter;
    for (std::uint8_t index = 0; index < BluetoothService::maximumBondCount; ++index) {
        adapter.bondListResult.bonds.push_back(bond(index));
    }
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothPairingOpenResult::CapacityReached),
                            static_cast<unsigned int>(service.openPairing()));
    TEST_ASSERT_EQUAL_UINT32(0, adapter.deletedBonds.size());
}

void test_selected_bond_rejects_other_bond_and_reconnects_selected_target() {
    FakeBluetoothAdapter adapter;
    adapter.bondListResult.bonds = {bond(1), bond(2)};
    adapter.peerReferences.emplace(10, bond(1));
    adapter.peerReferences.emplace(20, bond(2));
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothBondSelectionResult::Selected),
                            static_cast<unsigned int>(service.selectBond(bond(2))));
    adapter.events.push_back(peerConnected(10));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_FALSE(service.currentConnection().has_value());
    TEST_ASSERT_EQUAL_UINT32(10, adapter.disconnectedPeers.back().value);
    adapter.events.push_back(peerDisconnected(10));
    service.update(std::chrono::milliseconds::zero());
    service.update(std::chrono::seconds(1));
    adapter.events.push_back(peerConnected(20));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT32(20, service.currentConnection()->value);
}

void test_remove_active_bond_disconnects_first_and_remove_all_reports_partial_failure() {
    FakeBluetoothAdapter adapter;
    adapter.bondListResult.bonds = {bond(1), bond(2)};
    adapter.peerReferences.emplace(10, bond(1));
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    adapter.events.push_back(peerConnected(10));
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothBondRemovalResult::Pending),
                            static_cast<unsigned int>(service.removeBond(bond(1))));
    TEST_ASSERT_EQUAL_UINT32(0, adapter.deletedBonds.size());
    adapter.events.push_back(peerDisconnected(10));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT32(1, adapter.deletedBonds.size());

    adapter.deleteResults = {BluetoothAdapterResult::Success, BluetoothAdapterResult::AdapterError};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothRemoveAllBondsResult::Pending),
                            static_cast<unsigned int>(service.removeAllBonds()));
    TEST_ASSERT_EQUAL_UINT32(1, adapter.deletedBonds.size());
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(BluetoothRemoveAllBondsResult::PartialFailure),
        static_cast<unsigned int>(service.lastRemoveAllResult()));
    TEST_ASSERT_EQUAL_UINT32(3, adapter.deletedBonds.size());
}

void test_inactive_bond_deletion_progresses_only_through_update() {
    FakeBluetoothAdapter adapter;
    adapter.bondListResult.bonds = {bond(3)};
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothBondRemovalResult::Pending),
                            static_cast<unsigned int>(service.removeBond(bond(3))));
    TEST_ASSERT_EQUAL_UINT32(0, adapter.deletedBonds.size());
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT32(1, adapter.deletedBonds.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothBondRemovalResult::Removed),
                            static_cast<unsigned int>(service.lastBondRemovalResult()));
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

void test_hid_requires_selected_authenticated_peer_and_both_subscriptions() {
    FakeBluetoothAdapter adapter;
    const auto selected = bond(7);
    adapter.bondListResult.bonds = {selected};
    adapter.peerReferences[41] = selected;
    BluetoothService service(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.hidTransport().state()));
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(selected);
    adapter.events.push_back(peerConnected(41));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Starting),
                            static_cast<unsigned int>(service.hidTransport().state()));

    adapter.events.push_back(hidReadinessChanged(41, true, false));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Starting),
                            static_cast<unsigned int>(service.hidTransport().state()));

    adapter.events.push_back(hidReadinessChanged(41, true, true, 1, false));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Starting),
                            static_cast<unsigned int>(service.hidTransport().state()));

    adapter.events.push_back(hidReadinessChanged(41, true, true));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Ready),
                            static_cast<unsigned int>(service.hidTransport().state()));

    const auto sendsBeforePairing = adapter.sentHidReports.size();
    (void)service.openPairing();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.hidTransport().state()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::NotReady),
        static_cast<unsigned int>(service.hidTransport().send(HidKeyboardReport{0, {0x04}})));
    TEST_ASSERT_EQUAL_UINT32(sendsBeforePairing, adapter.sentHidReports.size());
}

void test_hid_sends_owned_keyboard_and_consumer_reports_only_to_selected_peer() {
    FakeBluetoothAdapter adapter;
    const auto selected = bond(8);
    adapter.bondListResult.bonds = {selected};
    adapter.peerReferences[42] = selected;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(selected);
    adapter.events.push_back(peerConnected(42));
    adapter.events.push_back(hidReadinessChanged(42, true, true));
    service.update(std::chrono::milliseconds::zero());

    HidKeyboardReport keyboard{0x02, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09}};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.hidTransport().send(keyboard)));
    keyboard.usages.fill(0);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::Sent),
        static_cast<unsigned int>(service.hidTransport().send(HidConsumerReport{0x00E9})));

    TEST_ASSERT_EQUAL_UINT32(2, adapter.sentHidReports.size());
    TEST_ASSERT_EQUAL_UINT32(42, adapter.hidSendPeers[0].value);
    TEST_ASSERT_EQUAL_HEX8(0x09, std::get<HidKeyboardReport>(adapter.sentHidReports[0]).usages[5]);
    TEST_ASSERT_EQUAL_HEX16(0x00E9, std::get<HidConsumerReport>(adapter.sentHidReports[1]).usage);
}

void test_invalid_hid_reports_and_adapter_backpressure_are_checked_without_blocking() {
    FakeBluetoothAdapter adapter;
    const auto selected = bond(9);
    adapter.bondListResult.bonds = {selected};
    adapter.peerReferences[43] = selected;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(selected);
    adapter.events.push_back(peerConnected(43));
    adapter.events.push_back(hidReadinessChanged(43, true, true));
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::AdapterError),
        static_cast<unsigned int>(service.hidTransport().send(HidKeyboardReport{0, {0x04, 0x04}})));
    TEST_ASSERT_TRUE(adapter.sentHidReports.empty());

    adapter.hidSendResult = BluetoothHidAdapterResult::Busy;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::Busy),
        static_cast<unsigned int>(service.hidTransport().send(HidConsumerReport{0x00CD})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Busy),
                            static_cast<unsigned int>(service.hidTransport().state()));
    adapter.hidSendResult = BluetoothHidAdapterResult::Sent;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::Sent),
        static_cast<unsigned int>(service.hidTransport().send(HidConsumerReport::neutral())));
}

void test_release_all_handles_success_disconnect_busy_and_adapter_failure() {
    FakeBluetoothAdapter adapter;
    const auto selected = bond(10);
    adapter.bondListResult.bonds = {selected};
    adapter.peerReferences[44] = selected;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(selected);
    adapter.events.push_back(peerConnected(44));
    adapter.events.push_back(hidReadinessChanged(44, true, true));
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.hidTransport().releaseAll()));
    adapter.hidReleaseResult = BluetoothHidAdapterResult::Busy;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Busy),
                            static_cast<unsigned int>(service.hidTransport().releaseAll()));
    adapter.hidReleaseResult = BluetoothHidAdapterResult::AdapterError;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::AdapterError),
                            static_cast<unsigned int>(service.hidTransport().releaseAll()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(service.hidTransport().state()));

    FakeBluetoothAdapter disconnectedAdapter;
    BluetoothService disconnected(disconnectedAdapter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(disconnected.hidTransport().releaseAll()));
}

void test_hid_ignores_stale_and_wrong_peer_readiness_then_stops_on_current_peer_loss() {
    FakeBluetoothAdapter adapter;
    const auto selected = bond(11);
    adapter.bondListResult.bonds = {selected};
    adapter.peerReferences[45] = selected;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(selected);
    adapter.events.push_back(peerConnected(45));
    adapter.events.push_back(hidReadinessChanged(45, true, true));
    service.update(std::chrono::milliseconds::zero());

    adapter.events.push_back(hidReadinessChanged(46, false, false));
    adapter.events.push_back(hidReadinessChanged(45, false, false, 99));
    adapter.events.push_back(peerConnected(46));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Ready),
                            static_cast<unsigned int>(service.hidTransport().state()));

    adapter.events.push_back(hidReadinessChanged(45, false, true));
    service.update(std::chrono::milliseconds::zero());
    const auto sendsBefore = adapter.sentHidReports.size();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::NotReady),
        static_cast<unsigned int>(service.hidTransport().send(HidKeyboardReport{0, {0x04}})));
    TEST_ASSERT_EQUAL_UINT32(sendsBefore, adapter.sentHidReports.size());
}

void test_hid_releases_before_target_change_and_disable_and_reenable_starts_clean() {
    FakeBluetoothAdapter adapter;
    const auto first = bond(12);
    const auto second = bond(13);
    adapter.bondListResult.bonds = {first, second};
    adapter.peerReferences[47] = first;
    BluetoothService service(adapter);
    (void)service.enable({"Cardputer Hub"});
    (void)service.selectBond(first);
    adapter.events.push_back(peerConnected(47));
    adapter.events.push_back(hidReadinessChanged(47, true, true));
    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothBondSelectionResult::Selected),
                            static_cast<unsigned int>(service.selectBond(second)));
    TEST_ASSERT_EQUAL_UINT32(1, adapter.hidReleasePeers.size());
    TEST_ASSERT_EQUAL_UINT32(47, adapter.hidReleasePeers.front().value);
    TEST_ASSERT_EQUAL_UINT32(1, adapter.disconnectedPeers.size());

    adapter.events.push_back(peerDisconnected(47));
    service.update(std::chrono::milliseconds::zero());
    (void)service.selectBond(first);
    adapter.events.push_back(peerConnected(47, 1));
    adapter.events.push_back(hidReadinessChanged(47, true, true, 1));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Ready),
                            static_cast<unsigned int>(service.hidTransport().state()));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothDisableResult::Disabled),
                            static_cast<unsigned int>(service.disable()));
    TEST_ASSERT_EQUAL_UINT32(2, adapter.hidReleasePeers.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BluetoothEnableResult::Enabled),
                            static_cast<unsigned int>(service.enable({"Cardputer Hub"})));
    adapter.events.push_back(hidReadinessChanged(47, true, true, 1));
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.hidTransport().state()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_construction_has_no_adapter_side_effects);
    RUN_TEST(test_pairing_is_closed_until_explicitly_opened);
    RUN_TEST(test_pairing_requires_enable_is_idempotent_and_times_out_from_advertising_start);
    RUN_TEST(test_open_pairing_disconnects_current_bond_without_deleting_it);
    RUN_TEST(test_pairing_admits_one_unbonded_peer_and_publishes_each_authenticated_challenge);
    RUN_TEST(test_pairing_response_validates_generation_kind_and_six_digit_entry);
    RUN_TEST(test_disable_forgets_the_previous_pairing_response_generation);
    RUN_TEST(test_cancel_pairing_rejects_incomplete_peer_and_waits_for_disconnect_to_reconnect);
    RUN_TEST(test_pairing_rejects_every_insecure_completion_and_deletes_a_created_bond);
    RUN_TEST(test_successful_pairing_returns_one_opaque_reference_and_keeps_connection);
    RUN_TEST(test_full_bond_registry_refuses_pairing_without_eviction);
    RUN_TEST(test_selected_bond_rejects_other_bond_and_reconnects_selected_target);
    RUN_TEST(test_remove_active_bond_disconnects_first_and_remove_all_reports_partial_failure);
    RUN_TEST(test_inactive_bond_deletion_progresses_only_through_update);
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
    RUN_TEST(test_hid_requires_selected_authenticated_peer_and_both_subscriptions);
    RUN_TEST(test_hid_sends_owned_keyboard_and_consumer_reports_only_to_selected_peer);
    RUN_TEST(test_invalid_hid_reports_and_adapter_backpressure_are_checked_without_blocking);
    RUN_TEST(test_release_all_handles_success_disconnect_busy_and_adapter_failure);
    RUN_TEST(test_hid_ignores_stale_and_wrong_peer_readiness_then_stops_on_current_peer_loss);
    RUN_TEST(test_hid_releases_before_target_change_and_disable_and_reenable_starts_clean);
    return UNITY_END();
}
