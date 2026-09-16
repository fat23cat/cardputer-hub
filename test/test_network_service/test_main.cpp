#include <unity.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "core/actions/action_bus.h"
#include "services/network/network_service.h"

using namespace cardputer_hub;
using namespace std::chrono_literals;

namespace {
class MemoryStorage final : public core::IStorageAdapter {
  public:
    explicit MemoryStorage(std::vector<std::string>& events) : events_(events) {}

    core::StorageReadResult read(const core::StorageAddress&) override {
        return {bytes.empty() ? core::StorageReadStatus::NotFound : core::StorageReadStatus::Found,
                bytes};
    }

    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        events_.push_back("persist");
        ++writes;
        if (writeError)
            return core::StorageWriteStatus::BackendError;
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }

    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }

    core::StorageBytes bytes;
    bool writeError = false;
    int writes = 0;

  private:
    std::vector<std::string>& events_;
};

class FakeWifiAdapter final : public connectivity::IWifiAdapter {
  public:
    explicit FakeWifiAdapter(std::vector<std::string>& events) : events_(events) {}

    connectivity::WifiAdapterResult initializeStation() override {
        events_.push_back("initialize");
        ++initializeCalls;
        return initializeResult;
    }

    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig& value) override {
        events_.push_back("connect");
        ++connectCalls;
        lastConfig = value;
        return connectResult;
    }

    connectivity::WifiAdapterResult disconnect() override {
        events_.push_back("disconnect");
        ++disconnectCalls;
        return disconnectResult;
    }

    connectivity::WifiAdapterState state() const override { return adapterState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return rssi; }

    connectivity::WifiAdapterResult initializeResult = connectivity::WifiAdapterResult::Success;
    connectivity::WifiAdapterResult connectResult = connectivity::WifiAdapterResult::Success;
    connectivity::WifiAdapterResult disconnectResult = connectivity::WifiAdapterResult::Success;
    connectivity::WifiAdapterState adapterState = connectivity::WifiAdapterState::Connecting;
    std::optional<std::int32_t> rssi;
    connectivity::WifiNetworkConfig lastConfig;
    int initializeCalls = 0;
    int connectCalls = 0;
    int disconnectCalls = 0;

  private:
    std::vector<std::string>& events_;
};

class CapturingLogSink final : public core::ILogSink {
  public:
    void write(const core::LogRecord& record) override { messages.emplace_back(record.message); }
    std::vector<std::string> messages;
};

struct Fixture {
    Fixture()
        : memory(events), storage(memory), configuration(storage), adapter(events), wifi(adapter),
          logger(sink, core::LogLevel::Debug), network(wifi, configuration, &logger) {
        TEST_ASSERT_TRUE(configuration.load() == services::ConfigurationResult::Success);
    }

    void storeWifi(bool enabled, std::string ssid = "Network",
                   std::string passphrase = "recognizable-secret") {
        auto value = configuration.value();
        value.wifi = {enabled, std::move(ssid), std::move(passphrase)};
        TEST_ASSERT_TRUE(configuration.save(value) == services::ConfigurationResult::Success);
        events.clear();
    }

    std::vector<std::string> events;
    MemoryStorage memory;
    core::Storage storage;
    services::ConfigurationService configuration;
    FakeWifiAdapter adapter;
    connectivity::WiFiService wifi;
    CapturingLogSink sink;
    core::Logger logger;
    services::NetworkService network;
};

void test_start_restores_only_enabled_configured_intent() {
    {
        Fixture f;
        TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
        TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);
        TEST_ASSERT_TRUE(f.network.status().connection == services::WifiConnectionStatus::Off);
    }
    {
        Fixture f;
        f.storeWifi(false);
        TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
        TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);
        TEST_ASSERT_TRUE(f.network.status().connection == services::WifiConnectionStatus::Off);
    }
    {
        Fixture f;
        f.storeWifi(true);
        TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
        TEST_ASSERT_EQUAL(1, f.adapter.connectCalls);
        TEST_ASSERT_EQUAL_STRING("Network", f.adapter.lastConfig.ssid.c_str());
        TEST_ASSERT_TRUE(f.network.status().connection ==
                         services::WifiConnectionStatus::Connecting);
    }
}

