#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <variant>
#include <vector>

#include "connectivity/usb/native_usb_descriptors.h"
#include "connectivity/usb/native_usb_hid.h"

namespace {

using namespace cardputer_hub::connectivity;

class FakeNativeUsbAdapter final : public INativeUsbAdapter {
  public:
    NativeUsbAdapterResult initialize(std::uint32_t lifecycle) override {
        ++initializeCount;
        initializedLifecycle = lifecycle;
        return initializeResult;
    }

    NativeUsbPollResult pollEvent() override {
        if (events.empty()) {
            return {NativeUsbPollStatus::NoEvent, {}};
        }
        const auto event = events.front();
        events.pop_front();
        return {NativeUsbPollStatus::Event, event};
    }

    NativeUsbAdapterResult sendHidReport(const HidReport& report) override {
        sentReports.push_back(report);
        if (!sendResults.empty()) {
            const auto result = sendResults.front();
            sendResults.pop_front();
            return result;
        }
        return sendResult;
    }

    NativeUsbAdapterResult initializeResult = NativeUsbAdapterResult::Success;
    NativeUsbAdapterResult sendResult = NativeUsbAdapterResult::Success;
    std::deque<NativeUsbAdapterResult> sendResults;
    std::deque<NativeUsbEvent> events;
    std::vector<HidReport> sentReports;
    int initializeCount = 0;
    std::uint32_t initializedLifecycle = 0;
};

NativeUsbEvent event(NativeUsbEventType type, std::uint32_t lifecycle = 1,
                     bool endpointReady = false) {
    return {type, lifecycle, endpointReady};
}

void mountReady(FakeNativeUsbAdapter& adapter, NativeUsbHidService& service) {
    adapter.events.push_back(event(NativeUsbEventType::Mounted));
    adapter.events.push_back(event(NativeUsbEventType::EndpointReadinessChanged, 1, true));
    service.update();
}

void test_descriptors_define_only_composite_cdc_and_hid_without_unique_serial() {
    TEST_ASSERT_EQUAL_UINT8(0, nativeUsbDeviceDescriptor[16]);
    TEST_ASSERT_EQUAL_UINT8(3, nativeUsbConfigurationDescriptor[4]);
    TEST_ASSERT_EQUAL_UINT16(
        nativeUsbConfigurationDescriptor.size(),
        static_cast<std::uint16_t>(nativeUsbConfigurationDescriptor[2]) |
            (static_cast<std::uint16_t>(nativeUsbConfigurationDescriptor[3]) << 8U));
    TEST_ASSERT_EQUAL_STRING("Cardputer Hub", nativeUsbStrings[2]);
    TEST_ASSERT_EQUAL_STRING("Cardputer CDC", nativeUsbStrings[3]);
    TEST_ASSERT_EQUAL_STRING("Cardputer HID", nativeUsbStrings[4]);
    TEST_ASSERT_EQUAL_UINT32(5, nativeUsbStrings.size());

    std::array<bool, 256> endpoints{};
    std::size_t interfaceCount = 0;
    std::size_t offset = 0;
    while (offset < nativeUsbConfigurationDescriptor.size()) {
        const auto length = nativeUsbConfigurationDescriptor[offset];
        TEST_ASSERT_GREATER_THAN_UINT8(1, length);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(nativeUsbConfigurationDescriptor.size(), offset + length);
        const auto type = nativeUsbConfigurationDescriptor[offset + 1];
        if (type == 0x04) {
            ++interfaceCount;
        } else if (type == 0x05) {
            const auto address = nativeUsbConfigurationDescriptor[offset + 2];
            TEST_ASSERT_FALSE(endpoints[address]);
            endpoints[address] = true;
        }
        offset += length;
    }
    TEST_ASSERT_EQUAL_UINT32(nativeUsbConfigurationDescriptor.size(), offset);
    TEST_ASSERT_EQUAL_UINT32(3, interfaceCount);
    TEST_ASSERT_TRUE(endpoints[0x81]);
    TEST_ASSERT_TRUE(endpoints[0x02]);
    TEST_ASSERT_TRUE(endpoints[0x82]);
    TEST_ASSERT_TRUE(endpoints[0x83]);
}

void test_report_descriptor_uses_shared_keyboard_and_consumer_ids() {
    TEST_ASSERT_EQUAL_UINT32(hidReportDescriptor.size(), nativeUsbHidReportDescriptor.size());
    TEST_ASSERT_EQUAL_UINT8(keyboardHidReportId, nativeUsbHidReportDescriptor[7]);
    TEST_ASSERT_EQUAL_UINT8(consumerHidReportId, nativeUsbHidReportDescriptor[72]);
    TEST_ASSERT_EQUAL_UINT8(0, std::memcmp(hidReportDescriptor.data(),
                                           nativeUsbHidReportDescriptor.data(),
                                           hidReportDescriptor.size()));
}

void test_construction_is_side_effect_free_and_initialization_is_checked_and_idempotent() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    TEST_ASSERT_EQUAL_INT(0, adapter.initializeCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.state()));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NativeUsbInitializeResult::Initialized),
                            static_cast<unsigned int>(service.initialize()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(NativeUsbInitializeResult::AlreadyInitialized),
        static_cast<unsigned int>(service.initialize()));
    TEST_ASSERT_EQUAL_INT(1, adapter.initializeCount);

    FakeNativeUsbAdapter failingAdapter;
    failingAdapter.initializeResult = NativeUsbAdapterResult::Error;
    NativeUsbHidService failing(failingAdapter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NativeUsbInitializeResult::AdapterError),
                            static_cast<unsigned int>(failing.initialize()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(failing.state()));
}

