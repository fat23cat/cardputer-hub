#include "apps/hosts/assets/micro5_digits.h"
#include "apps/hosts/host_settings.h"
#include "apps/shell/application_shell.h"
#include "core/display/palette.h"
#include "services/hosts/host_service.h"
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <unity.h>

using namespace cardputer_hub;
using namespace cardputer_hub::connectivity;
namespace {
BluetoothBondReference bond(unsigned char id) {
    BluetoothBondReference b{};
    b.bytes[0] = id;
    return b;
}
class Memory final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {bytes.empty() ? core::StorageReadStatus::NotFound : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        ++writes;
        if (failWrite)
            return core::StorageWriteStatus::BackendError;
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
    bool failWrite = false;
    int writes = 0;
};
class Adapter final : public IBluetoothAdapter {
  public:
    BluetoothAdapterResult initialize(const BluetoothDeviceConfig&,
                                      std::uint32_t generation) override {
        lifecycle = generation;
        trace.push_back("initialize");
        return failInitialize ? BluetoothAdapterResult::AdapterError
                              : BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult shutdown() override {
        trace.push_back("shutdown");
        events.clear();
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdvertisingResult
    startAdvertising(std::uint32_t generation,
                     std::optional<BluetoothBondReference> target = std::nullopt) override {
        trace.push_back("advertise");
        targets.push_back(target);
        events.emplace_back(BluetoothEventType::AdvertisingStarted, BluetoothPeerHandle{},
                            BluetoothFailureClass::Fatal, generation);
        return BluetoothAdvertisingResult::Started;
    }
    BluetoothAdapterResult requestAdvertisingStop() override {
        trace.push_back("stop");
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle) override {
        trace.push_back("disconnect");
        return BluetoothAdapterResult::Success;
    }
    BluetoothPollResult pollEvent() override {
        if (events.empty())
            return BluetoothPollResult::noEvent();
        auto e = events.front();
        events.pop_front();
        return BluetoothPollResult::withEvent(e);
    }
    BluetoothBondQueryResult bondState(BluetoothPeerHandle peer) override {
        return std::find(known.begin(), known.end(),
                         bond(static_cast<unsigned char>(peer.value))) != known.end()
                   ? BluetoothBondQueryResult::Bonded
                   : BluetoothBondQueryResult::Unbonded;
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
    BluetoothAdapterResult deleteBond(const BluetoothBondReference& reference) override {
        if (failDelete)
            return BluetoothAdapterResult::AdapterError;
        known.erase(std::remove(known.begin(), known.end(), reference), known.end());
        ++deletions;
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle) override {
        ++deletions;
        return BluetoothAdapterResult::Success;
    }
    BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle) override {
        return BluetoothHidAdapterResult::Ready;
    }
    BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle, const HidReport&) override {
        return BluetoothHidAdapterResult::Sent;
    }
    BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle) override {
        trace.push_back("release");
        return BluetoothHidAdapterResult::Sent;
    }
    void connect(unsigned char id) {
        events.emplace_back(BluetoothEventType::PeerConnected, BluetoothPeerHandle{id},
                            BluetoothFailureClass::Fatal, lifecycle);
    }
    std::uint32_t lifecycle = 0;
    int deletions = 0;
    bool failDelete = false;
    bool failInitialize = false;
    std::vector<BluetoothBondReference> known{bond(1), bond(2)};
    std::vector<std::optional<BluetoothBondReference>> targets;
    std::vector<std::string> trace;
    std::deque<BluetoothEvent> events;
};
struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService config{storage};
    Adapter adapter;
    BluetoothService bluetooth{adapter};
    services::HostService hosts{bluetooth, config};
};
void test_host_actions_retry_failed_startup_without_activating_previous_target() {
    Fixture saved;
    TEST_ASSERT_TRUE(saved.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(saved.hosts.selectHost(1) == services::HostResult::Success);
    const std::vector<core::Action> actions{{"host.bluetooth", "test", {{"enabled", false}}},
                                            {"host.select", "test", {{"id", std::int32_t{2}}}},
                                            {"host.pair", "test", {}}};
    for (const auto& action : actions) {
        Fixture f;
        f.memory.bytes = saved.memory.bytes;
        f.adapter.failInitialize = true;
        TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::BluetoothError);
        const auto persisted = f.memory.bytes;
        TEST_ASSERT_TRUE(f.hosts.handle(action) == core::ActionHandlingResult::Rejected);
        TEST_ASSERT_TRUE(f.hosts.lastResult() == services::HostResult::BluetoothError);
        TEST_ASSERT_TRUE(f.memory.bytes == persisted);
        f.adapter.failInitialize = false;
        TEST_ASSERT_TRUE(f.hosts.handle(action) == core::ActionHandlingResult::Handled);
        TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
        TEST_ASSERT_EQUAL(0, f.adapter.deletions);
        if (action.id == "host.bluetooth") {
            TEST_ASSERT_TRUE(f.adapter.targets.empty());
            TEST_ASSERT_FALSE(f.hosts.settings().bluetoothEnabled);
        } else {
            TEST_ASSERT_EQUAL_UINT(1, f.adapter.targets.size());
            TEST_ASSERT_TRUE(f.adapter.targets.front() ==
                             (action.id == "host.pair" ? std::nullopt : std::optional{bond(2)}));
        }
    }
}

