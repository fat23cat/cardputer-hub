#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "connectivity/wifi/wifi_service.h"

namespace {

using cardputer_hub::connectivity::IWifiAdapter;
using cardputer_hub::connectivity::WifiAdapterResult;
using cardputer_hub::connectivity::WifiAdapterState;
using cardputer_hub::connectivity::WifiConnectResult;
using cardputer_hub::connectivity::WifiDisconnectResult;
using cardputer_hub::connectivity::WifiNetworkConfig;
using cardputer_hub::connectivity::WiFiService;
using cardputer_hub::connectivity::WifiState;
using cardputer_hub::core::ILogSink;
using cardputer_hub::core::Logger;
using cardputer_hub::core::LogLevel;
using cardputer_hub::core::LogRecord;

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

class FakeWifiAdapter final : public IWifiAdapter {
  public:
    WifiAdapterResult initializeStation() override {
        ++initializeCount;
        return initializeResult;
    }

    WifiAdapterResult connect(const WifiNetworkConfig& config) override {
        ++connectCount;
        lastConfig = config;
        configs.push_back(config);
        return connectResult;
    }

    WifiAdapterResult disconnect() override {
        ++disconnectCount;
        return disconnectResult;
    }
    WifiAdapterState state() const override {
        ++stateCount;
        return linkState;
    }
    std::optional<std::int32_t> signalStrengthDbm() const override {
        ++rssiCount;
        return rssi;
    }

    int initializeCount = 0;
    int connectCount = 0;
    int disconnectCount = 0;
    mutable int stateCount = 0;
    mutable int rssiCount = 0;
    WifiAdapterResult initializeResult = WifiAdapterResult::Success;
    WifiAdapterResult connectResult = WifiAdapterResult::Success;
    WifiAdapterResult disconnectResult = WifiAdapterResult::Success;
    WifiAdapterState linkState = WifiAdapterState::Connecting;
    std::optional<std::int32_t> rssi = -50;
    WifiNetworkConfig lastConfig;
    std::vector<WifiNetworkConfig> configs;
};

void assertConnectResult(WifiConnectResult expected, const WifiNetworkConfig& config,
                         int expectedAdapterCalls) {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);

    const auto result = service.connect(config);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(expected), static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(expectedAdapterCalls, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(expectedAdapterCalls, adapter.connectCount);
}

} // namespace

void setUp() {}

void tearDown() {}

void test_ssid_accepts_one_and_thirty_two_bytes() {
    assertConnectResult(WifiConnectResult::Started, {"a", ""}, 1);
    assertConnectResult(WifiConnectResult::Started, {std::string(32, 's'), ""}, 1);
}

void test_ssid_rejects_empty_overlong_and_embedded_nul_values() {
    assertConnectResult(WifiConnectResult::InvalidConfig, {"", ""}, 0);
    assertConnectResult(WifiConnectResult::InvalidConfig, {std::string(33, 's'), ""}, 0);
    assertConnectResult(WifiConnectResult::InvalidConfig, {std::string{"wi\0fi", 5}, ""}, 0);
}

void test_passphrase_accepts_open_and_personal_wpa_boundaries() {
    assertConnectResult(WifiConnectResult::Started, {"network", ""}, 1);
    assertConnectResult(WifiConnectResult::Started, {"network", std::string(8, 'p')}, 1);
    assertConnectResult(WifiConnectResult::Started, {"network", std::string(63, 'p')}, 1);
}

void test_passphrase_accepts_exactly_sixty_four_hexadecimal_digits() {
    assertConnectResult(
        WifiConnectResult::Started,
        {"network", "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdefABCD"}, 1);
}

void test_passphrase_rejects_invalid_lengths_and_non_hex_raw_psk() {
    assertConnectResult(WifiConnectResult::InvalidConfig, {"network", std::string(7, 'p')}, 0);
    assertConnectResult(WifiConnectResult::InvalidConfig, {"network", std::string(64, 'p')}, 0);
    assertConnectResult(WifiConnectResult::InvalidConfig, {"network", std::string(65, 'a')}, 0);
}

void test_passphrase_rejects_embedded_nul() {
    assertConnectResult(WifiConnectResult::InvalidConfig,
                        {"network", std::string{"password\0suffix", 15}}, 0);
}

