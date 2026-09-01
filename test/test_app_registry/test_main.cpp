#include <unity.h>

#include <string>

#include "core/app_registry/app_registry.h"

namespace {

using cardputer_hub::core::AppDescriptor;
using cardputer_hub::core::AppRegistrationResult;
using cardputer_hub::core::AppRegistry;

void test_empty_registry_has_no_apps() {
    AppRegistry registry;

    TEST_ASSERT_TRUE(registry.apps().empty());
    TEST_ASSERT_NULL(registry.find("weather"));
    TEST_ASSERT_NULL(registry.find(""));
}

void test_registration_owns_all_descriptor_metadata() {
    AppRegistry registry;
    AppDescriptor descriptor{
        "weather", "Weather", "weather-icon", "weather/home", {"WIFI", "WEATHER_SERVICE"}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(descriptor)));

    descriptor.id = "changed-id";
    descriptor.displayName = "Changed name";
    descriptor.iconId = "changed-icon";
    descriptor.entryRoute = "changed/route";
    descriptor.requiredCapabilities[0] = "CHANGED_CAPABILITY";
    descriptor.requiredCapabilities.push_back("ANOTHER_CAPABILITY");

    const auto* registered = registry.find("weather");
    TEST_ASSERT_NOT_NULL(registered);
    TEST_ASSERT_EQUAL_STRING("weather", registered->id.c_str());
    TEST_ASSERT_EQUAL_STRING("Weather", registered->displayName.c_str());
    TEST_ASSERT_EQUAL_STRING("weather-icon", registered->iconId.c_str());
    TEST_ASSERT_EQUAL_STRING("weather/home", registered->entryRoute.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, registered->requiredCapabilities.size());
    TEST_ASSERT_EQUAL_STRING("WIFI", registered->requiredCapabilities[0].c_str());
    TEST_ASSERT_EQUAL_STRING("WEATHER_SERVICE", registered->requiredCapabilities[1].c_str());
    TEST_ASSERT_NULL(registry.find("changed-id"));
}

void test_registration_accepts_empty_icon_and_preserves_order() {
    AppRegistry registry;
    const AppDescriptor weather{"weather", "Weather", "weather-icon", "weather/home", {"WIFI"}};
    const AppDescriptor settings{"settings", "Settings", "", "settings/home", {}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(weather)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(settings)));

    const auto& apps = registry.apps();
    TEST_ASSERT_EQUAL_UINT32(2, apps.size());
    TEST_ASSERT_EQUAL_STRING("weather", apps[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("settings", apps[1].id.c_str());
    TEST_ASSERT_EQUAL_STRING("", apps[1].iconId.c_str());
    TEST_ASSERT_EQUAL_PTR(&apps[0], registry.find("weather"));
    TEST_ASSERT_EQUAL_PTR(&apps[1], registry.find("settings"));
    TEST_ASSERT_NULL(registry.find("Weather"));
}

void test_invalid_required_fields_do_not_mutate_registry() {
    AppRegistry registry;
    const AppDescriptor baseline{"settings", "Settings", "", "settings/home", {}};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(baseline)));

    const AppDescriptor emptyId{"", "Weather", "weather-icon", "weather/home", {"WIFI"}};
    const AppDescriptor emptyName{"weather", "", "weather-icon", "weather/home", {"WIFI"}};
    const AppDescriptor emptyRoute{"weather", "Weather", "weather-icon", "", {"WIFI"}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::InvalidDescriptor),
                            static_cast<unsigned int>(registry.registerApp(emptyId)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::InvalidDescriptor),
                            static_cast<unsigned int>(registry.registerApp(emptyName)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::InvalidDescriptor),
                            static_cast<unsigned int>(registry.registerApp(emptyRoute)));

    TEST_ASSERT_EQUAL_UINT32(1, registry.apps().size());
    TEST_ASSERT_EQUAL_STRING("settings", registry.apps()[0].id.c_str());
    TEST_ASSERT_NULL(registry.find("weather"));
}

void test_invalid_capability_requirements_do_not_mutate_registry() {
    AppRegistry registry;
    const AppDescriptor emptyCapability{
        "weather", "Weather", "weather-icon", "weather/home", {"WIFI", ""}};
    const AppDescriptor duplicateCapability{"devices",
                                            "Devices",
                                            "devices-icon",
                                            "devices/home",
                                            {"BLUETOOTH", "HOST_SERVICE", "BLUETOOTH"}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::InvalidDescriptor),
                            static_cast<unsigned int>(registry.registerApp(emptyCapability)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::InvalidDescriptor),
                            static_cast<unsigned int>(registry.registerApp(duplicateCapability)));
    TEST_ASSERT_TRUE(registry.apps().empty());

    const AppDescriptor exactCaseCapabilities{
        "case-test", "Case Test", "", "case-test/home", {"WIFI", "wifi"}};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(exactCaseCapabilities)));
    TEST_ASSERT_EQUAL_UINT32(1, registry.apps().size());
}

void test_duplicate_ids_preserve_original_while_different_case_is_distinct() {
    AppRegistry registry;
    const AppDescriptor weather{"weather", "Weather", "weather-icon", "weather/home", {"WIFI"}};
    const AppDescriptor settings{"settings", "Settings", "", "settings/home", {}};
    const AppDescriptor replacement{
        "weather", "Replacement", "replacement-icon", "replacement/home", {"RGB_PANEL"}};

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(weather)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(settings)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::DuplicateId),
                            static_cast<unsigned int>(registry.registerApp(replacement)));

    const auto& appsAfterDuplicate = registry.apps();
    TEST_ASSERT_EQUAL_UINT32(2, appsAfterDuplicate.size());
    TEST_ASSERT_EQUAL_STRING("weather", appsAfterDuplicate[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("Weather", appsAfterDuplicate[0].displayName.c_str());
    TEST_ASSERT_EQUAL_STRING("settings", appsAfterDuplicate[1].id.c_str());
    TEST_ASSERT_EQUAL_PTR(&appsAfterDuplicate[0], registry.find("weather"));

    const AppDescriptor differentlyCased{
        "Weather", "Weather Preview", "", "preview/home", {"WIFI"}};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(AppRegistrationResult::Registered),
                            static_cast<unsigned int>(registry.registerApp(differentlyCased)));

    const auto& apps = registry.apps();
    TEST_ASSERT_EQUAL_UINT32(3, apps.size());
    TEST_ASSERT_EQUAL_STRING("Weather", apps[2].id.c_str());
    TEST_ASSERT_EQUAL_PTR(&apps[2], registry.find("Weather"));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_registry_has_no_apps);
    RUN_TEST(test_registration_owns_all_descriptor_metadata);
    RUN_TEST(test_registration_accepts_empty_icon_and_preserves_order);
    RUN_TEST(test_invalid_required_fields_do_not_mutate_registry);
    RUN_TEST(test_invalid_capability_requirements_do_not_mutate_registry);
    RUN_TEST(test_duplicate_ids_preserve_original_while_different_case_is_distinct);
    return UNITY_END();
}
