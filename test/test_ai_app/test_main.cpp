#include <unity.h>

#include "../support/companion_session.h"
#include "apps/ai/ai_app.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace {
using namespace cardputer_hub;
using namespace connectivity;
using std::chrono::milliseconds;

class Transport : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return CompanionSendResult::Sent;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        const auto value = incoming.front();
        incoming.pop_front();
        return value;
    }
    CompanionEnvelope request(CompanionOperation operation) const {
        for (auto i = sent.rbegin(); i != sent.rend(); ++i) {
            const auto message = test_support::companionDecode(*i);
            if (message.kind == CompanionKind::Request && message.operation == operation)
                return message;
        }
        TEST_FAIL_MESSAGE("No request for the expected Companion operation");
        return {};
    }
    CompanionTransportState transportState = CompanionTransportState::Ready;
    std::deque<CompanionPayload> incoming;
    std::vector<CompanionPayload> sent;
};

class Leds : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame&) override {}
};

class Display : public core::IDisplayAdapter {
  public:
    void beginFrame() override { frameActive = true; }
    void endFrame() override { frameActive = false; }
    void beginTransition(core::SlideDirection direction) override {
        if (frameActive) {
            ++acceptedTransitions;
            transitions.push_back(direction);
        }
    }
    void clear(core::RgbColor) override { ++draws; }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor) override {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, position.x);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, position.y);
        TEST_ASSERT_LESS_OR_EQUAL_INT(240, position.x + width);
        TEST_ASSERT_LESS_OR_EQUAL_INT(135, position.y + height);
        ++draws;
    }
    void drawText(core::PixelPosition position, const char* text, core::TextStyle style) override {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, position.x);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, position.y);
        TEST_ASSERT_LESS_OR_EQUAL_INT(240, position.x + core::textWidth(text, style.scale));
        TEST_ASSERT_LESS_OR_EQUAL_INT(
            135,
            position.y + static_cast<int>(std::ceil(core::systemGlyphNativeHeight * style.scale)));
        labels.emplace_back(text);
        ++draws;
    }
    bool has(const char* text) const {
        return std::find(labels.begin(), labels.end(), text) != labels.end();
    }
    void reset() {
        labels.clear();
        draws = 0;
    }
    std::vector<std::string> labels;
    int draws = 0;
    bool frameActive = false;
    int acceptedTransitions = 0;
    std::vector<core::SlideDirection> transitions;
};

core::InputEvent key(core::NamedKey value) {
    return {core::InputEventType::NamedKey, 0, value, {}};
}
core::InputEvent character(char value) {
    return {core::InputEventType::PrintableCharacter, value, core::NamedKey::Tab, {}};
}

CompanionAiUsage workUsage() {
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 2;
    auto& codex = value.providers[0];
    codex.provider = AiProvider::Codex;
    codex.plan = AiPlan::Business;
    codex.metricCount = 1;
    codex.metrics[0] = {
        AiMetricKind::Credits, AiMetricUnit::Credits, 19765, 20000, 235, 1, 10, 2 * 86400};
    auto& cursor = value.providers[1];
    cursor.provider = AiProvider::Cursor;
    cursor.plan = AiPlan::Enterprise;
    cursor.metricCount = 1;
    cursor.metrics[0] = {
        AiMetricKind::Money, AiMetricUnit::Cents, 9458, 255000, 245542, 96, 20, 3 * 86400};
    return value;
}

CompanionAiUsage plusUsage() {
    CompanionAiUsage value{};
    value.state = AiUsageState::Ready;
    value.providerCount = 2;
    for (int i = 0; i < 2; ++i) {
        auto& provider = value.providers[i];
        provider.provider = i == 0 ? AiProvider::Codex : AiProvider::Claude;
        provider.plan = i == 0 ? AiPlan::Plus : AiPlan::Max;
        provider.metricCount = 2;
        provider.metrics[0] = {
            AiMetricKind::FiveHour, AiMetricUnit::Percent, 36, 100, 64, 64, 100, 7200};
        provider.metrics[1] = {
            AiMetricKind::Week, AiMetricUnit::Percent, 19, 100, 81, 81, 200, 86400};
    }
    auto& resets = value.providers[0].resetCredits;
    resets.known = true;
    resets.availableCount = 3;
    resets.creditCount = 3;
    for (int i = 0; i < 3; ++i) {
        std::snprintf(resets.credits[i].title.data(), resets.credits[i].title.size(), "BONUS %d",
                      i + 1);
        resets.credits[i].expiresAt = 300;
        resets.credits[i].expiresRemainingSeconds = 86400;
    }
    return value;
}

