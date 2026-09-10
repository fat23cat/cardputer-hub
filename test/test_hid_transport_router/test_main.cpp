#include <unity.h>

#include <chrono>
#include <deque>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "connectivity/bluetooth/bluetooth_hid_target.h"
#include "connectivity/hid/hid_transport_router.h"

namespace {

using namespace cardputer_hub::connectivity;
using namespace std::chrono_literals;

class FakeTransport : public IHidTransport {
  public:
    HidTransportState state() const noexcept override { return currentState; }

    HidSendResult send(const HidReport& report) override {
        ++sendCalls;
        const auto result = next(sendResults, sendResult);
        if (result == HidSendResult::Sent) {
            sentReports.push_back(report);
        }
        return result;
    }

    HidSendResult releaseAll() override {
        ++releaseCalls;
        return next(releaseResults, releaseResult);
    }

    static HidSendResult next(std::deque<HidSendResult>& results, HidSendResult fallback) {
        if (results.empty()) {
            return fallback;
        }
        const auto result = results.front();
        results.pop_front();
        return result;
    }

    HidTransportState currentState = HidTransportState::Unavailable;
    HidSendResult sendResult = HidSendResult::Sent;
    HidSendResult releaseResult = HidSendResult::Sent;
    std::deque<HidSendResult> sendResults;
    std::deque<HidSendResult> releaseResults;
    std::vector<HidReport> sentReports;
    int sendCalls = 0;
    int releaseCalls = 0;
};

class FakeUsbTransport final : public IUsbHidTransport {
  public:
    HidTransportState state() const noexcept override { return transport.state(); }
    HidSendResult send(const HidReport& report) override { return transport.send(report); }
    HidSendResult releaseAll() override { return transport.releaseAll(); }
    HidPhysicalLinkState linkState() const noexcept override { return link; }

    FakeTransport transport;
    HidPhysicalLinkState link = HidPhysicalLinkState::Disconnected;
};

class FakeBluetoothTarget final : public IBluetoothHidTarget {
  public:
    BluetoothBondSelectionResult
    selectBond(std::optional<BluetoothBondReference> reference) override {
        requests.push_back(reference);
        if (selectionResult == BluetoothBondSelectionResult::Selected ||
            selectionResult == BluetoothBondSelectionResult::Cleared ||
            selectionResult == BluetoothBondSelectionResult::AlreadySelected) {
            selection = reference;
        }
        return selectionResult;
    }

    std::optional<BluetoothBondReference> selectedBond() const noexcept override {
        return selection;
    }

    IHidTransport& hidTransport() noexcept override { return transport; }