void test_ready_requires_mount_resume_and_endpoint_readiness() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Disconnected),
                            static_cast<unsigned int>(service.linkState()));

    adapter.events.push_back(event(NativeUsbEventType::Mounted));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Starting),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Connected),
                            static_cast<unsigned int>(service.linkState()));
    adapter.events.push_back(event(NativeUsbEventType::EndpointReadinessChanged, 1, true));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Ready),
                            static_cast<unsigned int>(service.state()));

    adapter.events.push_back(event(NativeUsbEventType::Suspended));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Suspended),
                            static_cast<unsigned int>(service.linkState()));
    adapter.events.push_back(event(NativeUsbEventType::Resumed));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Starting),
                            static_cast<unsigned int>(service.state()));
    adapter.events.push_back(event(NativeUsbEventType::EndpointReadinessChanged, 1, true));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Ready),
                            static_cast<unsigned int>(service.state()));

    adapter.events.push_back(event(NativeUsbEventType::Unmounted));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Disconnected),
                            static_cast<unsigned int>(service.linkState()));
}

void test_send_preserves_semantics_owns_values_and_reports_backpressure() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    mountReady(adapter, service);

    HidKeyboardReport keyboard{0x03, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09}};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.send(keyboard)));
    keyboard.usages.fill(0);
    TEST_ASSERT_EQUAL_HEX8(0x09, std::get<HidKeyboardReport>(adapter.sentReports[0]).usages[5]);

    adapter.sendResult = NativeUsbAdapterResult::Busy;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Busy),
                            static_cast<unsigned int>(service.send(HidConsumerReport{0x00E9})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Busy),
                            static_cast<unsigned int>(service.state()));
    adapter.sendResult = NativeUsbAdapterResult::Success;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.send(HidConsumerReport::neutral())));
    TEST_ASSERT_EQUAL_HEX16(0x00E9, std::get<HidConsumerReport>(adapter.sentReports[1]).usage);

    const auto sendsBeforeInvalid = adapter.sentReports.size();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(HidSendResult::AdapterError),
        static_cast<unsigned int>(service.send(HidKeyboardReport{0, {0x04, 0x04}})));
    TEST_ASSERT_EQUAL_UINT32(sendsBeforeInvalid, adapter.sentReports.size());
}

void test_release_all_retries_only_the_unsent_neutral_report() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    mountReady(adapter, service);
    adapter.sendResults = {NativeUsbAdapterResult::Success, NativeUsbAdapterResult::Busy,
                           NativeUsbAdapterResult::Success};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Busy),
                            static_cast<unsigned int>(service.releaseAll()));
    TEST_ASSERT_EQUAL_UINT32(2, adapter.sentReports.size());
    TEST_ASSERT_TRUE(std::holds_alternative<HidKeyboardReport>(adapter.sentReports[0]));
    TEST_ASSERT_TRUE(std::holds_alternative<HidConsumerReport>(adapter.sentReports[1]));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.releaseAll()));
    TEST_ASSERT_EQUAL_UINT32(3, adapter.sentReports.size());
    TEST_ASSERT_TRUE(std::holds_alternative<HidConsumerReport>(adapter.sentReports[2]));
}

