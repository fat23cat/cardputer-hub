#include <unity.h>

#include <cstdint>
#include <string>
#include <variant>

#include "core/actions/action.h"
#include "core/actions/action_bus.h"

namespace {

using cardputer_hub::core::Action;
using cardputer_hub::core::ActionBus;
using cardputer_hub::core::ActionHandlingResult;
using cardputer_hub::core::ActionValue;
using cardputer_hub::core::DispatchResult;
using cardputer_hub::core::IActionHandler;
using cardputer_hub::core::RegistrationResult;

class RecordingActionHandler final : public IActionHandler {
  public:
    ActionHandlingResult handle(const Action& action) override {
        ++callCount;
        receivedAction = &action;
        return result;
    }

    ActionHandlingResult result = ActionHandlingResult::Handled;
    int callCount = 0;
    const Action* receivedAction = nullptr;
};

void test_action_owns_identifiers_and_string_parameters() {
    std::string id = "host.select";
    std::string source = "input.keyboard";
    std::string hostId = "host-work";
    Action action{id, source, {{"host_id", hostId}}};

    id = "changed";
    source = "changed";
    hostId = "changed";

    TEST_ASSERT_EQUAL_STRING("host.select", action.id.c_str());
    TEST_ASSERT_EQUAL_STRING("input.keyboard", action.source.c_str());
    const auto* value = action.findParameter("host_id");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_TRUE(std::holds_alternative<std::string>(*value));
    TEST_ASSERT_EQUAL_STRING("host-work", std::get<std::string>(*value).c_str());
}

void test_action_preserves_parameter_types_and_reports_missing_parameters() {
    Action action{"indicator.configure",
                  "app.settings",
                  {
                      {"enabled", true},
                      {"brightness", std::int32_t{42}},
                      {"pattern", std::string{"idle"}},
                  }};

    const ActionValue* enabled = action.findParameter("enabled");
    const ActionValue* brightness = action.findParameter("brightness");
    const ActionValue* pattern = action.findParameter("pattern");

    TEST_ASSERT_NOT_NULL(enabled);
    TEST_ASSERT_TRUE(std::holds_alternative<bool>(*enabled));
    TEST_ASSERT_TRUE(std::get<bool>(*enabled));
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_TRUE(std::holds_alternative<std::int32_t>(*brightness));
    TEST_ASSERT_EQUAL_INT32(42, std::get<std::int32_t>(*brightness));
    TEST_ASSERT_NOT_NULL(pattern);
    TEST_ASSERT_TRUE(std::holds_alternative<std::string>(*pattern));
    TEST_ASSERT_EQUAL_STRING("idle", std::get<std::string>(*pattern).c_str());
    TEST_ASSERT_NULL(action.findParameter("missing"));
    TEST_ASSERT_NULL(action.findParameter("Enabled"));
}

void test_action_bus_registers_one_handler_for_an_action_id() {
    ActionBus bus;
    RecordingActionHandler handler;

    const auto result = bus.registerHandler("host.select", handler);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(RegistrationResult::Registered),
                            static_cast<unsigned int>(result));
}

void test_action_bus_rejects_empty_and_duplicate_registration_ids() {
    ActionBus bus;
    RecordingActionHandler firstHandler;
    RecordingActionHandler replacementHandler;

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(RegistrationResult::InvalidId),
                            static_cast<unsigned int>(bus.registerHandler("", firstHandler)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(RegistrationResult::Registered),
        static_cast<unsigned int>(bus.registerHandler("host.select", firstHandler)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(RegistrationResult::DuplicateId),
        static_cast<unsigned int>(bus.registerHandler("host.select", replacementHandler)));
}

void test_action_bus_routes_registered_action_unchanged_and_exactly_once() {
    ActionBus bus;
    RecordingActionHandler handler;
    Action action{"host.select", "input.keyboard", {{"host_id", std::string{"host-work"}}}};
    bus.registerHandler("host.select", handler);

    const auto result = bus.dispatch(action);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Handled),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(1, handler.callCount);
    TEST_ASSERT_EQUAL_PTR(&action, handler.receivedAction);
}

void test_action_bus_propagates_handler_rejection() {
    ActionBus bus;
    RecordingActionHandler handler;
    handler.result = ActionHandlingResult::Rejected;
    bus.registerHandler("host.select", handler);

    const auto result = bus.dispatch({"host.select", "app.device_manager", {}});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Rejected),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(1, handler.callCount);
}

void test_action_bus_reports_unsupported_actions_and_matches_ids_exactly() {
    ActionBus bus;
    RecordingActionHandler handler;
    bus.registerHandler("host.select", handler);

    const auto unsupported = bus.dispatch({"weather.refresh", "app.weather", {}});
    const auto wrongCase = bus.dispatch({"Host.Select", "app.device_manager", {}});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Unsupported),
                            static_cast<unsigned int>(unsupported));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Unsupported),
                            static_cast<unsigned int>(wrongCase));
    TEST_ASSERT_EQUAL_INT(0, handler.callCount);
}

void test_action_bus_rejects_malformed_actions_without_invoking_a_handler() {
    ActionBus bus;
    RecordingActionHandler handler;
    bus.registerHandler("host.select", handler);

    const Action invalidActions[] = {
        {"", "input.keyboard", {}},
        {"host.select", "", {}},
        {"host.select", "input.keyboard", {{"", std::string{"host-work"}}}},
        {"host.select",
         "input.keyboard",
         {{"host_id", std::string{"host-work"}}, {"host_id", std::string{"host-home"}}}},
    };

    for (const auto& action : invalidActions) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Invalid),
                                static_cast<unsigned int>(bus.dispatch(action)));
    }
    TEST_ASSERT_EQUAL_INT(0, handler.callCount);
}

void test_action_bus_keeps_first_handler_and_routes_all_sources_by_action_id() {
    ActionBus bus;
    RecordingActionHandler firstHandler;
    RecordingActionHandler replacementHandler;
    bus.registerHandler("host.select", firstHandler);
    bus.registerHandler("host.select", replacementHandler);

    const auto keyboardResult = bus.dispatch({"host.select", "input.keyboard", {}});
    const auto appResult = bus.dispatch({"host.select", "app.device_manager", {}});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Handled),
                            static_cast<unsigned int>(keyboardResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(DispatchResult::Handled),
                            static_cast<unsigned int>(appResult));
    TEST_ASSERT_EQUAL_INT(2, firstHandler.callCount);
    TEST_ASSERT_EQUAL_INT(0, replacementHandler.callCount);
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_action_owns_identifiers_and_string_parameters);
    RUN_TEST(test_action_preserves_parameter_types_and_reports_missing_parameters);
    RUN_TEST(test_action_bus_registers_one_handler_for_an_action_id);
    RUN_TEST(test_action_bus_rejects_empty_and_duplicate_registration_ids);
    RUN_TEST(test_action_bus_routes_registered_action_unchanged_and_exactly_once);
    RUN_TEST(test_action_bus_propagates_handler_rejection);
    RUN_TEST(test_action_bus_reports_unsupported_actions_and_matches_ids_exactly);
    RUN_TEST(test_action_bus_rejects_malformed_actions_without_invoking_a_handler);
    RUN_TEST(test_action_bus_keeps_first_handler_and_routes_all_sources_by_action_id);
    return UNITY_END();
}
