#include <unity.h>

#include "hardware/esp32/bluetooth/ble_peer_event_order.h"

namespace {
using namespace cardputer_hub::hardware::bluetooth_detail;

void expect(PeerEventDisposition expected, PeerEventDisposition actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void test_security_and_subscriptions_before_connect_keep_one_peer() {
    BlePeerEventOrder order;
    // Captured NimBLE order: encryption, subscriptions, delayed CONNECT.
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Data));
    expect(PeerEventDisposition::Forward, order.observe(0, PeerEventKind::Data));
    expect(PeerEventDisposition::Forward, order.observe(0, PeerEventKind::Data));
    expect(PeerEventDisposition::Ignore, order.observe(0, PeerEventKind::Connected));
    TEST_ASSERT_EQUAL_UINT16(0, order.active().value());
}

void test_normal_connect_and_duplicate_do_not_reinitialize_peer() {
    BlePeerEventOrder order;
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Connected));
    expect(PeerEventDisposition::Forward, order.observe(0, PeerEventKind::Data));
    expect(PeerEventDisposition::Ignore, order.observe(0, PeerEventKind::Connected));
}

void test_disconnect_and_new_lifecycle_allow_handle_reuse() {
    BlePeerEventOrder order;
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Connected));
    expect(PeerEventDisposition::Forward, order.observe(0, PeerEventKind::Disconnected));
    TEST_ASSERT_FALSE(order.active().has_value());
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Data));
    order.reset();
    TEST_ASSERT_FALSE(order.active().has_value());
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Data));
}

void test_other_connection_cannot_replace_or_clear_current_peer() {
    BlePeerEventOrder order;
    expect(PeerEventDisposition::Ignore, order.observe(7, PeerEventKind::Disconnected));
    expect(PeerEventDisposition::AnnounceConnection, order.observe(0, PeerEventKind::Connected));
    expect(PeerEventDisposition::Reject, order.observe(7, PeerEventKind::Data));
    expect(PeerEventDisposition::Reject, order.observe(7, PeerEventKind::Disconnected));
    TEST_ASSERT_EQUAL_UINT16(0, order.active().value());
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_security_and_subscriptions_before_connect_keep_one_peer);
    RUN_TEST(test_normal_connect_and_duplicate_do_not_reinitialize_peer);
    RUN_TEST(test_disconnect_and_new_lifecycle_allow_handle_reuse);
    RUN_TEST(test_other_connection_cannot_replace_or_clear_current_peer);
    return UNITY_END();
}