struct Fixture {
    Transport transport;
    core::CapabilityRegistry capabilities;
    services::CompanionService companion{transport, capabilities};
    services::AiUsageService usage{companion};
    services::AiAgentStatusService status{companion};
    Leds leds;
    services::IndicatorService indicator{leds};
    services::AiUsageIndicatorController quota{usage, indicator};
    services::AiAgentStatusIndicatorController agents{status, indicator};
    Display display;
    apps::AiApp app{usage, quota, status, agents, display};

    void tick(milliseconds elapsed = {}) {
        companion.update(elapsed);
        usage.update(elapsed);
        status.update(elapsed);
        quota.update(elapsed);
        agents.update();
        indicator.update();
        display.beginFrame();
        app.update({}, elapsed);
        display.endFrame();
    }
    void open() {
        test_support::completeCompanionHandshake(transport, companion);
        usage.update({});
        app.onActivate();
        tick();
    }
    void answerUsage(const CompanionAiUsage& value) {
        const auto request = transport.request(CompanionOperation::AiUsage);
        auto reply = makeResponse(companion.session(), request.requestId,
                                  CompanionOperation::AiUsage, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setAiUsage(reply, value));
        transport.incoming.push_back(test_support::companionWire(reply));
        tick();
    }
    void answerStatus(AgentState codex, AgentState cursor = AgentState::Working) {
        CompanionAgentStatus value{};
        value.set(AiProvider::Codex, codex);
        value.set(AiProvider::Cursor, cursor);
        answerStatus(value);
    }
    void answerStatus(const CompanionAgentStatus& value) {
        const auto request = transport.request(CompanionOperation::AiAgentStatus);
        auto reply = makeResponse(companion.session(), request.requestId,
                                  CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setAgentStatus(reply, value));
        transport.incoming.push_back(test_support::companionWire(reply));
        tick();
    }
    void press(const core::InputEvent& event) {
        display.reset();
        display.beginFrame();
        app.update({event}, {});
        display.endFrame();
        quota.update({});
        agents.update();
        indicator.update();
    }
};

void test_activation_leaves_the_shell_frame_open_for_its_transition() {
    Fixture f;
    // The shell activates the app, requests the opening slide, then supplies
    // the scheduled update. Check both first entry and reopening on USAGE.
    for (int entry = 0; entry < 2; ++entry) {
        f.display.reset();
        f.display.beginFrame();
        f.app.onActivate();
        TEST_ASSERT_TRUE(f.display.frameActive);
        TEST_ASSERT_EQUAL_INT(0, f.display.draws);
        const auto transitions = f.display.acceptedTransitions;
        f.display.beginTransition(core::SlideDirection::Forward);
        TEST_ASSERT_EQUAL_INT(transitions + 1, f.display.acceptedTransitions);
        f.app.update({}, {});
        TEST_ASSERT_TRUE(f.display.frameActive);
        f.display.endFrame();
        TEST_ASSERT_TRUE(f.display.has("AI"));
        TEST_ASSERT_TRUE(f.display.has("CHECKING AI"));
        if (entry == 0)
            f.press(key(core::NamedKey::Right));
        f.app.onDeactivate();
    }
}

void test_page_switch_keeps_outer_frame_open_for_escape_in_the_same_tick() {
    Fixture f;
    f.open();
    f.display.beginFrame();
    f.app.update({key(core::NamedKey::Right)}, {});
    TEST_ASSERT_TRUE(f.display.frameActive);
    TEST_ASSERT_FALSE(f.app.handleBack());
    f.display.beginTransition(core::SlideDirection::Backward);
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL_INT(2, f.display.acceptedTransitions);
    f.display.endFrame();
}

