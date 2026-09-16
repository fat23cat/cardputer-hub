#include <unity.h>

#include <string>
#include <vector>

#include "apps/runtime/mini_app_runtime.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"
#include "core/input/input_event.h"

namespace {

using cardputer_hub::apps::IMiniApp;
using cardputer_hub::apps::MiniAppActivationResult;
using cardputer_hub::apps::MiniAppDeactivationResult;
using cardputer_hub::apps::MiniAppEligibility;
using cardputer_hub::apps::MiniAppInstanceRegistrationResult;
using cardputer_hub::apps::MiniAppRuntime;
using cardputer_hub::apps::MiniAppUpdateResult;
using cardputer_hub::core::AppDescriptor;
using cardputer_hub::core::AppRegistrationResult;
using cardputer_hub::core::AppRegistry;
using cardputer_hub::core::CapabilityRegistrationResult;
using cardputer_hub::core::CapabilityRegistry;
using cardputer_hub::core::CapabilityRemovalResult;
using cardputer_hub::core::InputEvent;
using cardputer_hub::core::InputEvents;
using cardputer_hub::core::InputEventType;
using cardputer_hub::core::NamedKey;

template <typename Result> void assertResult(Result actual, Result expected) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(expected), static_cast<unsigned int>(actual));
}

class FakeMiniApp final : public IMiniApp {
  public:
    FakeMiniApp() = default;
    FakeMiniApp(const char* id, std::vector<std::string>& lifecycle)
        : id_(id), lifecycle_(&lifecycle) {}

    void onActivate() override {
        ++activateCount;
        record("activate");
    }

    void onDeactivate() override {
        ++deactivateCount;
        record("deactivate");
    }

    void update(const InputEvents& input, std::chrono::milliseconds elapsed) override {
        ++updateCount;
        lastInput = input;
        lastElapsed = elapsed;
        record("update");
    }

    int activateCount = 0;
    int deactivateCount = 0;
    int updateCount = 0;
    InputEvents lastInput;
    std::chrono::milliseconds lastElapsed{0};
    std::vector<std::string> log;

  private:
    void record(const char* event) {
        log.emplace_back(event);
        if (lifecycle_ != nullptr)
            lifecycle_->push_back(std::string(id_) + "." + event);
    }

    const char* id_ = "";
    std::vector<std::string>* lifecycle_ = nullptr;
};

struct RuntimeFixture {
    AppRegistry apps;
    CapabilityRegistry capabilities;
    MiniAppRuntime runtime{apps, capabilities};
    std::vector<std::string> lifecycle;
    FakeMiniApp weather{"weather", lifecycle};
    FakeMiniApp devices{"devices", lifecycle};

    void registerDescriptor(std::string id, std::vector<std::string> required = {}) {
        const AppDescriptor descriptor{id, id, "", id + "/home", std::move(required)};
        assertResult(apps.registerApp(descriptor), AppRegistrationResult::Registered);
    }

    void registerCapability(const char* id) {
        assertResult(capabilities.registerCapability(id), CapabilityRegistrationResult::Registered);
    }
};

const InputEvent tab{InputEventType::NamedKey, 0, NamedKey::Tab, {}};

void test_empty_id_registration_is_rejected() {
    RuntimeFixture f;
    f.registerDescriptor("weather");

    assertResult(f.runtime.registerInstance("", f.weather),
                 MiniAppInstanceRegistrationResult::InvalidId);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_FALSE(f.runtime.activeAppId().has_value());
    TEST_ASSERT_EQUAL_INT(0, f.weather.activateCount);
}

void test_unknown_descriptor_registration_is_rejected() {
    RuntimeFixture f;

    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::UnknownDescriptor);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(0, f.weather.activateCount);
}

void test_successful_registration_is_exact_and_does_not_activate() {
    RuntimeFixture f;
    f.registerDescriptor("weather");

    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_FALSE(f.runtime.activeAppId().has_value());
    TEST_ASSERT_EQUAL_INT(0, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.deactivateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.updateCount);
}

void test_registration_ids_are_case_sensitive() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    f.registerDescriptor("Weather");

    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("Weather", f.devices),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("WEATHER", f.devices),
                 MiniAppInstanceRegistrationResult::UnknownDescriptor);
}

void test_duplicate_instance_registration_preserves_the_original() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    FakeMiniApp replacement;

    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("weather", replacement),
                 MiniAppInstanceRegistrationResult::DuplicateId);

    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, replacement.activateCount);
    TEST_ASSERT_TRUE(f.runtime.activeAppId().has_value());
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
}