void test_valid_request_starts_immediately_and_copies_configuration() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    WifiNetworkConfig config{"first-network", "first-password"};

    const auto result = service.connect(config);
    config.ssid = "mutated";
    config.passphrase = "mutated-password";

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::Started),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
    TEST_ASSERT_EQUAL_STRING("first-network", adapter.lastConfig.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("first-password", adapter.lastConfig.passphrase.c_str());
}

void test_valid_replacement_disconnects_previous_target_and_starts_immediately() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"first-network", "first-password"});

    const auto result = service.connect({"second-network", "second-password"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::Started),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(2, adapter.connectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_STRING("second-network", adapter.lastConfig.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("second-password", adapter.lastConfig.passphrase.c_str());
}

void test_replacement_disconnect_failure_is_fatal_and_does_not_start_new_target() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"first-network", "first-password"});
    adapter.disconnectResult = WifiAdapterResult::Error;

    const auto result = service.connect({"second-network", "second-password"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_STRING("first-network", adapter.lastConfig.ssid.c_str());
}

void test_invalid_replacement_does_not_touch_adapter_or_active_state() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"first-network", "first-password"});

    const auto result = service.connect({"", "invalid"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::InvalidConfig),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.disconnectCount);
}

void test_explicit_disconnect_cancels_active_intent_and_returns_idle() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});

    const auto result = service.disconnect();
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiDisconnectResult::Disconnected),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Idle),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.stateCount);
}

void test_explicit_disconnect_reports_adapter_failure_and_clears_intent() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.disconnectResult = WifiAdapterResult::Error;

    const auto result = service.disconnect();
    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiDisconnectResult::AdapterError),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);

    const auto repeatedResult = service.disconnect();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiDisconnectResult::Disconnected),
                            static_cast<unsigned int>(repeatedResult));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
}

void test_update_observes_successful_connection() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Connected;

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connected),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.stateCount);
}

void test_failed_attempt_and_unexpected_connection_loss_enter_retry_waiting() {
    FakeWifiAdapter failedAttemptAdapter;
    WiFiService failedAttempt(failedAttemptAdapter);
    (void)failedAttempt.connect({"network", "password"});
    failedAttemptAdapter.linkState = WifiAdapterState::Disconnected;

    failedAttempt.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::RetryWaiting),
                            static_cast<unsigned int>(failedAttempt.state()));
    TEST_ASSERT_EQUAL_INT(1, failedAttemptAdapter.disconnectCount);

    FakeWifiAdapter lostConnectionAdapter;
    WiFiService lostConnection(lostConnectionAdapter);
    (void)lostConnection.connect({"network", "password"});
    lostConnectionAdapter.linkState = WifiAdapterState::Connected;
    lostConnection.update(std::chrono::milliseconds::zero());
    lostConnectionAdapter.linkState = WifiAdapterState::Disconnected;

    lostConnection.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::RetryWaiting),
                            static_cast<unsigned int>(lostConnection.state()));
    TEST_ASSERT_EQUAL_INT(1, lostConnectionAdapter.disconnectCount);
}

void test_disconnect_failure_while_scheduling_retry_is_fatal() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Disconnected;
    adapter.disconnectResult = WifiAdapterResult::Error;

    service.update(std::chrono::milliseconds::zero());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
}

void test_initialization_and_connect_operation_failures_are_fatal() {
    FakeWifiAdapter initializationFailure;
    initializationFailure.initializeResult = WifiAdapterResult::Error;
    WiFiService initializationService(initializationFailure);

    const auto initializationResult = initializationService.connect({"network", "password"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::AdapterError),
                            static_cast<unsigned int>(initializationResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(initializationService.state()));
    TEST_ASSERT_EQUAL_INT(1, initializationFailure.initializeCount);
    TEST_ASSERT_EQUAL_INT(0, initializationFailure.connectCount);

    FakeWifiAdapter connectFailure;
    connectFailure.connectResult = WifiAdapterResult::Error;
    WiFiService connectService(connectFailure);

    const auto connectResult = connectService.connect({"network", "password"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiConnectResult::AdapterError),
                            static_cast<unsigned int>(connectResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(connectService.state()));
    TEST_ASSERT_EQUAL_INT(1, connectFailure.initializeCount);
    TEST_ASSERT_EQUAL_INT(1, connectFailure.connectCount);
}

void test_fatal_polled_adapter_state_enters_error_without_retrying() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Error;

    service.update(std::chrono::hours(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
}

void test_state_query_never_polls_adapter() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});

    (void)service.state();
    (void)service.state();

    TEST_ASSERT_EQUAL_INT(0, adapter.stateCount);
}