void test_suspend_preserves_release_requirement_but_unmount_neutralizes_it() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    mountReady(adapter, service);
    adapter.sendResults = {NativeUsbAdapterResult::Success, NativeUsbAdapterResult::Busy};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Busy),
                            static_cast<unsigned int>(service.releaseAll()));

    adapter.events.push_back(event(NativeUsbEventType::Suspended));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::NotReady),
                            static_cast<unsigned int>(service.releaseAll()));
    adapter.events.push_back(event(NativeUsbEventType::Resumed));
    adapter.events.push_back(event(NativeUsbEventType::EndpointReadinessChanged, 1, true));
    service.update();
    adapter.sendResult = NativeUsbAdapterResult::Success;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.releaseAll()));

    adapter.sendResults = {NativeUsbAdapterResult::Success, NativeUsbAdapterResult::Busy};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Busy),
                            static_cast<unsigned int>(service.releaseAll()));
    adapter.events.push_back(event(NativeUsbEventType::Unmounted));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::Sent),
                            static_cast<unsigned int>(service.releaseAll()));
}

void test_stale_events_are_ignored_and_poll_or_adapter_failures_enter_error() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    adapter.events.push_back(event(NativeUsbEventType::Mounted, 99));
    adapter.events.push_back(event(NativeUsbEventType::EndpointReadinessChanged, 99, true));
    service.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Unavailable),
                            static_cast<unsigned int>(service.state()));

    mountReady(adapter, service);
    adapter.sendResult = NativeUsbAdapterResult::Error;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::AdapterError),
                            static_cast<unsigned int>(service.send(HidConsumerReport{0x00CD})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(service.state()));

    FakeNativeUsbAdapter eventAdapter;
    NativeUsbHidService eventService(eventAdapter);
    (void)eventService.initialize();
    eventAdapter.events.push_back(event(NativeUsbEventType::AdapterError));
    eventService.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(eventService.state()));
}

void test_fatal_send_error_still_observes_a_later_unmount() {
    FakeNativeUsbAdapter adapter;
    NativeUsbHidService service(adapter);
    (void)service.initialize();
    mountReady(adapter, service);

    adapter.sendResult = NativeUsbAdapterResult::Error;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidSendResult::AdapterError),
                            static_cast<unsigned int>(service.send(HidConsumerReport{0x00CD})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Connected),
                            static_cast<unsigned int>(service.linkState()));

    adapter.events.push_back(event(NativeUsbEventType::Unmounted));
    service.update();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidTransportState::Error),
                            static_cast<unsigned int>(service.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(HidPhysicalLinkState::Disconnected),
                            static_cast<unsigned int>(service.linkState()));
}

void test_bounded_callback_queue_reports_overflow_instead_of_replaying_stale_events() {
    NativeUsbEventQueue queue;
    for (std::size_t index = 0; index < NativeUsbEventQueue::capacity; ++index) {
        TEST_ASSERT_TRUE(queue.push(event(NativeUsbEventType::Mounted)));
    }
    TEST_ASSERT_FALSE(queue.push(event(NativeUsbEventType::Unmounted)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NativeUsbPollStatus::AdapterError),
                            static_cast<unsigned int>(queue.pop().status));
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_descriptors_define_only_composite_cdc_and_hid_without_unique_serial);
    RUN_TEST(test_report_descriptor_uses_shared_keyboard_and_consumer_ids);
    RUN_TEST(test_construction_is_side_effect_free_and_initialization_is_checked_and_idempotent);
    RUN_TEST(test_ready_requires_mount_resume_and_endpoint_readiness);
    RUN_TEST(test_send_preserves_semantics_owns_values_and_reports_backpressure);
    RUN_TEST(test_release_all_retries_only_the_unsent_neutral_report);
    RUN_TEST(test_suspend_preserves_release_requirement_but_unmount_neutralizes_it);
    RUN_TEST(test_stale_events_are_ignored_and_poll_or_adapter_failures_enter_error);
    RUN_TEST(test_fatal_send_error_still_observes_a_later_unmount);
    RUN_TEST(test_bounded_callback_queue_reports_overflow_instead_of_replaying_stale_events);
    return UNITY_END();
}