void test_start_does_not_connect_when_persisted_configuration_is_invalid() {
    std::vector<std::string> events;
    MemoryStorage memory(events);
    memory.bytes = {'N', 'O', 'T', 'H', 4};
    core::Storage storage(memory);
    services::ConfigurationService configuration(storage);
    FakeWifiAdapter adapter(events);
    connectivity::WiFiService wifi(adapter);
    services::NetworkService network(wifi, configuration);

    TEST_ASSERT_TRUE(network.start() == services::NetworkResult::StorageError);
    TEST_ASSERT_EQUAL(0, adapter.initializeCalls);
    TEST_ASSERT_EQUAL(0, adapter.connectCalls);
    TEST_ASSERT_TRUE(network.status().lastResult == services::NetworkResult::StorageError);
}

void test_configure_persists_before_reconnecting_only_when_enabled() {
    Fixture f;
    TEST_ASSERT_TRUE(f.network.configure("Open", "") == services::NetworkResult::Success);
    TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);
    TEST_ASSERT_FALSE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Open", f.configuration.value().wifi.ssid.c_str());

    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::Success);
    f.events.clear();
    TEST_ASSERT_TRUE(f.network.configure("Replacement", "replacement-secret") ==
                     services::NetworkResult::Success);
    TEST_ASSERT_EQUAL_UINT(3, f.events.size());
    TEST_ASSERT_EQUAL_STRING("persist", f.events[0].c_str());
    TEST_ASSERT_EQUAL_STRING("disconnect", f.events[1].c_str());
    TEST_ASSERT_EQUAL_STRING("connect", f.events[2].c_str());
    TEST_ASSERT_EQUAL_STRING("Replacement", f.configuration.value().wifi.ssid.c_str());
}

void test_enable_disable_and_forget_persist_before_touching_connectivity() {
    Fixture f;
    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::NotConfigured);
    TEST_ASSERT_EQUAL(0, f.memory.writes);
    TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);

    f.storeWifi(false);
    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::Success);
    TEST_ASSERT_EQUAL_STRING("persist", f.events[0].c_str());
    TEST_ASSERT_EQUAL_STRING("initialize", f.events[1].c_str());
    TEST_ASSERT_EQUAL_STRING("connect", f.events[2].c_str());
    TEST_ASSERT_TRUE(f.configuration.value().wifi.enabled);

    f.events.clear();
    TEST_ASSERT_TRUE(f.network.setEnabled(false) == services::NetworkResult::Success);
    TEST_ASSERT_EQUAL_STRING("persist", f.events[0].c_str());
    TEST_ASSERT_EQUAL_STRING("disconnect", f.events[1].c_str());
    TEST_ASSERT_FALSE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Network", f.configuration.value().wifi.ssid.c_str());

    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::Success);
    f.events.clear();
    TEST_ASSERT_TRUE(f.network.forget() == services::NetworkResult::Success);
    TEST_ASSERT_EQUAL_STRING("persist", f.events[0].c_str());
    TEST_ASSERT_EQUAL_STRING("disconnect", f.events[1].c_str());
    TEST_ASSERT_FALSE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_TRUE(f.configuration.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(f.configuration.value().wifi.passphrase.empty());
}

void test_invalid_credentials_are_rejected_without_persistence_or_connectivity() {
    Fixture f;
    TEST_ASSERT_TRUE(f.network.configure("Protected", "short") ==
                     services::NetworkResult::InvalidInput);
    TEST_ASSERT_EQUAL(0, f.memory.writes);
    TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);
    TEST_ASSERT_FALSE(f.network.status().configured);
}