void test_connecting_attempt_times_out_at_exactly_fifteen_seconds() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});

    service.update(std::chrono::milliseconds(14'999));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(0, adapter.disconnectCount);

    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::RetryWaiting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
}

void test_disconnect_failure_at_attempt_timeout_is_fatal() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.disconnectResult = WifiAdapterResult::Error;

    service.update(std::chrono::seconds(15));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.disconnectCount);
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);
}

void test_retry_starts_only_at_exact_delay_boundary() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;

    service.update(std::chrono::milliseconds(999));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::RetryWaiting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(1, adapter.connectCount);

    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.connectCount);
}

void test_adapter_failure_while_starting_retry_is_fatal() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.connectResult = WifiAdapterResult::Error;

    service.update(std::chrono::seconds(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.connectCount);
}

void test_retries_follow_capped_exponential_schedule_indefinitely() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    const std::chrono::seconds retryDelays[] = {
        std::chrono::seconds(1),  std::chrono::seconds(2),  std::chrono::seconds(4),
        std::chrono::seconds(8),  std::chrono::seconds(16), std::chrono::seconds(30),
        std::chrono::seconds(30), std::chrono::seconds(30),
    };

    for (std::size_t index = 0; index < std::size(retryDelays); ++index) {
        adapter.linkState = WifiAdapterState::Disconnected;
        service.update(std::chrono::milliseconds::zero());
        adapter.linkState = WifiAdapterState::Connecting;

        service.update(retryDelays[index] - std::chrono::milliseconds(1));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::RetryWaiting),
                                static_cast<unsigned int>(service.state()));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(index) + 1, adapter.connectCount);

        service.update(std::chrono::milliseconds(1));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                                static_cast<unsigned int>(service.state()));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(index) + 2, adapter.connectCount);
    }
}

void test_success_resets_next_retry_delay_to_one_second() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;
    service.update(std::chrono::seconds(1));
    adapter.linkState = WifiAdapterState::Connected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;

    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(2, adapter.connectCount);
    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_INT(3, adapter.connectCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connecting),
                            static_cast<unsigned int>(service.state()));
}

void test_replacement_resets_backoff_and_retry_uses_new_owned_target() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"first-network", "first-password"});
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;
    service.update(std::chrono::seconds(1));
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());

    WifiNetworkConfig replacement{"second-network", "second-password"};
    (void)service.connect(replacement);
    replacement.ssid = "mutated";
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;
    service.update(std::chrono::milliseconds(999));
    TEST_ASSERT_EQUAL_INT(3, adapter.connectCount);
    service.update(std::chrono::milliseconds(1));

    TEST_ASSERT_EQUAL_INT(4, adapter.connectCount);
    TEST_ASSERT_EQUAL_STRING("second-network", adapter.lastConfig.ssid.c_str());
}

void test_invalid_replacement_preserves_target_used_by_retry() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"first-network", "first-password"});
    (void)service.connect({"", "invalid"});
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;

    service.update(std::chrono::seconds(1));

    TEST_ASSERT_EQUAL_INT(2, adapter.connectCount);
    TEST_ASSERT_EQUAL_STRING("first-network", adapter.lastConfig.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("first-password", adapter.lastConfig.passphrase.c_str());
}

void test_signal_strength_is_forwarded_only_while_connected() {
    FakeWifiAdapter adapter;
    adapter.rssi = -67;
    WiFiService service(adapter);

    TEST_ASSERT_FALSE(service.signalStrengthDbm().has_value());
    TEST_ASSERT_EQUAL_INT(0, adapter.rssiCount);

    (void)service.connect({"network", "password"});
    TEST_ASSERT_FALSE(service.signalStrengthDbm().has_value());
    TEST_ASSERT_EQUAL_INT(0, adapter.rssiCount);

    adapter.linkState = WifiAdapterState::Connected;
    service.update(std::chrono::milliseconds::zero());
    const auto connectedRssi = service.signalStrengthDbm();
    TEST_ASSERT_TRUE(connectedRssi.has_value());
    TEST_ASSERT_EQUAL_INT32(-67, *connectedRssi);
    TEST_ASSERT_EQUAL_INT(1, adapter.rssiCount);

    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    TEST_ASSERT_FALSE(service.signalStrengthDbm().has_value());
    TEST_ASSERT_EQUAL_INT(1, adapter.rssiCount);
}

