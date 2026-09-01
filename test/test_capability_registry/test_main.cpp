#include <unity.h>

#include <string>

#include "core/capabilities/capability_registry.h"

namespace {

using cardputer_hub::core::CapabilityRegistrationResult;
using cardputer_hub::core::CapabilityRegistry;
using cardputer_hub::core::CapabilityRemovalResult;

void test_empty_registry_has_no_available_capabilities() {
    CapabilityRegistry registry;

    TEST_ASSERT_TRUE(registry.availableCapabilities().empty());
    TEST_ASSERT_FALSE(registry.isAvailable("WIFI"));
    TEST_ASSERT_FALSE(registry.isAvailable(""));
}

void test_registration_owns_ids_and_preserves_order_and_exact_case() {
    CapabilityRegistry registry;
    std::string wifi = "WIFI";

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability(wifi)));
    wifi = "changed";
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("BLE_HID")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("wifi")));

    const auto& capabilities = registry.availableCapabilities();
    TEST_ASSERT_EQUAL_UINT32(3, capabilities.size());
    TEST_ASSERT_EQUAL_STRING("WIFI", capabilities[0].c_str());
    TEST_ASSERT_EQUAL_STRING("BLE_HID", capabilities[1].c_str());
    TEST_ASSERT_EQUAL_STRING("wifi", capabilities[2].c_str());
}

void test_invalid_and_duplicate_registration_leave_order_unchanged() {
    CapabilityRegistry registry;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::DuplicateId),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::InvalidId),
                            static_cast<unsigned int>(registry.registerCapability("")));

    const auto& capabilities = registry.availableCapabilities();
    TEST_ASSERT_EQUAL_UINT32(1, capabilities.size());
    TEST_ASSERT_EQUAL_STRING("WIFI", capabilities[0].c_str());
}

void test_availability_queries_are_exact_and_case_sensitive() {
    CapabilityRegistry registry;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));

    TEST_ASSERT_TRUE(registry.isAvailable("WIFI"));
    TEST_ASSERT_FALSE(registry.isAvailable("wifi"));
    TEST_ASSERT_FALSE(registry.isAvailable("BLE_HID"));
    TEST_ASSERT_FALSE(registry.isAvailable(""));
    TEST_ASSERT_EQUAL_UINT32(1, registry.availableCapabilities().size());
}

void test_removal_updates_availability_and_preserves_remaining_order() {
    CapabilityRegistry registry;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("BLE_HID")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("RGB_PANEL")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRemovalResult::Removed),
                            static_cast<unsigned int>(registry.removeCapability("BLE_HID")));
    TEST_ASSERT_FALSE(registry.isAvailable("BLE_HID"));

    const auto& capabilities = registry.availableCapabilities();
    TEST_ASSERT_EQUAL_UINT32(2, capabilities.size());
    TEST_ASSERT_EQUAL_STRING("WIFI", capabilities[0].c_str());
    TEST_ASSERT_EQUAL_STRING("RGB_PANEL", capabilities[1].c_str());
}

void test_invalid_and_missing_removal_preserve_state_and_reregistration_appends() {
    CapabilityRegistry registry;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("BLE_HID")));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRemovalResult::InvalidId),
                            static_cast<unsigned int>(registry.removeCapability("")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRemovalResult::NotFound),
                            static_cast<unsigned int>(registry.removeCapability("wifi")));
    TEST_ASSERT_EQUAL_UINT32(2, registry.availableCapabilities().size());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRemovalResult::Removed),
                            static_cast<unsigned int>(registry.removeCapability("WIFI")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(CapabilityRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerCapability("WIFI")));

    const auto& capabilities = registry.availableCapabilities();
    TEST_ASSERT_EQUAL_UINT32(2, capabilities.size());
    TEST_ASSERT_EQUAL_STRING("BLE_HID", capabilities[0].c_str());
    TEST_ASSERT_EQUAL_STRING("WIFI", capabilities[1].c_str());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_registry_has_no_available_capabilities);
    RUN_TEST(test_registration_owns_ids_and_preserves_order_and_exact_case);
    RUN_TEST(test_invalid_and_duplicate_registration_leave_order_unchanged);
    RUN_TEST(test_availability_queries_are_exact_and_case_sensitive);
    RUN_TEST(test_removal_updates_availability_and_preserves_remaining_order);
    RUN_TEST(test_invalid_and_missing_removal_preserve_state_and_reregistration_appends);
    return UNITY_END();
}