void test_corrupt_configuration_is_not_overwritten_by_startup_retry() {
    Fixture f;
    f.memory.bytes = {1, 2, 3};
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::StorageError);
    const auto persisted = f.memory.bytes;
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::StorageError);
    TEST_ASSERT_TRUE(f.memory.bytes == persisted);
    TEST_ASSERT_TRUE(f.adapter.trace.empty());
}

void test_interrupted_pairing_invalidates_cached_challenge_for_all_prompt_types() {
    for (const auto type : {BluetoothPairingChallengeType::DisplayPasskey,
                            BluetoothPairingChallengeType::EnterPasskey,
                            BluetoothPairingChallengeType::ConfirmComparison}) {
        for (const bool replacementQueued : {false, true}) {
            Fixture f;
            TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
            TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::Success);
            f.adapter.connect(3);
            f.hosts.update(std::chrono::milliseconds(1));
            BluetoothEvent challenge{BluetoothEventType::PairingChallenge,
                                     {3},
                                     BluetoothFailureClass::Fatal,
                                     f.adapter.lifecycle};
            challenge.challengeType = type;
            if (type != BluetoothPairingChallengeType::EnterPasskey)
                challenge.challengeValue = 123456;
            f.adapter.events.push_back(challenge);
            f.hosts.update(std::chrono::milliseconds(1));
            TEST_ASSERT_TRUE(f.hosts.pairingChallenge().has_value());
            const auto generation = f.hosts.pairingChallenge()->generation;
            f.hosts.update(std::chrono::milliseconds(1));
            TEST_ASSERT_TRUE(f.hosts.pairingChallenge().has_value());
            f.adapter.events.emplace_back(BluetoothEventType::PeerDisconnected,
                                          BluetoothPeerHandle{3}, BluetoothFailureClass::Fatal,
                                          f.adapter.lifecycle);
            if (replacementQueued)
                f.adapter.connect(4);
            f.hosts.update(std::chrono::milliseconds(1));
            TEST_ASSERT_TRUE(f.hosts.pairing());
            TEST_ASSERT_FALSE(f.hosts.pairingChallenge().has_value());
            TEST_ASSERT_EQUAL(0, f.adapter.deletions);
            if (!replacementQueued) {
                f.hosts.update(std::chrono::seconds(1));
                TEST_ASSERT_TRUE(f.hosts.pairingState() == BluetoothPairingState::Advertising);
                f.adapter.connect(4);
                f.hosts.update(std::chrono::milliseconds(1));
            }
            challenge.peer = {4};
            f.adapter.events.push_back(challenge);
            f.hosts.update(std::chrono::milliseconds(1));
            TEST_ASSERT_TRUE(f.hosts.pairingChallenge().has_value());
            TEST_ASSERT_NOT_EQUAL(generation, f.hosts.pairingChallenge()->generation);
            f.hosts.update(std::chrono::seconds(120));
            TEST_ASSERT_FALSE(f.hosts.pairing());
            TEST_ASSERT_FALSE(f.hosts.pairingChallenge().has_value());
        }
    }
}