void test_signal_strength_is_hidden_after_adapter_error() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Error;
    service.update(std::chrono::milliseconds::zero());

    const auto result = service.signalStrengthDbm();

    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL_INT(0, adapter.rssiCount);
}

void test_signal_strength_is_hidden_when_link_drops_before_next_update() {
    FakeWifiAdapter adapter;
    WiFiService service(adapter);
    (void)service.connect({"network", "password"});
    adapter.linkState = WifiAdapterState::Connected;
    service.update(std::chrono::milliseconds::zero());
    adapter.rssi = std::nullopt;

    const auto result = service.signalStrengthDbm();

    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL_INT(1, adapter.rssiCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(WifiState::Connected),
                            static_cast<unsigned int>(service.state()));
}

void test_logs_describe_wifi_outcomes_without_network_identity_or_credentials() {
    FakeWifiAdapter adapter;
    CapturingLogSink sink;
    Logger logger(sink, LogLevel::Debug);
    WiFiService service(adapter, logger);
    const std::string ssid = "SECRET_NETWORK_ID";
    const std::string passphrase = "SECRET_PASSWORD_VALUE";

    (void)service.connect({ssid, passphrase});
    adapter.linkState = WifiAdapterState::Connected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Disconnected;
    service.update(std::chrono::milliseconds::zero());
    adapter.linkState = WifiAdapterState::Connecting;
    service.update(std::chrono::seconds(1));
    service.disconnect();

    TEST_ASSERT_GREATER_THAN_UINT32(0, sink.records.size());
    for (const auto& record : sink.records) {
        TEST_ASSERT_EQUAL_STRING("wifi", record.component.c_str());
        TEST_ASSERT_NULL(std::strstr(record.message.c_str(), ssid.c_str()));
        TEST_ASSERT_NULL(std::strstr(record.message.c_str(), passphrase.c_str()));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_accepts_one_and_thirty_two_bytes);
    RUN_TEST(test_ssid_rejects_empty_overlong_and_embedded_nul_values);
    RUN_TEST(test_passphrase_accepts_open_and_personal_wpa_boundaries);
    RUN_TEST(test_passphrase_accepts_exactly_sixty_four_hexadecimal_digits);
    RUN_TEST(test_passphrase_rejects_invalid_lengths_and_non_hex_raw_psk);
    RUN_TEST(test_passphrase_rejects_embedded_nul);
    RUN_TEST(test_valid_request_starts_immediately_and_copies_configuration);
    RUN_TEST(test_valid_replacement_disconnects_previous_target_and_starts_immediately);
    RUN_TEST(test_replacement_disconnect_failure_is_fatal_and_does_not_start_new_target);
    RUN_TEST(test_invalid_replacement_does_not_touch_adapter_or_active_state);
    RUN_TEST(test_explicit_disconnect_cancels_active_intent_and_returns_idle);
    RUN_TEST(test_explicit_disconnect_reports_adapter_failure_and_clears_intent);
    RUN_TEST(test_update_observes_successful_connection);
    RUN_TEST(test_failed_attempt_and_unexpected_connection_loss_enter_retry_waiting);
    RUN_TEST(test_disconnect_failure_while_scheduling_retry_is_fatal);
    RUN_TEST(test_initialization_and_connect_operation_failures_are_fatal);
    RUN_TEST(test_fatal_polled_adapter_state_enters_error_without_retrying);
    RUN_TEST(test_state_query_never_polls_adapter);
    RUN_TEST(test_connecting_attempt_times_out_at_exactly_fifteen_seconds);
    RUN_TEST(test_disconnect_failure_at_attempt_timeout_is_fatal);
    RUN_TEST(test_retry_starts_only_at_exact_delay_boundary);
    RUN_TEST(test_adapter_failure_while_starting_retry_is_fatal);
    RUN_TEST(test_retries_follow_capped_exponential_schedule_indefinitely);
    RUN_TEST(test_success_resets_next_retry_delay_to_one_second);
    RUN_TEST(test_replacement_resets_backoff_and_retry_uses_new_owned_target);
    RUN_TEST(test_invalid_replacement_preserves_target_used_by_retry);
    RUN_TEST(test_signal_strength_is_forwarded_only_while_connected);
    RUN_TEST(test_signal_strength_is_hidden_after_adapter_error);
    RUN_TEST(test_signal_strength_is_hidden_when_link_drops_before_next_update);
    RUN_TEST(test_logs_describe_wifi_outcomes_without_network_identity_or_credentials);
    return UNITY_END();
}
