#include <unity.h>

#include <string>

#include "core/navigation/navigation_stack.h"

namespace {

using cardputer_hub::core::BackResult;
using cardputer_hub::core::NavigationResult;
using cardputer_hub::core::NavigationStack;

void test_empty_stack_has_no_current_route_and_safe_back() {
    NavigationStack navigation;

    TEST_ASSERT_EQUAL_UINT32(0, navigation.depth());
    TEST_ASSERT_NULL(navigation.current());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::Empty),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT32(0, navigation.depth());
    TEST_ASSERT_NULL(navigation.current());
}

void test_reset_sets_exact_root_and_replaces_populated_history() {
    NavigationStack navigation;

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.resetTo("Launcher")));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("app.weather")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("forecast")));
    TEST_ASSERT_EQUAL_UINT32(3, navigation.depth());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.resetTo("launcher")));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("launcher", navigation.current()->c_str());
}

void test_reset_owns_route_id_after_caller_mutation() {
    NavigationStack navigation;
    std::string route = "app.weather";

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.resetTo(route)));
    route = "changed";

    TEST_ASSERT_EQUAL_STRING("app.weather", navigation.current()->c_str());
}

void test_invalid_reset_preserves_existing_history() {
    NavigationStack navigation;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.resetTo("Launcher")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::InvalidRoute),
                            static_cast<unsigned int>(navigation.resetTo("")));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());
}

void test_push_builds_history_from_empty_in_insertion_order() {
    NavigationStack navigation;

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("Launcher")));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("app.weather")));
    TEST_ASSERT_EQUAL_UINT32(2, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("app.weather", navigation.current()->c_str());
}

void test_push_owns_route_ids_and_keeps_repeated_entries_distinct() {
    NavigationStack navigation;
    std::string route = "app.weather.details";

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push(route)));
    route = "changed";
    TEST_ASSERT_EQUAL_STRING("app.weather.details", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("app.weather.details")));
    TEST_ASSERT_EQUAL_UINT32(2, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("app.weather.details", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::Popped),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("app.weather.details", navigation.current()->c_str());
}

void test_invalid_push_preserves_existing_history() {
    NavigationStack navigation;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("Launcher")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::InvalidRoute),
                            static_cast<unsigned int>(navigation.push("")));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());
}

void test_back_traverses_once_per_call_and_preserves_root() {
    NavigationStack navigation;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.resetTo("Launcher")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("app.vps")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NavigationResult::Navigated),
                            static_cast<unsigned int>(navigation.push("server.details")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::Popped),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT32(2, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("app.vps", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::Popped),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::AtRoot),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(BackResult::AtRoot),
                            static_cast<unsigned int>(navigation.back()));
    TEST_ASSERT_EQUAL_UINT32(1, navigation.depth());
    TEST_ASSERT_EQUAL_STRING("Launcher", navigation.current()->c_str());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_stack_has_no_current_route_and_safe_back);
    RUN_TEST(test_reset_sets_exact_root_and_replaces_populated_history);
    RUN_TEST(test_reset_owns_route_id_after_caller_mutation);
    RUN_TEST(test_invalid_reset_preserves_existing_history);
    RUN_TEST(test_push_builds_history_from_empty_in_insertion_order);
    RUN_TEST(test_push_owns_route_ids_and_keeps_repeated_entries_distinct);
    RUN_TEST(test_invalid_push_preserves_existing_history);
    RUN_TEST(test_back_traverses_once_per_call_and_preserves_root);
    return UNITY_END();
}
