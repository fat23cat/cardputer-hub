#include <unity.h>

#include "apps/common/shell_actions.h"
#include "apps/hosts/host_settings.h"
#include "apps/shell/application_shell.h"
#include "core/actions/action_bus.h"
#include "services/ai_agent_status/ai_agent_status_service.h"
#include "services/ai_usage/ai_usage_service.h"
#include "services/audio/audio_service.h"
#include "services/device_settings/device_settings_service.h"
#include "services/host_control/host_control_service.h"
#include "services/hosts/host_service.h"
#include "services/led_canvas/led_canvas_service.h"
#include "services/network/network_service.h"
#include "services/service_status/service_status_service.h"
#include "services/web_access/web_access_service.h"

namespace {

using cardputer_hub::core::Action;
using cardputer_hub::core::ActionBus;
using cardputer_hub::core::ActionHandlingResult;
using cardputer_hub::core::IActionHandler;
namespace apps = cardputer_hub::apps;
namespace services = cardputer_hub::services;

class NullHandler final : public IActionHandler {
  public:
    ActionHandlingResult handle(const Action&) override { return ActionHandlingResult::Handled; }
};

// Mirrors the production registrations in main.cpp, ApplicationShell and
// HostSettings: every owner's IDs must be valid and distinct.
void test_production_action_ids_register_without_refusal() {
    ActionBus bus;
    NullHandler handler;

    (void)bus.registerHandlers(apps::shellNavigationActionIds, handler);
    (void)bus.registerHandler(apps::ApplicationShell::statusToggleActionId, handler);
    (void)bus.registerHandlers(apps::hostSettingsActionIds, handler);
    (void)bus.registerHandlers(services::hostServiceActionIds, handler);
    (void)bus.registerHandler(services::audioVolumeStepActionId, handler);
    (void)bus.registerHandlers(services::deviceSettingsActionIds, handler);
    (void)bus.registerHandlers(services::networkServiceActionIds, handler);
    (void)bus.registerHandler(services::hostAppActivateActionId, handler);
    (void)bus.registerHandler(services::serviceStatusRefreshActionId, handler);
    (void)bus.registerHandler(services::aiUsageRefreshActionId, handler);
    (void)bus.registerHandler(services::aiAgentStatusRefreshActionId, handler);
    (void)bus.registerHandler(services::webAccessSetEnabledActionId, handler);
    (void)bus.registerHandler(services::ledCanvasHoldActionId, handler);
    (void)bus.registerHandler(services::ledCanvasClearActionId, handler);

    for (const auto& rejected : bus.rejectedRegistrations())
        TEST_FAIL_MESSAGE(rejected.actionId.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, bus.rejectedRegistrations().size());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_production_action_ids_register_without_refusal);
    return UNITY_END();
}