void test_page_slides_follow_key_direction_in_both_cyclic_directions() {
    Fixture f;
    f.open();
    for (const auto direction : {core::NamedKey::Right, core::NamedKey::Right, core::NamedKey::Left,
                                 core::NamedKey::Left}) {
        const auto count = f.display.transitions.size();
        f.press(key(direction));
        TEST_ASSERT_EQUAL_UINT(count + 1, f.display.transitions.size());
        TEST_ASSERT_EQUAL_INT(direction == core::NamedKey::Left ? core::SlideDirection::Backward
                                                                : core::SlideDirection::Forward,
                              f.display.transitions.back());
    }
}

void test_stale_no_hooks_answer_survives_leaving_and_returning_to_status() {
    Fixture f;
    f.open();
    f.answerStatus(CompanionAgentStatus{});
    TEST_ASSERT_TRUE(f.display.has("NO AI HOOKS"));
    f.press(key(core::NamedKey::Right));
    f.tick(std::chrono::seconds(4));
    TEST_ASSERT_EQUAL_INT(services::AgentStatusFreshness::Stale, f.status.snapshot().freshness);
    f.press(key(core::NamedKey::Left));
    TEST_ASSERT_TRUE(f.display.has("NO AI HOOKS"));
    TEST_ASSERT_FALSE(f.display.has("CHECKING AI"));
}

void test_usage_refresh_preserves_header_when_attention_is_unchanged() {
    Fixture f;
    f.open();
    f.answerUsage(workUsage());
    f.press(key(core::NamedKey::Right));
    f.usage.update(services::AiUsageService::pollInterval);
    f.display.reset();
    auto updated = workUsage();
    updated.providers[0].metrics[0].used = 19000;
    updated.providers[0].metrics[0].remaining = 1000;
    updated.providers[0].metrics[0].remainingPercent = 5;
    f.answerUsage(updated);
    TEST_ASSERT_TRUE(f.display.has("19,000 / 20,000 CR"));
    TEST_ASSERT_FALSE(f.display.has("AI"));
}

void test_switch_pages_preserves_work_quota_fields_and_puzzle_ownership() {
    Fixture f;
    f.open();
    f.answerUsage(workUsage());
    f.answerStatus(AgentState::Working, AgentState::NeedsYou);
    TEST_ASSERT_TRUE(f.display.has("WORKING"));
    TEST_ASSERT_TRUE(f.display.has("NEEDS YOU"));
    TEST_ASSERT_EQUAL_STRING("ai-status", f.indicator.resolved().owner.c_str());
    f.press(key(core::NamedKey::Right));
    for (const auto* label :
         {"AI", "STATUS", "USAGE", "BUSINESS", "ENTERPRISE", "19,765 / 20,000 CR",
          "$94.58 / $2,550.00", "1% LEFT !", "96% LEFT", "RESET 2D 0H", "RESET 3D 0H", "!"})
        TEST_ASSERT_TRUE_MESSAGE(f.display.has(label), label);
    TEST_ASSERT_TRUE(f.status.monitoring());
    TEST_ASSERT_EQUAL_STRING("ai-usage", f.indicator.resolved().owner.c_str());
    f.press(character(','));
    TEST_ASSERT_TRUE(f.display.has("NEEDS YOU"));
    TEST_ASSERT_EQUAL_STRING("ai-status", f.indicator.resolved().owner.c_str());
    f.press(character('/'));
    TEST_ASSERT_TRUE(f.display.has("19,765 / 20,000 CR"));
    f.app.onDeactivate();
    f.quota.update({});
    f.agents.update();
    f.indicator.update();
    TEST_ASSERT_FALSE(f.status.monitoring());
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
    f.display.reset();
    f.app.onActivate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.has("19,765 / 20,000 CR"));
}

void test_detail_arrows_are_local_and_escape_returns_to_usage() {
    Fixture f;
    f.open();
    f.answerUsage(plusUsage());
    f.press(key(core::NamedKey::Right));
    for (const auto* label : {"PLUS", "MAX", "5H", "WK", "LEFT", "RESET", "R", "3"})
        TEST_ASSERT_TRUE_MESSAGE(f.display.has(label), label);
    f.press(key(core::NamedKey::Enter));
    TEST_ASSERT_TRUE(f.display.has("USED"));
    TEST_ASSERT_FALSE(f.display.has("AI"));
    f.press(character('/'));
    TEST_ASSERT_TRUE(f.display.has("RESET CREDITS"));
    f.press(character('.'));
    TEST_ASSERT_TRUE(f.display.has("#3  BONUS 3"));
    f.display.reset();
    TEST_ASSERT_TRUE(f.app.handleBack());
    TEST_ASSERT_TRUE(f.display.has("USAGE"));
    TEST_ASSERT_TRUE(f.display.has("MAX"));
    TEST_ASSERT_FALSE(f.app.handleBack());
    f.press(key(core::NamedKey::Left));
    TEST_ASSERT_FALSE(f.app.handleBack());
}