void test_eligibility_unknown_app_and_missing_instance() {
    RuntimeFixture f;
    f.registerDescriptor("weather");

    assertResult(f.runtime.eligibility("missing"), MiniAppEligibility::UnknownApp);
    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::MissingInstance);

    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::Eligible);
}

void test_zero_capability_requirements_are_eligible() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);

    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::Eligible);
}

void test_required_capabilities_must_all_be_available() {
    RuntimeFixture f;
    f.registerDescriptor("weather", {"WIFI", "WEATHER_SERVICE"});
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    f.registerCapability("WIFI");
    f.registerCapability("BLUETOOTH");

    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::MissingCapability);

    f.registerCapability("WEATHER_SERVICE");
    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::Eligible);

    assertResult(f.capabilities.removeCapability("WIFI"), CapabilityRemovalResult::Removed);
    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::MissingCapability);
}

void test_unrelated_unavailable_capability_does_not_affect_eligibility() {
    RuntimeFixture f;
    f.registerDescriptor("weather", {"WIFI"});
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    f.registerCapability("WIFI");

    assertResult(f.runtime.eligibility("weather"), MiniAppEligibility::Eligible);
}

void test_eligible_application_activates_once() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);

    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);
    TEST_ASSERT_TRUE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.deactivateCount);
}

void test_reactivating_the_same_app_is_already_active() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::AlreadyActive);
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.deactivateCount);
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
}

void test_reactivating_the_active_app_is_already_active_before_capability_loss() {
    RuntimeFixture f;
    f.registerDescriptor("weather", {"WIFI"});
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    f.registerCapability("WIFI");
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.capabilities.removeCapability("WIFI"), CapabilityRemovalResult::Removed);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::AlreadyActive);
    TEST_ASSERT_TRUE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.deactivateCount);

    assertResult(f.runtime.update({tab}, std::chrono::milliseconds(16)),
                 MiniAppUpdateResult::DeactivatedMissingCapability);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.weather.deactivateCount);
}

void test_failed_activation_does_not_mutate_active_state() {
    RuntimeFixture f;
    f.registerDescriptor("weather", {"WIFI"});
    f.registerDescriptor("devices");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("devices", f.devices),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("devices"), MiniAppActivationResult::Activated);

    assertResult(f.runtime.activate("missing"), MiniAppActivationResult::UnknownApp);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::MissingCapability);
    FakeMiniApp orphan;
    f.registerDescriptor("orphan");
    assertResult(f.runtime.activate("orphan"), MiniAppActivationResult::MissingInstance);

    TEST_ASSERT_EQUAL_STRING("devices", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_INT(1, f.devices.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.devices.deactivateCount);
    TEST_ASSERT_EQUAL_INT(0, f.weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, orphan.activateCount);
}

void test_switch_deactivates_old_app_before_activating_new_app() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    f.registerDescriptor("devices");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("devices", f.devices),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.runtime.activate("devices"), MiniAppActivationResult::Activated);
    TEST_ASSERT_EQUAL_STRING("devices", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_UINT32(3, f.lifecycle.size());
    TEST_ASSERT_EQUAL_STRING("weather.activate", f.lifecycle[0].c_str());
    TEST_ASSERT_EQUAL_STRING("weather.deactivate", f.lifecycle[1].c_str());
    TEST_ASSERT_EQUAL_STRING("devices.activate", f.lifecycle[2].c_str());

    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_UINT32(5, f.lifecycle.size());
    TEST_ASSERT_EQUAL_STRING("devices.deactivate", f.lifecycle[3].c_str());
    TEST_ASSERT_EQUAL_STRING("weather.activate", f.lifecycle[4].c_str());
}

void test_failed_switch_preserves_the_active_application() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    f.registerDescriptor("devices", {"BLUETOOTH"});
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("devices", f.devices),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.runtime.activate("devices"), MiniAppActivationResult::MissingCapability);
    TEST_ASSERT_EQUAL_STRING("weather", f.runtime.activeAppId()->c_str());
    TEST_ASSERT_EQUAL_INT(0, f.weather.deactivateCount);
    TEST_ASSERT_EQUAL_INT(0, f.devices.activateCount);
}

void test_deactivation_clears_active_identity_exactly_once() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.runtime.deactivate(), MiniAppDeactivationResult::Deactivated);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_FALSE(f.runtime.activeAppId().has_value());
    TEST_ASSERT_EQUAL_INT(1, f.weather.deactivateCount);

    assertResult(f.runtime.deactivate(), MiniAppDeactivationResult::AlreadyInactive);
    TEST_ASSERT_EQUAL_INT(1, f.weather.deactivateCount);
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
}