    FakeTransport transport;
    BluetoothBondSelectionResult selectionResult = BluetoothBondSelectionResult::Selected;
    std::optional<BluetoothBondReference> selection;
    std::vector<std::optional<BluetoothBondReference>> requests;
};

BluetoothBondReference bond(std::uint8_t value) {
    BluetoothBondReference reference;
    reference.bytes[0] = value;
    return reference;
}

HidTransaction keyboardTransaction(std::chrono::milliseconds dwell = 10ms) {
    return {{{HidKeyboardReport{0x02, {0x04}}, dwell}, {HidKeyboardReport::neutral(), 0ms}}};
}

HidTransaction consumerTransaction(std::chrono::milliseconds dwell = 10ms) {
    return {{{HidConsumerReport{0x00E9}, dwell}, {HidConsumerReport::neutral(), 0ms}}};
}

void readyUsb(FakeUsbTransport& usb) {
    usb.link = HidPhysicalLinkState::Connected;
    usb.transport.currentState = HidTransportState::Ready;
}

void readyBle(FakeBluetoothTarget& ble, BluetoothBondReference reference) {
    ble.selection = reference;
    ble.transport.currentState = HidTransportState::Ready;
}

void test_transaction_validation_rejects_empty_oversized_negative_and_non_neutral_endings() {
    TEST_ASSERT_FALSE(isValidHidTransaction({}));

    HidTransaction oversized;
    oversized.frames.resize(HidTransaction::maximumFrameCount + 1,
                            {HidKeyboardReport::neutral(), 0ms});
    TEST_ASSERT_FALSE(isValidHidTransaction(oversized));
    TEST_ASSERT_FALSE(isValidHidTransaction({{{HidKeyboardReport::neutral(), -1ms}}}));
    TEST_ASSERT_FALSE(isValidHidTransaction({{{HidKeyboardReport{0, {0x04}}, 0ms}}}));
    TEST_ASSERT_FALSE(isValidHidTransaction(
        {{{HidKeyboardReport{0, {0x04, 0x04}}, 0ms}, {HidKeyboardReport::neutral(), 0ms}}}));
    TEST_ASSERT_FALSE(isValidHidTransaction(
        {{{HidConsumerReport{0x00E9}, 0ms}, {HidKeyboardReport::neutral(), 0ms}}}));
}

void test_transaction_validation_accepts_owned_mixed_neutral_ending_sequence() {
    HidTransaction transaction{{{HidKeyboardReport{0x02, {0x04}}, 4ms},
                                {HidKeyboardReport::neutral(), 0ms},
                                {HidConsumerReport{0x00CD}, 6ms},
                                {HidConsumerReport::neutral(), 0ms}}};
    TEST_ASSERT_TRUE(isValidHidTransaction(transaction));
    transaction.frames[0].report = HidKeyboardReport::neutral();
    TEST_ASSERT_TRUE(isValidHidTransaction(transaction));
}

void test_idle_selection_prefers_genuinely_ready_usb_then_selected_ble() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    const auto selected = bond(1);
    readyBle(ble, selected);
    HidTransportRouter router(usb, ble);
    router.setBleTarget(selected);
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));

    usb.link = HidPhysicalLinkState::Connected;
    usb.transport.currentState = HidTransportState::Starting;
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));

    readyUsb(usb);
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransport()));

    usb.link = HidPhysicalLinkState::Suspended;
    usb.transport.currentState = HidTransportState::Unavailable;
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));
}

void test_no_or_unavailable_selected_target_never_uses_another_bond() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyBle(ble, bond(1));
    ble.selectionResult = BluetoothBondSelectionResult::NotFound;
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(2));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::NoReadyTransport),
                            static_cast<unsigned int>(router.route(keyboardTransaction())));
    TEST_ASSERT_EQUAL_INT(0, ble.transport.sendCalls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Unavailable),
                            static_cast<unsigned int>(router.state()));
}

void test_usb_precedence_binds_owned_transaction_to_usb_only() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    auto transaction = keyboardTransaction();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(transaction)));
    transaction.frames[1].report = HidKeyboardReport{0, {0x05}};
    router.update(10ms);

    TEST_ASSERT_EQUAL_UINT32(2, usb.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT32(0, ble.transport.sentReports.size());
    TEST_ASSERT_TRUE(std::get<HidKeyboardReport>(usb.transport.sentReports[1]) ==
                     HidKeyboardReport::neutral());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::None),
                            static_cast<unsigned int>(router.activeTransactionTransport()));
}

void test_ble_delivery_forwards_only_to_the_selected_target() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    const auto selected = bond(3);
    readyBle(ble, selected);
    HidTransportRouter router(usb, ble);
    router.setBleTarget(selected);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(consumerTransaction())));
    router.update(10ms);
    TEST_ASSERT_EQUAL_UINT32(2, ble.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT32(0, usb.transport.sentReports.size());
    TEST_ASSERT_TRUE(ble.requests.back().has_value());
    TEST_ASSERT_TRUE(*ble.requests.back() == selected);
}