void test_storage_failures_retain_published_intent_and_do_not_touch_connectivity() {
    Fixture f;
    f.storeWifi(true);
    TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
    f.events.clear();
    f.memory.writeError = true;

    TEST_ASSERT_TRUE(f.network.configure("Replacement", "replacement-secret") ==
                     services::NetworkResult::StorageError);
    TEST_ASSERT_EQUAL_STRING("Network", f.configuration.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL(0, f.adapter.disconnectCalls);

    TEST_ASSERT_TRUE(f.network.setEnabled(false) == services::NetworkResult::StorageError);
    TEST_ASSERT_TRUE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL(0, f.adapter.disconnectCalls);

    TEST_ASSERT_TRUE(f.network.forget() == services::NetworkResult::StorageError);
    TEST_ASSERT_EQUAL_STRING("recognizable-secret",
                             f.configuration.value().wifi.passphrase.c_str());
    TEST_ASSERT_EQUAL(0, f.adapter.disconnectCalls);

    Fixture enable;
    enable.storeWifi(false);
    enable.memory.writeError = true;
    TEST_ASSERT_TRUE(enable.network.setEnabled(true) == services::NetworkResult::StorageError);
    TEST_ASSERT_FALSE(enable.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL(0, enable.adapter.connectCalls);
}

void test_connectivity_failures_do_not_roll_back_persisted_intent() {
    Fixture f;
    f.storeWifi(false);
    f.adapter.connectResult = connectivity::WifiAdapterResult::Error;
    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::ConnectivityError);
    TEST_ASSERT_TRUE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Network", f.configuration.value().wifi.ssid.c_str());
    TEST_ASSERT_TRUE(f.network.status().connection == services::WifiConnectionStatus::Error);

    Fixture replacement;
    replacement.storeWifi(true);
    TEST_ASSERT_TRUE(replacement.network.start() == services::NetworkResult::Success);
    replacement.adapter.connectResult = connectivity::WifiAdapterResult::Error;
    TEST_ASSERT_TRUE(replacement.network.configure("Replacement", "replacement-secret") ==
                     services::NetworkResult::ConnectivityError);
    TEST_ASSERT_TRUE(replacement.configuration.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Replacement", replacement.configuration.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("replacement-secret",
                             replacement.configuration.value().wifi.passphrase.c_str());

    Fixture disabled;
    disabled.storeWifi(true);
    TEST_ASSERT_TRUE(disabled.network.start() == services::NetworkResult::Success);
    disabled.adapter.disconnectResult = connectivity::WifiAdapterResult::Error;
    TEST_ASSERT_TRUE(disabled.network.setEnabled(false) ==
                     services::NetworkResult::ConnectivityError);
    TEST_ASSERT_FALSE(disabled.configuration.value().wifi.enabled);
    TEST_ASSERT_TRUE(disabled.network.status().connection == services::WifiConnectionStatus::Off);
}

void test_forget_keeps_credentials_deleted_when_disconnect_fails() {
    Fixture f;
    f.storeWifi(true);
    TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
    f.events.clear();
    f.adapter.disconnectResult = connectivity::WifiAdapterResult::Error;

    TEST_ASSERT_TRUE(f.network.forget() == services::NetworkResult::ConnectivityError);
    TEST_ASSERT_EQUAL_UINT(2, f.events.size());
    TEST_ASSERT_EQUAL_STRING("persist", f.events[0].c_str());
    TEST_ASSERT_EQUAL_STRING("disconnect", f.events[1].c_str());
    TEST_ASSERT_FALSE(f.configuration.value().wifi.enabled);
    TEST_ASSERT_TRUE(f.configuration.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(f.configuration.value().wifi.passphrase.empty());
    TEST_ASSERT_TRUE(f.network.status().connection == services::WifiConnectionStatus::Off);
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::ConnectivityError);
}

void test_status_centralizes_runtime_mapping_and_connected_only_rssi() {
    Fixture f;
    f.storeWifi(true);
    TEST_ASSERT_TRUE(f.network.start() == services::NetworkResult::Success);
    auto status = f.network.status();
    TEST_ASSERT_TRUE(status.configured);
    TEST_ASSERT_TRUE(status.enabled);
    TEST_ASSERT_EQUAL_STRING("Network", status.ssid.c_str());
    TEST_ASSERT_TRUE(status.connection == services::WifiConnectionStatus::Connecting);
    TEST_ASSERT_FALSE(status.signalStrengthDbm.has_value());

    f.adapter.adapterState = connectivity::WifiAdapterState::Disconnected;
    f.network.update(1ms);
    status = f.network.status();
    TEST_ASSERT_TRUE(status.connection == services::WifiConnectionStatus::Connecting);
    TEST_ASSERT_FALSE(status.signalStrengthDbm.has_value());

    f.network.update(1s);
    f.adapter.adapterState = connectivity::WifiAdapterState::Connected;
    f.adapter.rssi = -52;
    f.network.update(1ms);
    status = f.network.status();
    TEST_ASSERT_TRUE(status.connection == services::WifiConnectionStatus::Connected);
    TEST_ASSERT_TRUE(status.signalStrengthDbm == std::optional<std::int32_t>{-52});

    f.adapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(1ms);
    status = f.network.status();
    TEST_ASSERT_TRUE(status.connection == services::WifiConnectionStatus::Error);
    TEST_ASSERT_FALSE(status.signalStrengthDbm.has_value());
    TEST_ASSERT_TRUE(status.lastResult == services::NetworkResult::ConnectivityError);
}

void registerNetworkActions(core::ActionBus& actions, services::NetworkService& network) {
    for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
        TEST_ASSERT_TRUE(actions.registerHandler(id, network) ==
                         core::RegistrationResult::Registered);
}

void test_network_actions_dispatch_valid_requests_and_reject_malformed_ones() {
    Fixture f;
    f.storeWifi(false);
    core::ActionBus actions;
    registerNetworkActions(actions, f.network);

    TEST_ASSERT_TRUE(actions.dispatch({"network.set-enabled", "test", {{"enabled", true}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(f.network.status().enabled);
    TEST_ASSERT_EQUAL(1, f.adapter.connectCalls);

    const auto connects = f.adapter.connectCalls;
    TEST_ASSERT_TRUE(actions.dispatch({"network.set-enabled", "test", {}}) ==
                     core::DispatchResult::Rejected);
    TEST_ASSERT_TRUE(
        actions.dispatch({"network.set-enabled", "test", {{"enabled", std::int32_t{1}}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_TRUE(f.network.status().enabled);
    TEST_ASSERT_EQUAL(connects, f.adapter.connectCalls);

    TEST_ASSERT_TRUE(actions.dispatch({"network.configure",
                                       "test",
                                       {{"ssid", std::string{"Office"}},
                                        {"passphrase", std::string{"recognizable-secret"}}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_EQUAL_STRING("Office", f.network.status().ssid.c_str());

    const auto configuredSsid = f.network.status().ssid;
    TEST_ASSERT_TRUE(
        actions.dispatch({"network.configure", "test", {{"ssid", std::string{"Guest"}}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_TRUE(
        actions.dispatch({"network.configure",
                          "test",
                          {{"ssid", std::int32_t{1}}, {"passphrase", std::string{"x"}}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_EQUAL_STRING(configuredSsid.c_str(), f.network.status().ssid.c_str());

    TEST_ASSERT_TRUE(actions.dispatch({"network.forget", "test", {}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_FALSE(f.network.status().configured);

    TEST_ASSERT_TRUE(actions.dispatch({"network.unknown", "test", {}}) ==
                     core::DispatchResult::Unsupported);
}

void test_valid_network_actions_stay_handled_when_the_domain_operation_fails() {
    Fixture f;
    core::ActionBus actions;
    registerNetworkActions(actions, f.network);

    TEST_ASSERT_TRUE(actions.dispatch({"network.set-enabled", "test", {{"enabled", true}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::NotConfigured);
    TEST_ASSERT_FALSE(f.network.status().enabled);
    TEST_ASSERT_EQUAL(0, f.adapter.connectCalls);

    TEST_ASSERT_TRUE(actions.dispatch({"network.configure",
                                       "test",
                                       {{"ssid", std::string{""}},
                                        {"passphrase", std::string{"recognizable-secret"}}}}) ==
                     core::DispatchResult::Handled);
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::InvalidInput);
    TEST_ASSERT_FALSE(f.network.status().configured);
}

void test_status_and_logs_never_publish_the_passphrase() {
    Fixture f;
    const std::string secret = "recognizable-secret";
    TEST_ASSERT_TRUE(f.network.configure("Network", secret) == services::NetworkResult::Success);
    f.adapter.connectResult = connectivity::WifiAdapterResult::Error;
    TEST_ASSERT_TRUE(f.network.setEnabled(true) == services::NetworkResult::ConnectivityError);

    const auto status = f.network.status();
    TEST_ASSERT_EQUAL_STRING("Network", status.ssid.c_str());
    for (const auto& message : f.sink.messages)
        TEST_ASSERT_EQUAL(std::string::npos, message.find(secret));
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_start_restores_only_enabled_configured_intent);
    RUN_TEST(test_start_does_not_connect_when_persisted_configuration_is_invalid);
    RUN_TEST(test_configure_persists_before_reconnecting_only_when_enabled);
    RUN_TEST(test_enable_disable_and_forget_persist_before_touching_connectivity);
    RUN_TEST(test_invalid_credentials_are_rejected_without_persistence_or_connectivity);
    RUN_TEST(test_storage_failures_retain_published_intent_and_do_not_touch_connectivity);
    RUN_TEST(test_connectivity_failures_do_not_roll_back_persisted_intent);
    RUN_TEST(test_forget_keeps_credentials_deleted_when_disconnect_fails);
    RUN_TEST(test_status_centralizes_runtime_mapping_and_connected_only_rssi);
    RUN_TEST(test_status_and_logs_never_publish_the_passphrase);
    RUN_TEST(test_network_actions_dispatch_valid_requests_and_reject_malformed_ones);
    RUN_TEST(test_valid_network_actions_stay_handled_when_the_domain_operation_fails);
    return UNITY_END();
}