void test_import_existing_pairs_without_advertising_then_persist_selected_host() {
    Fixture f;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    for (const auto& host : f.hosts.settings().hosts) {
        TEST_ASSERT_FALSE(host.platform.has_value());
        TEST_ASSERT_TRUE(host.capabilities.empty());
        TEST_ASSERT_FALSE(host.mappingTemplate.has_value());
    }
    TEST_ASSERT_TRUE(f.adapter.targets.empty());
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
    const auto id = f.hosts.settings().hosts[1].id;
    TEST_ASSERT_TRUE(f.hosts.selectHost(id) == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(2));
    services::ConfigurationService reloaded(f.storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reloaded.value().activeHost == id);
    TEST_ASSERT_TRUE(reloaded.value().bluetoothEnabled);
}
void test_switch_releases_old_host_before_shutdown_and_only_advertises_new_target() {
    Fixture f;
    f.hosts.start();
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    f.hosts.selectHost(f.hosts.settings().hosts[0].id);
    f.adapter.connect(1);
    f.hosts.update(std::chrono::milliseconds(1));
    f.adapter.trace.clear();
    TEST_ASSERT_TRUE(f.hosts.selectHost(f.hosts.settings().hosts[1].id) ==
                     services::HostResult::Success);
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(2));
    const auto release = std::find(f.adapter.trace.begin(), f.adapter.trace.end(), "release");
    const auto disconnect = std::find(f.adapter.trace.begin(), f.adapter.trace.end(), "disconnect");
    const auto shutdown = std::find(f.adapter.trace.begin(), f.adapter.trace.end(), "shutdown");
    const auto advertise = std::find(f.adapter.trace.begin(), f.adapter.trace.end(), "advertise");
    TEST_ASSERT_TRUE(release < disconnect && disconnect < shutdown && shutdown < advertise);
    TEST_ASSERT_EQUAL(0, f.adapter.deletions);
}
void test_off_survives_restart_and_failed_selection_is_closed_without_fallback() {
    Fixture f;
    f.hosts.start();
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    const auto one = f.hosts.settings().hosts[0].id;
    f.hosts.selectHost(one);
    TEST_ASSERT_TRUE(f.hosts.setEnabled(false) == services::HostResult::Success);
    Fixture reboot;
    reboot.memory.bytes = f.memory.bytes;
    TEST_ASSERT_TRUE(reboot.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(reboot.adapter.targets.empty());
    TEST_ASSERT_TRUE(reboot.hosts.settings().activeHost == one);
    reboot.hosts.setEnabled(true);
    reboot.memory.failWrite = true;
    TEST_ASSERT_TRUE(reboot.hosts.selectHost(reboot.hosts.settings().hosts[1].id) ==
                     services::HostResult::StorageError);
    TEST_ASSERT_TRUE(reboot.hosts.bluetoothState() == BluetoothState::Disabled);
    TEST_ASSERT_TRUE(reboot.hosts.settings().activeHost == one);
}
void test_pairing_cancel_restores_selected_host_without_deleting_pairs() {
    Fixture f;
    f.hosts.start();
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    f.hosts.selectHost(f.hosts.settings().hosts[1].id);
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.pairing());
    TEST_ASSERT_FALSE(f.adapter.targets.back().has_value());
    TEST_ASSERT_TRUE(f.hosts.cancelPairing() == services::HostResult::Success);
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(2));
    TEST_ASSERT_EQUAL(0, f.adapter.deletions);
}
void test_completed_pair_is_saved_once_and_missing_selected_bond_never_falls_back() {
    Fixture f;
    f.hosts.start();
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::Success);
    f.adapter.connect(3);
    f.hosts.update(std::chrono::milliseconds(1));
    f.adapter.known.push_back(bond(3));
    BluetoothEvent done{BluetoothEventType::PairingCompleted,
                        {3},
                        BluetoothFailureClass::Fatal,
                        f.adapter.lifecycle};
    done.security = {true, true, true, true};
    f.adapter.events.push_back(done);
    f.hosts.update(std::chrono::milliseconds(1));
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_EQUAL_UINT(3, f.hosts.settings().hosts.size());
    TEST_ASSERT_TRUE(f.hosts.settings().activeHost == f.hosts.settings().hosts.back().id);
    TEST_ASSERT_TRUE(f.bluetooth.selectedBond() == bond(3));
    TEST_ASSERT_FALSE(f.hosts.settings().hosts.back().platform.has_value());
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.back().capabilities.empty());
    TEST_ASSERT_FALSE(f.hosts.settings().hosts.back().mappingTemplate.has_value());
    const auto saved = f.memory.bytes;
    f.hosts.update(std::chrono::seconds(1));
    TEST_ASSERT_TRUE(saved == f.memory.bytes);
    Fixture reboot;
    reboot.memory.bytes = saved;
    TEST_ASSERT_TRUE(reboot.hosts.start() == services::HostResult::MissingBond);
    TEST_ASSERT_TRUE(reboot.adapter.targets.empty());
    TEST_ASSERT_TRUE(reboot.bluetooth.state() == BluetoothState::Disabled);
}
void test_timeout_returns_to_saved_off_and_rename_preserves_identity() {
    Fixture f;
    f.hosts.start();
    const auto id = f.hosts.settings().hosts.front().id;
    TEST_ASSERT_TRUE(f.hosts.setHostPlatform(id, "macos") == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.setHostCapability(id, "app.activate", true) ==
                     services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.setHostMappingTemplate(id, "macos.default") ==
                     services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.renameHost(id, "Notebook") == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().bond == bond(1));
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().platform ==
                     std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().capabilities ==
                     std::vector<std::string>{"app.activate"});
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().mappingTemplate ==
                     std::optional<std::string>{"macos.default"});
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::Success);
    f.hosts.update(std::chrono::milliseconds(1));
    f.hosts.update(std::chrono::seconds(120));
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_TRUE(f.bluetooth.state() == BluetoothState::Disabled);
    TEST_ASSERT_FALSE(f.hosts.settings().bluetoothEnabled);
    TEST_ASSERT_EQUAL(0, f.adapter.deletions);
}
void test_actions_preserve_selected_host_on_restart_and_reject_wrong_parameter_types() {
    Fixture f;
    f.hosts.start();
    core::ActionBus bus;
    bus.registerHandler("host.select", f.hosts);
    bus.registerHandler("host.bluetooth", f.hosts);
    const auto id = static_cast<std::int32_t>(f.hosts.settings().hosts[1].id);
    TEST_ASSERT_TRUE(bus.dispatch({"host.select", "test", {{"id", id}}}) ==
                     core::DispatchResult::Handled);
    Fixture reboot;
    reboot.memory.bytes = f.memory.bytes;
    TEST_ASSERT_TRUE(reboot.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_EQUAL_UINT(1, reboot.adapter.targets.size());
    TEST_ASSERT_TRUE(reboot.adapter.targets.back() == bond(2));
    const auto trace = f.adapter.trace;
    TEST_ASSERT_TRUE(
        bus.dispatch({"host.bluetooth", "test", {{"enabled", std::string("false")}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_TRUE(f.adapter.trace == trace);
    TEST_ASSERT_TRUE(bus.dispatch({"host.bluetooth", "test", {{"enabled", false}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
}

void test_metadata_actions_persist_without_touching_the_ready_selected_host() {
    Fixture f;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    const auto id = f.hosts.settings().hosts.front().id;
    TEST_ASSERT_TRUE(f.hosts.selectHost(id) == services::HostResult::Success);
    f.adapter.connect(1);
    f.hosts.update(std::chrono::milliseconds(1));
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Connected);
    f.adapter.trace.clear();

    core::ActionBus bus;
    for (const auto* action : {"host.platform", "host.capability", "host.mapping-template"})
        bus.registerHandler(action, f.hosts);
    const auto writesBefore = f.memory.writes;
    TEST_ASSERT_TRUE(bus.dispatch({"host.platform",
                                   "test",
                                   {{"id", static_cast<std::int32_t>(id)},
                                    {"platform", std::string{"macos"}}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(bus.dispatch({"host.capability",
                                   "test",
                                   {{"id", static_cast<std::int32_t>(id)},
                                    {"capability", std::string{"app.activate"}},
                                    {"enabled", true}}}) == core::DispatchResult::Handled);
    const auto writesAfterCapability = f.memory.writes;
    TEST_ASSERT_TRUE(bus.dispatch({"host.capability",
                                   "test",
                                   {{"id", static_cast<std::int32_t>(id)},
                                    {"capability", std::string{"app.activate"}},
                                    {"enabled", true}}}) == core::DispatchResult::Handled);
    TEST_ASSERT_EQUAL(writesAfterCapability, f.memory.writes);
    TEST_ASSERT_TRUE(bus.dispatch({"host.mapping-template",
                                   "test",
                                   {{"id", static_cast<std::int32_t>(id)},
                                    {"template", std::string{"macos.default"}}}}) ==
                     core::DispatchResult::Handled);

    const auto& host = f.hosts.settings().hosts.front();
    TEST_ASSERT_TRUE(host.platform == std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(host.capabilities == std::vector<std::string>{"app.activate"});
    TEST_ASSERT_TRUE(host.mappingTemplate == std::optional<std::string>{"macos.default"});
    TEST_ASSERT_TRUE(f.hosts.settings().activeHost == id);
    TEST_ASSERT_TRUE(f.hosts.settings().bluetoothEnabled);
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Connected);
    TEST_ASSERT_TRUE(f.adapter.trace.empty());
    TEST_ASSERT_EQUAL(writesBefore + 3, f.memory.writes);
    const auto writesAfterMetadata = f.memory.writes;
    TEST_ASSERT_TRUE(f.hosts.setHostPlatform(id, "macos") == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.setHostMappingTemplate(id, "macos.default") ==
                     services::HostResult::Success);
    TEST_ASSERT_EQUAL(writesAfterMetadata, f.memory.writes);

    services::ConfigurationService reloaded(f.storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reloaded.value().hosts.front().platform ==
                     std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(reloaded.value().hosts.front().capabilities ==
                     std::vector<std::string>{"app.activate"});
    TEST_ASSERT_TRUE(reloaded.value().hosts.front().mappingTemplate ==
                     std::optional<std::string>{"macos.default"});

    TEST_ASSERT_TRUE(
        bus.dispatch({"host.platform",
                      "test",
                      {{"id", static_cast<std::int32_t>(id)}, {"platform", std::string{}}}}) ==
        core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(bus.dispatch({"host.capability",
                                   "test",
                                   {{"id", static_cast<std::int32_t>(id)},
                                    {"capability", std::string{"app.activate"}},
                                    {"enabled", false}}}) == core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(
        bus.dispatch({"host.mapping-template",
                      "test",
                      {{"id", static_cast<std::int32_t>(id)}, {"template", std::string{}}}}) ==
        core::DispatchResult::Handled);
    TEST_ASSERT_FALSE(f.hosts.settings().hosts.front().platform.has_value());
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().capabilities.empty());
    TEST_ASSERT_FALSE(f.hosts.settings().hosts.front().mappingTemplate.has_value());
    TEST_ASSERT_TRUE(f.adapter.trace.empty());
}

void test_invalid_or_failed_metadata_actions_preserve_configuration_and_radio_state() {
    Fixture f;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    const auto id = f.hosts.settings().hosts.front().id;
    TEST_ASSERT_TRUE(f.hosts.selectHost(id) == services::HostResult::Success);
    f.adapter.connect(1);
    f.hosts.update(std::chrono::milliseconds(1));
    f.adapter.trace.clear();
    const auto stored = f.memory.bytes;
    const auto writes = f.memory.writes;

    const std::vector<core::Action> invalid{
        {"host.platform",
         "test",
         {{"id", static_cast<std::int32_t>(id)}, {"platform", std::string{"Mac OS"}}}},
        {"host.platform", "test", {{"id", std::string{"1"}}, {"platform", std::string{"macos"}}}},
        {"host.capability",
         "test",
         {{"id", static_cast<std::int32_t>(id)},
          {"capability", std::string{}},
          {"enabled", false}}},
        {"host.capability",
         "test",
         {{"id", static_cast<std::int32_t>(id)},
          {"capability", std::string{"app.activate"}},
          {"enabled", std::string{"true"}}}},
        {"host.mapping-template",
         "test",
         {{"id", std::int32_t{999}}, {"template", std::string{"macos.default"}}}},
    };
    for (const auto& action : invalid)
        TEST_ASSERT_TRUE(f.hosts.handle(action) == core::ActionHandlingResult::Rejected);
    TEST_ASSERT_EQUAL(writes, f.memory.writes);
    TEST_ASSERT_TRUE(f.memory.bytes == stored);
    TEST_ASSERT_TRUE(f.adapter.trace.empty());

    f.memory.failWrite = true;
    TEST_ASSERT_TRUE(f.hosts.handle({"host.platform",
                                     "test",
                                     {{"id", static_cast<std::int32_t>(id)},
                                      {"platform", std::string{"macos"}}}}) ==
                     core::ActionHandlingResult::Rejected);
    TEST_ASSERT_TRUE(f.hosts.lastResult() == services::HostResult::StorageError);
    TEST_ASSERT_FALSE(f.hosts.settings().hosts.front().platform.has_value());
    TEST_ASSERT_TRUE(f.memory.bytes == stored);
    TEST_ASSERT_TRUE(f.hosts.settings().activeHost == id);
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Connected);
    TEST_ASSERT_TRUE(f.adapter.trace.empty());
}

void test_metadata_operations_are_bounded_and_do_not_start_bluetooth() {
    Fixture f;
    services::HostConfiguration value;
    value.hosts = {{1, "Host 1", bond(1), std::nullopt, {}, std::nullopt}};
    value.nextHostId = 2;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.adapter.trace.clear();

    const auto writesBefore = f.memory.writes;
    TEST_ASSERT_TRUE(f.hosts.setHostPlatform(1, "vendor.future") == services::HostResult::Success);
    for (std::size_t index = 0; index < services::ConfigurationService::maximumHostCapabilityCount;
         ++index) {
        TEST_ASSERT_TRUE(f.hosts.setHostCapability(1, "capability." + std::to_string(index),
                                                   true) == services::HostResult::Success);
    }
    const auto writesAtCapacity = f.memory.writes;
    TEST_ASSERT_TRUE(f.hosts.setHostCapability(1, "capability.overflow", true) ==
                     services::HostResult::CapacityReached);
    TEST_ASSERT_EQUAL(writesAtCapacity, f.memory.writes);
    TEST_ASSERT_TRUE(f.hosts.setHostPlatform(999, "macos") == services::HostResult::InvalidInput);
    TEST_ASSERT_EQUAL(writesAtCapacity, f.memory.writes);
    TEST_ASSERT_EQUAL(writesBefore + 17, f.memory.writes);
    TEST_ASSERT_TRUE(f.adapter.trace.empty());
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
}

void test_full_profile_list_rejects_pairing_without_interrupting_selected_host() {
    Fixture f;
    f.adapter.known.clear();
    for (unsigned char id = 1; id <= BluetoothService::maximumBondCount; ++id)
        f.adapter.known.push_back(bond(id));
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    f.hosts.selectHost(f.hosts.settings().hosts.front().id);
    const auto trace = f.adapter.trace;
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::CapacityReached);
    TEST_ASSERT_TRUE(f.adapter.trace == trace);
    TEST_ASSERT_FALSE(f.hosts.pairing());
}

class Display final : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor) override {
        texts.clear();
        ink.clear();
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        if (color.red == core::palette::ink.red && color.green == core::palette::ink.green &&
            color.blue == core::palette::ink.blue) {
            for (int y = position.y; y < position.y + height; ++y)
                for (int x = position.x; x < position.x + width; ++x)
                    ink.emplace(x, y);
        }
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0);
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        texts.emplace_back(value);
    }
    std::vector<std::string> texts;
    std::set<std::pair<int, int>> ink;
};

struct Screen {
    explicit Screen(Fixture& f) : ui(f.hosts, bus, display), shell(f.hosts, bus, display, ui) {
        for (const auto* id : {"host.bluetooth", "host.select", "host.pair", "host.cancel-pairing",
                               "host.delete", "host.rename"})
            bus.registerHandler(id, f.hosts);
    }
    void openBluetooth() {
        shell.update({{core::InputEventType::NamedKey,
                       0,
                       core::NamedKey::Tab,
                       {false, false, false, false, true}}});
        press(core::NamedKey::Enter);
    }
    void press(core::NamedKey key) { shell.update({{core::InputEventType::NamedKey, 0, key, {}}}); }
    bool shows(const char* text) const {
        return std::find(display.texts.begin(), display.texts.end(), text) != display.texts.end();
    }
    core::ActionBus bus;
    Display display;
    apps::HostSettings ui;
    apps::ApplicationShell shell;
};

void test_host_menu_rename_and_confirmed_delete_affect_only_that_host() {
    Fixture f;
    f.hosts.start();
    const auto first = f.hosts.settings().hosts.front().id;
    const auto second = f.hosts.settings().hosts.back().id;
    Screen screen(f);
    screen.openBluetooth();
    screen.press(core::NamedKey::Down);
    screen.press(core::NamedKey::Down);
    const auto saved = f.memory.bytes;
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_TRUE(screen.shows("Rename"));
    TEST_ASSERT_TRUE(screen.shows("Delete"));
    TEST_ASSERT_TRUE(f.memory.bytes == saved);
    screen.press(core::NamedKey::Down); // Rename
    screen.press(core::NamedKey::Enter);
    screen.shell.update({{core::InputEventType::PrintableCharacter, 'x', {}, {}}});
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_EQUAL_STRING("Host 1x", f.hosts.settings().hosts.front().name.c_str());
    screen.press(core::NamedKey::Down); // Delete
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_TRUE(screen.shows("DELETE HOST"));
    TEST_ASSERT_EQUAL(0, f.adapter.deletions);
    screen.press(core::NamedKey::Escape);
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    screen.press(core::NamedKey::Enter);
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_EQUAL(1, f.adapter.deletions);
    TEST_ASSERT_EQUAL_UINT(1, f.hosts.settings().hosts.size());
    TEST_ASSERT_EQUAL(second, f.hosts.settings().hosts.front().id);
    TEST_ASSERT_TRUE(f.adapter.known == std::vector<BluetoothBondReference>{bond(2)});
    TEST_ASSERT_TRUE(screen.shows("BLUETOOTH"));
    services::ConfigurationService reloaded(f.storage);
    reloaded.load();
    TEST_ASSERT_EQUAL(second, reloaded.value().hosts.front().id);
    TEST_ASSERT_TRUE(reloaded.value().nextHostId > first);
}

void test_delete_selected_stays_off_and_nonselected_preserves_live_host() {
    Fixture f;
    f.hosts.start();
    const auto first = f.hosts.settings().hosts.front().id;
    const auto second = f.hosts.settings().hosts.back().id;
    f.hosts.selectHost(first);
    f.adapter.connect(1);
    f.hosts.update(std::chrono::milliseconds(1));
    TEST_ASSERT_TRUE(f.hosts.setHostPlatform(first, "macos") == services::HostResult::Success);
    const auto trace = f.adapter.trace;
    TEST_ASSERT_TRUE(
        f.hosts.handle({"host.delete", "test", {{"id", static_cast<std::int32_t>(second)}}}) ==
        core::ActionHandlingResult::Handled);
    TEST_ASSERT_TRUE(f.adapter.trace == trace);
    TEST_ASSERT_TRUE(f.hosts.settings().activeHost == first);
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.front().platform ==
                     std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Connected);
    TEST_ASSERT_TRUE(
        f.hosts.handle({"host.delete", "test", {{"id", static_cast<std::int32_t>(first)}}}) ==
        core::ActionHandlingResult::Handled);
    TEST_ASSERT_TRUE(f.hosts.settings().hosts.empty());
    TEST_ASSERT_FALSE(f.hosts.settings().activeHost.has_value());
    TEST_ASSERT_FALSE(f.hosts.settings().bluetoothEnabled);
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
}

void test_delete_failure_preserves_profile_and_missing_bond_can_be_removed() {
    Fixture f;
    f.hosts.start();
    const auto first = static_cast<std::int32_t>(f.hosts.settings().hosts.front().id);
    const core::Action remove{"host.delete", "test", {{"id", first}}};
    f.memory.failWrite = true;
    f.hosts.handle(remove);
    TEST_ASSERT_TRUE(f.hosts.lastResult() == services::HostResult::StorageError);
    TEST_ASSERT_EQUAL(0, f.adapter.deletions);
    f.memory.failWrite = false;
    f.adapter.failDelete = true;
    f.hosts.handle(remove);
    TEST_ASSERT_TRUE(f.hosts.lastResult() == services::HostResult::BluetoothError);
    TEST_ASSERT_EQUAL_UINT(2, f.hosts.settings().hosts.size());
    f.adapter.failDelete = false;
    f.adapter.known.erase(f.adapter.known.begin()); // Already missing on device.
    TEST_ASSERT_TRUE(f.hosts.handle(remove) == core::ActionHandlingResult::Handled);
    TEST_ASSERT_EQUAL_UINT(1, f.hosts.settings().hosts.size());
}

void test_first_enable_guides_to_host_and_next_enter_selects_it() {
    Fixture f;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    const auto saved = f.memory.bytes;
    const auto trace = f.adapter.trace;
    Screen screen(f);
    screen.openBluetooth();              // Open panel, do not activate its first row.
    screen.press(core::NamedKey::Enter); // Enable with no selected host.
    TEST_ASSERT_TRUE(screen.shows("Select host, then Enter"));
    TEST_ASSERT_FALSE(f.hosts.settings().activeHost.has_value());
    TEST_ASSERT_TRUE(f.memory.bytes == saved);
    TEST_ASSERT_TRUE(f.adapter.trace == trace);
    screen.press(core::NamedKey::Enter); // Open focused host menu.
    screen.press(core::NamedKey::Enter); // Connect.
    TEST_ASSERT_TRUE(f.hosts.settings().activeHost == f.hosts.settings().hosts.front().id);
    TEST_ASSERT_TRUE(f.hosts.settings().bluetoothEnabled);
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(1));
    TEST_ASSERT_TRUE(f.hosts.lastResult() == services::HostResult::Success);
    screen.press(core::NamedKey::Up);
    screen.press(core::NamedKey::Up);
    screen.press(core::NamedKey::Enter); // Off retains selection.
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
    screen.press(core::NamedKey::Enter); // On resumes the selected host directly.
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(1));
    TEST_ASSERT_TRUE(f.hosts.settings().bluetoothEnabled);
}

void test_first_enable_without_profiles_guides_to_explicit_pairing() {
    Fixture f;
    f.adapter.known.clear();
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    Screen screen(f);
    screen.openBluetooth();
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_TRUE(screen.shows("Add device, then Enter"));
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_TRUE(f.adapter.targets.empty());
    screen.press(core::NamedKey::Enter);
    TEST_ASSERT_TRUE(f.hosts.pairing());
    TEST_ASSERT_FALSE(f.adapter.targets.back().has_value());
    screen.press(core::NamedKey::Escape);
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_TRUE(f.hosts.bluetoothState() == BluetoothState::Disabled);
}

void test_home_distinguishes_missing_selection_and_invalid_input_from_fault() {
    Fixture f;
    f.hosts.start();
    Screen screen(f);
    screen.openBluetooth();
    screen.press(core::NamedKey::Enter);
    screen.press(core::NamedKey::Escape);
    TEST_ASSERT_TRUE(screen.shows("OFF"));
    TEST_ASSERT_FALSE(screen.shows("ERROR"));
    f.hosts.renameHost(f.hosts.settings().hosts.front().id, "");
    screen.shell.update({});
    TEST_ASSERT_FALSE(screen.shows("ERROR"));
    f.memory.failWrite = true;
    TEST_ASSERT_TRUE(f.hosts.selectHost(f.hosts.settings().hosts.front().id) ==
                     services::HostResult::StorageError);
    screen.shell.update({});
    TEST_ASSERT_TRUE(screen.shows("ERROR"));
}

void test_pairing_digits_follow_the_reference_font_character_order() {
    // The upstream sheet is ordered "1234560789", not ASCII digit order.
    for (const auto& item : {std::pair<std::uint32_t, int>{0, 6}, {123456, 0}}) {
        Fixture f;
        f.hosts.start();
        f.hosts.startPairing();
        f.adapter.connect(3);
        f.hosts.update(std::chrono::milliseconds(1));
        BluetoothEvent event{BluetoothEventType::PairingChallenge,
                             {3},
                             BluetoothFailureClass::Fatal,
                             f.adapter.lifecycle};
        event.challengeType = BluetoothPairingChallengeType::DisplayPasskey;
        event.challengeValue = item.first;
        f.adapter.events.push_back(event);
        f.hosts.update(std::chrono::milliseconds(1));
        core::ActionBus bus;
        Display display;
        apps::HostSettings ui(f.hosts, bus, display);
        ui.update({});
        for (int y = 0; y < micro5_digits::kHeight; ++y) {
            for (int x = 0; x < micro5_digits::kWidth; ++x) {
                const bool expected =
                    micro5_digits::kGlyphs[0][item.second][y * micro5_digits::kStride + x / 8] &
                    (0x80U >> (x % 8));
                TEST_ASSERT_EQUAL(expected, display.ink.count({48 + x, 52 + y}) != 0);
            }
        }
    }
}

void test_settings_pairing_progress_and_rename_cancel_use_real_services() {
    Fixture f;
    f.hosts.start();
    core::ActionBus bus;
    for (const auto* id :
         {"host.pair", "host.cancel-pairing", "host.rename", "host.select", "host.bluetooth"})
        bus.registerHandler(id, f.hosts);
    Display display;
    apps::HostSettings ui(f.hosts, bus, display);
    const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
    const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    const core::InputEvent rename{core::InputEventType::PrintableCharacter, 'r', {}, {}};
    ui.update({down, down, rename});
    const auto before = f.memory.bytes;
    ui.update({escape});
    TEST_ASSERT_TRUE(before == f.memory.bytes);
    ui.update({enter, enter});
    TEST_ASSERT_TRUE(f.adapter.targets.back() == bond(1));
    bus.dispatch({"host.bluetooth", "test", {{"enabled", false}}});
    ui.update({});
    TEST_ASSERT_TRUE(f.bluetooth.state() == BluetoothState::Disabled);
    f.hosts.startPairing();
    f.adapter.connect(3);
    f.hosts.update(std::chrono::milliseconds(1));
    ui.update({});
    TEST_ASSERT_TRUE(std::find(display.texts.begin(), display.texts.end(), "Securing connection") !=
                     display.texts.end());
    ui.update({escape});
    TEST_ASSERT_FALSE(f.hosts.pairing());
    TEST_ASSERT_FALSE(f.hosts.settings().bluetoothEnabled);
}

} // namespace
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_metadata_operations_are_bounded_and_do_not_start_bluetooth);
    RUN_TEST(test_invalid_or_failed_metadata_actions_preserve_configuration_and_radio_state);
    RUN_TEST(test_metadata_actions_persist_without_touching_the_ready_selected_host);
    RUN_TEST(test_host_actions_retry_failed_startup_without_activating_previous_target);
    RUN_TEST(test_corrupt_configuration_is_not_overwritten_by_startup_retry);
    RUN_TEST(test_interrupted_pairing_invalidates_cached_challenge_for_all_prompt_types);
    RUN_TEST(test_host_menu_rename_and_confirmed_delete_affect_only_that_host);
    RUN_TEST(test_delete_selected_stays_off_and_nonselected_preserves_live_host);
    RUN_TEST(test_delete_failure_preserves_profile_and_missing_bond_can_be_removed);
    RUN_TEST(test_first_enable_guides_to_host_and_next_enter_selects_it);
    RUN_TEST(test_first_enable_without_profiles_guides_to_explicit_pairing);
    RUN_TEST(test_home_distinguishes_missing_selection_and_invalid_input_from_fault);
    RUN_TEST(test_pairing_digits_follow_the_reference_font_character_order);
    RUN_TEST(test_settings_pairing_progress_and_rename_cancel_use_real_services);
    RUN_TEST(test_full_profile_list_rejects_pairing_without_interrupting_selected_host);
    RUN_TEST(test_actions_preserve_selected_host_on_restart_and_reject_wrong_parameter_types);
    RUN_TEST(test_completed_pair_is_saved_once_and_missing_selected_bond_never_falls_back);
    RUN_TEST(test_timeout_returns_to_saved_off_and_rename_preserves_identity);
    RUN_TEST(test_import_existing_pairs_without_advertising_then_persist_selected_host);
    RUN_TEST(test_switch_releases_old_host_before_shutdown_and_only_advertises_new_target);
    RUN_TEST(test_off_survives_restart_and_failed_selection_is_closed_without_fallback);
    RUN_TEST(test_pairing_cancel_restores_selected_host_without_deleting_pairs);
    return UNITY_END();
}