void test_one_active_transaction_dwell_boundaries_and_busy_submission() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    HidTransportRouter router(usb, ble);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(keyboardTransaction(10ms))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Busy),
                            static_cast<unsigned int>(router.route(consumerTransaction())));
    router.update(9ms);
    TEST_ASSERT_EQUAL_UINT32(1, usb.transport.sentReports.size());
    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT32(2, usb.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
}

void test_send_backpressure_retries_without_rebinding() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    usb.transport.sendResults = {HidSendResult::Busy, HidSendResult::Sent, HidSendResult::Sent};
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(keyboardTransaction(0ms))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransactionTransport()));
    router.update(0ms);
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT32(2, usb.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT32(0, ble.transport.sentReports.size());
}

void test_ble_to_usb_handover_releases_and_never_replays_old_transaction() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyBle(ble, bond(1));
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    (void)router.route(keyboardTransaction(50ms));

    readyUsb(usb);
    router.update(1ms);
    TEST_ASSERT_EQUAL_INT(1, ble.transport.releaseCalls);
    TEST_ASSERT_EQUAL_UINT32(0, usb.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransport()));
}

void test_handover_busy_retries_release_and_rejects_new_transactions() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyBle(ble, bond(1));
    ble.transport.releaseResults = {HidSendResult::Busy, HidSendResult::Sent};
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    (void)router.route(keyboardTransaction(50ms));
    readyUsb(usb);

    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Handover),
                            static_cast<unsigned int>(router.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransport()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransactionTransport()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Busy),
                            static_cast<unsigned int>(router.route(consumerTransaction())));
    router.update(1ms);
    TEST_ASSERT_EQUAL_INT(2, ble.transport.releaseCalls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
}

void test_usb_unmount_cancels_immediately_without_ble_replay() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    (void)router.route(keyboardTransaction(50ms));

    usb.link = HidPhysicalLinkState::Disconnected;
    usb.transport.currentState = HidTransportState::Unavailable;
    router.update(1ms);
    TEST_ASSERT_EQUAL_INT(0, usb.transport.releaseCalls);
    TEST_ASSERT_EQUAL_UINT32(0, ble.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::None),
                            static_cast<unsigned int>(router.activeTransactionTransport()));
}

void test_usb_suspend_waits_for_resume_and_release_or_unmount() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    (void)router.route(consumerTransaction(50ms));

    usb.link = HidPhysicalLinkState::Suspended;
    usb.transport.currentState = HidTransportState::Unavailable;
    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Handover),
                            static_cast<unsigned int>(router.state()));
    TEST_ASSERT_EQUAL_INT(0, usb.transport.releaseCalls);

    readyUsb(usb);
    router.update(1ms);
    TEST_ASSERT_EQUAL_INT(1, usb.transport.releaseCalls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransport()));

    readyUsb(usb);
    (void)router.route(keyboardTransaction(50ms));
    usb.link = HidPhysicalLinkState::Suspended;
    usb.transport.currentState = HidTransportState::Unavailable;
    router.update(1ms);
    usb.link = HidPhysicalLinkState::Disconnected;
    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
}

void test_release_failure_enters_error_and_later_cleanup_recovers() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyBle(ble, bond(1));
    ble.transport.releaseResults = {HidSendResult::AdapterError, HidSendResult::Sent};
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    (void)router.route(keyboardTransaction(50ms));
    readyUsb(usb);

    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Error),
                            static_cast<unsigned int>(router.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::TransportError),
                            static_cast<unsigned int>(router.route(consumerTransaction())));
    router.update(1ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
}

void test_ambiguous_send_failure_never_fails_over_and_requires_cleanup() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    usb.transport.sendResult = HidSendResult::AdapterError;
    usb.transport.releaseResult = HidSendResult::Busy;
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::TransportError),
                            static_cast<unsigned int>(router.route(keyboardTransaction())));
    TEST_ASSERT_EQUAL_UINT32(0, ble.transport.sentReports.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Error),
                            static_cast<unsigned int>(router.state()));
    usb.transport.releaseResult = HidSendResult::Sent;
    router.cancel();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
}

