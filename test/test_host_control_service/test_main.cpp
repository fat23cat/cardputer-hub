#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "connectivity/companion/companion_protocol.h"
#include "core/actions/action_bus.h"
#include "core/capabilities/capability_registry.h"
#include "services/host_control/host_control_service.h"
#include "services/hosts/host_service.h"

using namespace cardputer_hub;
using namespace cardputer_hub::connectivity;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;

namespace {

CompanionPayload encode(const CompanionEnvelope& message) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    CompanionPayload payload{};
    payload.size = encoded->size;
    std::memcpy(payload.bytes.data(), encoded->bytes.data(), encoded->size);
    return payload;
}

class FakeTransport final : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return sendResult;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        const auto payload = incoming.front();
        incoming.pop_front();
        return payload;
    }

    CompanionTransportState transportState = CompanionTransportState::Unavailable;
    CompanionSendResult sendResult = CompanionSendResult::Sent;
    std::vector<CompanionPayload> sent;
    std::deque<CompanionPayload> incoming;
};

BluetoothBondReference bond(unsigned char id) {
    BluetoothBondReference value{};
    value.bytes[0] = id;
    return value;
}

class Memory final : public IStorageAdapter {
  public:
    StorageReadResult read(const StorageAddress&) override {
        return {bytes.empty() ? StorageReadStatus::NotFound : StorageReadStatus::Found, bytes};
    }
    StorageWriteStatus write(const StorageAddress&, const StorageBytes& value) override {
        bytes = value;
        return StorageWriteStatus::Stored;
    }
    StorageRemoveStatus remove(const StorageAddress&) override {
        return StorageRemoveStatus::NotFound;
    }
    StorageBytes bytes;
};

class Adapter final : public IBluetoothAdapter {
  public:
    BluetoothAdapterResult initialize(const BluetoothDeviceConfig&,
                                      std::uint32_t generation) override {
        lifecycle = generation;
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult shutdown() override { return BluetoothAdapterResult::Success; }
    BluetoothAdvertisingResult
    startAdvertising(std::uint32_t generation,
                     std::optional<BluetoothBondReference> = std::nullopt) override {
        events.emplace_back(BluetoothEventType::AdvertisingStarted, BluetoothPeerHandle{},
                            BluetoothFailureClass::Fatal, generation);
        return BluetoothAdvertisingResult::Started;
    }
    BluetoothAdapterResult requestAdvertisingStop() override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothPollResult pollEvent() override {
        if (events.empty())
            return BluetoothPollResult::noEvent();
        auto event = events.front();
        events.pop_front();
        return BluetoothPollResult::withEvent(event);
    }
    BluetoothBondQueryResult bondState(BluetoothPeerHandle) override {
        return BluetoothBondQueryResult::Bonded;
    }
    BluetoothAdapterResult beginPairing(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult restoreBondSecurity(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult respondToPairing(BluetoothPeerHandle, BluetoothPairingChallengeType,
                                            bool, std::optional<std::uint32_t>) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothBondListResult bonds() override { return {BluetoothBondListStatus::Success, known}; }
    BluetoothBondReferenceResult bondReference(BluetoothPeerHandle peer) override {
        return {BluetoothBondReferenceStatus::Found, bond(static_cast<unsigned char>(peer.value))};
    }
    BluetoothAdapterResult deleteBond(const BluetoothBondReference&) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle) override {
        return BluetoothHidAdapterResult::Ready;
    }
    BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle, const HidReport&) override {
        return BluetoothHidAdapterResult::Sent;
    }
    BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle) override {
        return BluetoothHidAdapterResult::Sent;
    }
    BluetoothCompanionAdapterResult companionReadiness(BluetoothPeerHandle) override {
        return BluetoothCompanionAdapterResult::NotReady;
    }
    BluetoothCompanionAdapterResult sendCompanionChunk(BluetoothPeerHandle, const std::uint8_t*,
                                                       std::size_t) override {
        return BluetoothCompanionAdapterResult::NotReady;
    }
    bool receiveCompanionChunk(CompanionChunk&) override { return false; }
    bool takeCompanionIncomingOverflow() override { return false; }

    std::uint32_t lifecycle = 0;
    std::vector<BluetoothBondReference> known{bond(1)};
    std::deque<BluetoothEvent> events;
};

struct Fixture {
    Memory memory;
    Storage storage{memory};
    ConfigurationService config{storage};
    Adapter adapter;
    BluetoothService bluetooth{adapter};
    HostService hosts{bluetooth, config};
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService companion{transport, capabilities};
    HostControlService hostControl{hosts, companion, capabilities};
    ActionBus bus;

    Fixture() {
        TEST_ASSERT_TRUE(hosts.start() == HostResult::Success);
        (void)bus.registerHandler(hostAppActivateActionId, hostControl);
    }

    void selectHost() {
        TEST_ASSERT_TRUE(hosts.selectHost(hosts.settings().hosts.front().id) ==
                         HostResult::Success);
    }

    CompanionEnvelope lastSent() const {
        TEST_ASSERT_FALSE(transport.sent.empty());
        const auto decoded =
            decodeCompanionMessage(transport.sent.back().bytes.data(), transport.sent.back().size);
        TEST_ASSERT_TRUE(decoded.has_value());
        return *decoded;
    }

    CompanionEnvelope sentAt(std::size_t index) const {
        TEST_ASSERT_TRUE(index < transport.sent.size());
        const auto decoded =
            decodeCompanionMessage(transport.sent[index].bytes.data(), transport.sent[index].size);
        TEST_ASSERT_TRUE(decoded.has_value());
        return *decoded;
    }