void test_idle_update_forwards_nothing() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);

    assertResult(f.runtime.update({tab}, std::chrono::milliseconds(16)), MiniAppUpdateResult::Idle);
    TEST_ASSERT_EQUAL_INT(0, f.weather.updateCount);
    TEST_ASSERT_TRUE(f.weather.lastInput.empty());
}

void test_active_update_forwards_exact_input_and_elapsed() {
    RuntimeFixture f;
    f.registerDescriptor("weather");
    f.registerDescriptor("devices");
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.registerInstance("devices", f.devices),
                 MiniAppInstanceRegistrationResult::Registered);
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    const InputEvents input{tab};
    assertResult(f.runtime.update(input, std::chrono::milliseconds(33)),
                 MiniAppUpdateResult::Updated);
    TEST_ASSERT_EQUAL_INT(1, f.weather.updateCount);
    TEST_ASSERT_EQUAL_INT(0, f.devices.updateCount);
    TEST_ASSERT_EQUAL_UINT32(1, f.weather.lastInput.size());
    TEST_ASSERT_TRUE(f.weather.lastInput.front().type == InputEventType::NamedKey);
    TEST_ASSERT_TRUE(f.weather.lastInput.front().namedKey == NamedKey::Tab);
    TEST_ASSERT_EQUAL_INT64(33, f.weather.lastElapsed.count());
}

void test_required_capability_loss_deactivates_before_update() {
    RuntimeFixture f;
    f.registerDescriptor("weather", {"WIFI"});
    assertResult(f.runtime.registerInstance("weather", f.weather),
                 MiniAppInstanceRegistrationResult::Registered);
    f.registerCapability("WIFI");
    f.registerCapability("BLUETOOTH");
    assertResult(f.runtime.activate("weather"), MiniAppActivationResult::Activated);

    assertResult(f.capabilities.removeCapability("BLUETOOTH"), CapabilityRemovalResult::Removed);
    assertResult(f.runtime.update({tab}, std::chrono::milliseconds(16)),
                 MiniAppUpdateResult::Updated);
    TEST_ASSERT_EQUAL_INT(1, f.weather.updateCount);

    assertResult(f.capabilities.removeCapability("WIFI"), CapabilityRemovalResult::Removed);
    assertResult(f.runtime.update({tab}, std::chrono::milliseconds(16)),
                 MiniAppUpdateResult::DeactivatedMissingCapability);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.weather.updateCount);
    TEST_ASSERT_EQUAL_INT(1, f.weather.deactivateCount);

    f.registerCapability("WIFI");
    assertResult(f.runtime.update({tab}, std::chrono::milliseconds(16)), MiniAppUpdateResult::Idle);
    TEST_ASSERT_FALSE(f.runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.weather.updateCount);
    TEST_ASSERT_EQUAL_INT(1, f.weather.activateCount);
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_id_registration_is_rejected);
    RUN_TEST(test_unknown_descriptor_registration_is_rejected);
    RUN_TEST(test_successful_registration_is_exact_and_does_not_activate);
    RUN_TEST(test_registration_ids_are_case_sensitive);
    RUN_TEST(test_duplicate_instance_registration_preserves_the_original);
    RUN_TEST(test_eligibility_unknown_app_and_missing_instance);
    RUN_TEST(test_zero_capability_requirements_are_eligible);
    RUN_TEST(test_required_capabilities_must_all_be_available);
    RUN_TEST(test_unrelated_unavailable_capability_does_not_affect_eligibility);
    RUN_TEST(test_eligible_application_activates_once);
    RUN_TEST(test_reactivating_the_same_app_is_already_active);
    RUN_TEST(test_reactivating_the_active_app_is_already_active_before_capability_loss);
    RUN_TEST(test_failed_activation_does_not_mutate_active_state);
    RUN_TEST(test_switch_deactivates_old_app_before_activating_new_app);
    RUN_TEST(test_failed_switch_preserves_the_active_application);
    RUN_TEST(test_deactivation_clears_active_identity_exactly_once);
    RUN_TEST(test_idle_update_forwards_nothing);
    RUN_TEST(test_active_update_forwards_exact_input_and_elapsed);
    RUN_TEST(test_required_capability_loss_deactivates_before_update);
    return UNITY_END();
}