void test_usb_error_cleanup_recovers_after_confirmed_unmount() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    usb.transport.sendResult = HidSendResult::AdapterError;
    usb.transport.releaseResult = HidSendResult::AdapterError;
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::TransportError),
                            static_cast<unsigned int>(router.route(keyboardTransaction())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Error),
                            static_cast<unsigned int>(router.state()));

    usb.link = HidPhysicalLinkState::Disconnected;
    router.update(1ms);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouterState::Idle),
                            static_cast<unsigned int>(router.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));
    ble.transport.sentReports.clear();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(consumerTransaction(0ms))));
}

void test_ble_target_replacement_is_retained_during_usb_ownership_and_after_loss() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyUsb(usb);
    readyBle(ble, bond(1));
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    router.setBleTarget(bond(2));
    TEST_ASSERT_TRUE(ble.selection.has_value());
    TEST_ASSERT_TRUE(*ble.selection == bond(2));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Usb),
                            static_cast<unsigned int>(router.activeTransport()));

    usb.link = HidPhysicalLinkState::Disconnected;
    usb.transport.currentState = HidTransportState::Unavailable;
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportKind::Ble),
                            static_cast<unsigned int>(router.activeTransport()));
    (void)router.route(consumerTransaction(0ms));
    router.update(0ms);
    TEST_ASSERT_EQUAL_UINT32(2, ble.transport.sentReports.size());
}

void test_transport_failures_are_isolated_when_the_other_transport_is_ready() {
    FakeUsbTransport usb;
    FakeBluetoothTarget ble;
    readyBle(ble, bond(1));
    usb.transport.currentState = HidTransportState::Error;
    HidTransportRouter router(usb, ble);
    router.setBleTarget(bond(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(router.route(consumerTransaction(0ms))));
    router.update(0ms);

    FakeUsbTransport otherUsb;
    FakeBluetoothTarget otherBle;
    readyUsb(otherUsb);
    otherBle.transport.currentState = HidTransportState::Error;
    HidTransportRouter otherRouter(otherUsb, otherBle);
    otherRouter.setBleTarget(bond(2));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidRouteResult::Accepted),
                            static_cast<unsigned int>(otherRouter.route(keyboardTransaction(0ms))));
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_transaction_validation_rejects_empty_oversized_negative_and_non_neutral_endings);
    RUN_TEST(test_transaction_validation_accepts_owned_mixed_neutral_ending_sequence);
    RUN_TEST(test_idle_selection_prefers_genuinely_ready_usb_then_selected_ble);
    RUN_TEST(test_no_or_unavailable_selected_target_never_uses_another_bond);
    RUN_TEST(test_usb_precedence_binds_owned_transaction_to_usb_only);
    RUN_TEST(test_ble_delivery_forwards_only_to_the_selected_target);
    RUN_TEST(test_one_active_transaction_dwell_boundaries_and_busy_submission);
    RUN_TEST(test_send_backpressure_retries_without_rebinding);
    RUN_TEST(test_ble_to_usb_handover_releases_and_never_replays_old_transaction);
    RUN_TEST(test_handover_busy_retries_release_and_rejects_new_transactions);
    RUN_TEST(test_usb_unmount_cancels_immediately_without_ble_replay);
    RUN_TEST(test_usb_suspend_waits_for_resume_and_release_or_unmount);
    RUN_TEST(test_release_failure_enters_error_and_later_cleanup_recovers);
    RUN_TEST(test_ambiguous_send_failure_never_fails_over_and_requires_cleanup);
    RUN_TEST(test_usb_error_cleanup_recovers_after_confirmed_unmount);
    RUN_TEST(test_ble_target_replacement_is_retained_during_usb_ownership_and_after_loss);
    RUN_TEST(test_transport_failures_are_isolated_when_the_other_transport_is_ready);
    return UNITY_END();
}