    void completeHandshake() {
        transport.transportState = CompanionTransportState::Ready;
        const auto sentBefore = transport.sent.size();
        const std::uint8_t versions[] = {1};
        transport.incoming.push_back(encode(makeHello(versions, 1)));
        companion.update(std::chrono::milliseconds::zero());
        TEST_ASSERT_TRUE(transport.sent.size() >= sentBefore + 2);
        const auto ack = sentAt(sentBefore);
        const auto capsRequest = sentAt(sentBefore + 1);
        auto capabilitiesResponse =
            makeResponse(ack.session, capsRequest.requestId, CompanionOperation::Capabilities,
                         CompanionStatus::Ok);
        const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                           CompanionCapability::AppActivate,
                                           CompanionCapability::AppActiveEvents};
        TEST_ASSERT_TRUE(setCapabilityList(capabilitiesResponse, ids, 3));
        transport.incoming.push_back(encode(capabilitiesResponse));
        companion.update(std::chrono::milliseconds::zero());
        const auto activeRequest = lastSent();
        auto activeResponse = makeResponse(ack.session, activeRequest.requestId,
                                           CompanionOperation::AppActive, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setBundleIdentifier(activeResponse, "dev.zed.Zed"));
        transport.incoming.push_back(encode(activeResponse));
        companion.update(std::chrono::milliseconds::zero());
        TEST_ASSERT_TRUE(companion.hasLiveCompanion());
        while (companion.takeCompletedRequest().has_value()) {
        }
    }

    Action activate(const char* bundleId) {
        return {hostAppActivateActionId,
                "mac-control",
                {{hostAppActivateBundleParameter, std::string(bundleId)}}};
    }

    void completeActivate(CompanionStatus status) {
        const auto request = lastSent();
        transport.incoming.push_back(encode(makeResponse(companion.session(), request.requestId,
                                                         CompanionOperation::AppActivate, status)));
        companion.update(std::chrono::milliseconds::zero());
        hostControl.update();
    }
};

void test_valid_activate_submits_companion_request() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    const auto sent = f.lastSent();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionOperation::AppActivate),
                            static_cast<unsigned>(sent.operation));
    char bundle[64]{};
    std::uint8_t length = 0;
    TEST_ASSERT_TRUE(readBundleIdentifier(sent, bundle, sizeof(bundle), length));
    TEST_ASSERT_EQUAL_STRING("com.tdesktop.Telegram", bundle);
}

void test_missing_or_invalid_bundle_id_is_rejected() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(f.bus.dispatch({hostAppActivateActionId, "mac-control", {}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(f.bus.dispatch(
            {hostAppActivateActionId, "mac-control", {{"bundleId", std::int32_t{1}}}})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DispatchResult::Rejected),
                            static_cast<unsigned>(f.bus.dispatch(f.activate(""))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Idle),
                            static_cast<unsigned>(f.hostControl.status().state));
}

void test_missing_host_or_companion_is_rejected() {
    Fixture f;
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    Fixture g;
    g.selectHost();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(g.bus.dispatch(g.activate("com.tdesktop.Telegram"))));
}

void test_companion_busy_is_rejected() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(f.companion.activateApplication("a.b")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(f.companion.activateApplication("c.d")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(f.companion.activateApplication("e.f")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionSubmitResult::Submitted),
                            static_cast<unsigned>(f.companion.activateApplication("g.h")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
}

void test_ok_and_not_found_completions() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    const auto generation = f.hostControl.status().generation;
    f.completeActivate(CompanionStatus::Ok);
    TEST_ASSERT_EQUAL_UINT32(generation, f.hostControl.status().generation);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Succeeded),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlFailure::None),
                            static_cast<unsigned>(f.hostControl.status().failure));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DispatchResult::Handled),
                            static_cast<unsigned>(f.bus.dispatch(f.activate("missing.app"))));
    f.completeActivate(CompanionStatus::NotFound);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlFailure::NotFound),
                            static_cast<unsigned>(f.hostControl.status().failure));
}

void test_timeout_and_transport_loss() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    f.companion.update(std::chrono::seconds(2));
    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlFailure::Timeout),
                            static_cast<unsigned>(f.hostControl.status().failure));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update(std::chrono::milliseconds::zero());
    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlFailure::Unavailable),
                            static_cast<unsigned>(f.hostControl.status().failure));
}

void test_disconnect_clears_pending_and_reconnect_does_not_replay() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    const auto sentBefore = f.transport.sent.size();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.companion.update(std::chrono::milliseconds::zero());
    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    const auto sentAfterLoss = f.transport.sent.size();
    TEST_ASSERT_EQUAL_UINT32(sentBefore + 1, sentAfterLoss);
    f.completeHandshake();
    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    bool replayed = false;
    for (std::size_t index = sentAfterLoss; index < f.transport.sent.size(); ++index) {
        if (f.sentAt(index).operation == CompanionOperation::AppActivate &&
            f.sentAt(index).kind == CompanionKind::Request)
            replayed = true;
    }
    TEST_ASSERT_FALSE(replayed);
}

void test_second_command_is_rejected_while_pending() {
    Fixture f;
    f.selectHost();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Handled),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(DispatchResult::Rejected),
        static_cast<unsigned>(f.bus.dispatch(f.activate("com.tdesktop.Telegram"))));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_activate_submits_companion_request);
    RUN_TEST(test_missing_or_invalid_bundle_id_is_rejected);
    RUN_TEST(test_missing_host_or_companion_is_rejected);
    RUN_TEST(test_companion_busy_is_rejected);
    RUN_TEST(test_ok_and_not_found_completions);
    RUN_TEST(test_timeout_and_transport_loss);
    RUN_TEST(test_disconnect_clears_pending_and_reconnect_does_not_replay);
    RUN_TEST(test_second_command_is_rejected_while_pending);
    return UNITY_END();
}