void test_hidden_status_updates_attention_without_redrawing_quota() {
    Fixture f;
    f.open();
    f.answerUsage(workUsage());
    f.answerStatus(AgentState::Working);
    f.press(key(core::NamedKey::Right));
    f.tick(std::chrono::seconds(1));
    f.display.reset();
    f.answerStatus(AgentState::NeedsYou);
    TEST_ASSERT_TRUE(f.display.has("!"));
    TEST_ASSERT_FALSE(f.display.has("19,765 / 20,000 CR"));
    f.display.reset();
    f.tick(milliseconds(100));
    TEST_ASSERT_EQUAL_INT(0, f.display.draws);
    f.tick(std::chrono::seconds(4));
    TEST_ASSERT_FALSE(f.display.has("!"));
    f.press(key(core::NamedKey::Left));
    TEST_ASSERT_TRUE(f.display.has("--"));
}

void test_modified_horizontal_keys_do_not_switch_pages() {
    Fixture f;
    f.open();
    f.answerUsage(workUsage());
    for (int modifier = 0; modifier < 4; ++modifier) {
        auto event = character('/');
        event.modifiers.ctrl = modifier == 0;
        event.modifiers.alt = modifier == 1;
        event.modifiers.option = modifier == 2;
        event.modifiers.shift = modifier == 3;
        f.press(event);
        TEST_ASSERT_FALSE(f.display.has("BUSINESS"));
    }
    auto event = key(core::NamedKey::Right);
    event.modifiers.fn = true;
    f.press(event);
    TEST_ASSERT_TRUE(f.display.has("BUSINESS"));
}
void test_new_companion_never_reuses_previous_host_quota_or_attention() {
    Fixture f;
    f.open();
    f.answerUsage(workUsage());
    f.answerStatus(AgentState::NeedsYou);
    f.press(key(core::NamedKey::Right));
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.display.reset();
    f.tick();
    TEST_ASSERT_TRUE(f.display.has("CHECKING AI"));
    TEST_ASSERT_FALSE(f.display.has("19,765 / 20,000 CR"));
    TEST_ASSERT_FALSE(f.display.has("!"));
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
    f.app.onDeactivate();
    f.display.reset();
    f.transport.transportState = CompanionTransportState::Ready;
    f.open();
    TEST_ASSERT_TRUE(f.display.has("USAGE"));
    TEST_ASSERT_TRUE(f.display.has("CHECKING AI"));
    TEST_ASSERT_FALSE(f.display.has("19,765 / 20,000 CR"));
    auto replacement = workUsage();
    replacement.providers[0].metrics[0].used = 10000;
    replacement.providers[0].metrics[0].remaining = 10000;
    replacement.providers[0].metrics[0].remainingPercent = 50;
    f.answerUsage(replacement);
    TEST_ASSERT_TRUE(f.display.has("10,000 / 20,000 CR"));
    TEST_ASSERT_FALSE(f.display.has("!"));
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_activation_leaves_the_shell_frame_open_for_its_transition);
    RUN_TEST(test_page_switch_keeps_outer_frame_open_for_escape_in_the_same_tick);
    RUN_TEST(test_page_slides_follow_key_direction_in_both_cyclic_directions);
    RUN_TEST(test_stale_no_hooks_answer_survives_leaving_and_returning_to_status);
    RUN_TEST(test_usage_refresh_preserves_header_when_attention_is_unchanged);
    RUN_TEST(test_switch_pages_preserves_work_quota_fields_and_puzzle_ownership);
    RUN_TEST(test_detail_arrows_are_local_and_escape_returns_to_usage);
    RUN_TEST(test_hidden_status_updates_attention_without_redrawing_quota);
    RUN_TEST(test_modified_horizontal_keys_do_not_switch_pages);
    RUN_TEST(test_new_companion_never_reuses_previous_host_quota_or_attention);
    return UNITY_END();
}
