#include <unity.h>

#include "../support/companion_session.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "apps/ai_agent_status/ai_agent_status_app.h"
#include "connectivity/companion/companion_protocol.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "services/ai_agent_status/ai_agent_status_indicator_controller.h"

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
        auto value = incoming.front();
        incoming.pop_front();
        return value;
    }
    CompanionEnvelope last() const { return test_support::companionDecode(sent.back()); }
    CompanionEnvelope lastStatusRequest() const {
        for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
            const auto message = test_support::companionDecode(*it);
            if (message.kind == CompanionKind::Request &&
                message.operation == CompanionOperation::AiAgentStatus)
                return message;
        }
        TEST_FAIL_MESSAGE("no AI_AGENT_STATUS request");
        return {};
    }
    int requests(CompanionOperation operation) const {
        int count = 0;
        for (const auto& payload : sent) {
            const auto message = test_support::companionDecode(payload);
            if (message.kind == CompanionKind::Request && message.operation == operation)
                ++count;
        }
        return count;
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
    void clear(core::RgbColor) override { ++draws; }
    void fillRectangle(core::PixelPosition, std::int32_t, std::int32_t,
                       core::RgbColor color) override {
        colors.push_back(color);
        ++draws;
    }
    void drawText(core::PixelPosition, const char* text, core::TextStyle) override {
        labels.emplace_back(text);
        ++draws;
    }
    bool drew(core::RgbColor color) const {
        return std::any_of(colors.begin(), colors.end(), [&](const core::RgbColor& c) {
            return c.red == color.red && c.green == color.green && c.blue == color.blue;
        });
    }
    void reset() {
        draws = 0;
        labels.clear();
        colors.clear();
    }
    int draws = 0;
    std::vector<std::string> labels;
    std::vector<core::RgbColor> colors;
};

// nullopt: hooks not installed for that application.
using Installed = std::optional<AgentState>;
constexpr std::nullopt_t none = std::nullopt;

CompanionAgentStatus statusOf(Installed codex, Installed claude, Installed cursor) {
    CompanionAgentStatus status{};
    if (codex)
        status.set(AiProvider::Codex, *codex);
    if (claude)
        status.set(AiProvider::Claude, *claude);
    if (cursor)
        status.set(AiProvider::Cursor, *cursor);
    return status;
}

struct Fixture {
    Transport transport;
    core::CapabilityRegistry capabilities;
    services::CompanionService companion{transport, capabilities};
    services::AiAgentStatusService status{companion};
    Leds leds;
    services::IndicatorService indicator{leds};
    services::AiAgentStatusIndicatorController puzzle{status, indicator};
    Display display;
    apps::AiAgentStatusApp app{status, puzzle, display};

    void ready() {
        transport.transportState = CompanionTransportState::Ready;
        test_support::completeCompanionHandshake(transport, companion);
        TEST_ASSERT_TRUE(companion.hasLiveCompanion());
    }

    // Answers the outstanding AI_AGENT_STATUS request.
    void answer(const CompanionAgentStatus& value) {
        const auto request = transport.lastStatusRequest();
        auto reply = makeResponse(companion.session(), request.requestId,
                                  CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setAgentStatus(reply, value));
        transport.incoming.push_back(test_support::companionWire(reply));
        tick({});
    }

    void tick(milliseconds elapsed) {
        companion.update(elapsed);
        status.update(elapsed);
        puzzle.update();
        indicator.update();
        app.update({}, elapsed);
    }
};

bool hasLabel(const Display& display, const char* text) {
    return std::find(display.labels.begin(), display.labels.end(), text) != display.labels.end();
}

int litRows(const services::IndicatorFrame& frame) {
    int rows = 0;
    for (int row = 0; row < 8; ++row) {
        const auto& pixel = frame.pixels[row * 8];
        if (pixel.red || pixel.green || pixel.blue)
            ++rows;
    }
    return rows;
}

// ---- Service -------------------------------------------------------------------

void test_service_polls_once_per_second_with_one_request_outstanding() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    TEST_ASSERT_EQUAL_INT(1, f.transport.requests(CompanionOperation::AiAgentStatus));
    f.tick(milliseconds(900));
    f.tick(milliseconds(900)); // unanswered, but within the request timeout
    TEST_ASSERT_EQUAL_INT(1, f.transport.requests(CompanionOperation::AiAgentStatus));
    f.answer(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Fresh),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    f.tick(milliseconds(1000));
    TEST_ASSERT_EQUAL_INT(2, f.transport.requests(CompanionOperation::AiAgentStatus));
}

void test_closed_app_polls_nothing() {
    Fixture f;
    f.ready();
    const auto before = f.transport.sent.size();
    f.tick(milliseconds(5000));
    TEST_ASSERT_EQUAL_INT(0, f.transport.requests(CompanionOperation::AiAgentStatus));
    TEST_ASSERT_TRUE(f.transport.sent.size() >= before);
}

void test_delivery_stale_after_three_seconds() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    f.answer(statusOf(AgentState::Working, AgentState::Done, none));
    f.tick(milliseconds(3000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Fresh),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    f.tick(milliseconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Stale),
                            static_cast<unsigned>(f.status.snapshot().freshness));
}

void test_bad_status_payload_isolated() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    const auto request = f.transport.lastStatusRequest();
    auto reply = makeResponse(f.companion.session(), request.requestId,
                              CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setAgentStatus(reply, statusOf(AgentState::Working, none, none)));
    auto bytes = test_support::companionWire(reply);
    bytes.bytes[companionEnvelopeSize + 1] = 9; // no such state
    f.transport.incoming.push_back(bytes);
    f.tick({});
    TEST_ASSERT_TRUE(f.companion.hasLiveCompanion());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    f.tick(milliseconds(1000));
    TEST_ASSERT_EQUAL_INT(2, f.transport.requests(CompanionOperation::AiAgentStatus));
}

void test_obsolete_status_completion_ignored() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    const auto request = f.transport.lastStatusRequest();
    f.status.stopMonitoring();
    auto reply = makeResponse(f.companion.session(), request.requestId,
                              CompanionOperation::AiAgentStatus, CompanionStatus::Ok);
    TEST_ASSERT_TRUE(setAgentStatus(reply, statusOf(AgentState::Working, none, none)));
    f.transport.incoming.push_back(test_support::companionWire(reply));
    f.tick({});
    f.status.startMonitoring();
    f.tick({});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.snapshot().freshness));
}

void test_host_switch_clears_and_reconnect_fetches_current_snapshot() {
    Fixture f;
    f.ready();
    f.status.startMonitoring();
    f.answer(statusOf(AgentState::Done, AgentState::Working, none));
    f.transport.transportState = CompanionTransportState::Unavailable;
    f.tick({});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(services::AgentStatusFreshness::Empty),
                            static_cast<unsigned>(f.status.snapshot().freshness));
    const auto before = f.transport.requests(CompanionOperation::AiAgentStatus);
    f.ready();
    f.tick({});
    TEST_ASSERT_EQUAL_INT(before + 1, f.transport.requests(CompanionOperation::AiAgentStatus));
    f.answer(statusOf(none, none, AgentState::NeedsYou));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(AgentState::NeedsYou),
        static_cast<unsigned>(f.status.snapshot().status.state(AiProvider::Cursor)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(AgentState::Unknown),
        static_cast<unsigned>(f.status.snapshot().status.state(AiProvider::Codex)));
}

// ---- LCD -----------------------------------------------------------------------

void test_checking_until_first_answer() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.tick({});
    TEST_ASSERT_TRUE(hasLabel(f.display, "CHECKING AI"));
    TEST_ASSERT_FALSE(hasLabel(f.display, "CODEX"));
}

void test_only_installed_applications_have_rows() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.tick({});
    f.display.reset();
    f.answer(statusOf(AgentState::Working, AgentState::Unknown, none));
    TEST_ASSERT_TRUE(hasLabel(f.display, "CODEX"));
    TEST_ASSERT_TRUE(hasLabel(f.display, "CLAUDE"));
    TEST_ASSERT_FALSE(hasLabel(f.display, "CURSOR"));
    TEST_ASSERT_TRUE(hasLabel(f.display, "WORKING"));
    TEST_ASSERT_TRUE(hasLabel(f.display, "--")); // Claude installed, nothing known
    f.tick(milliseconds(1000));
    f.display.reset();
    f.answer(statusOf(AgentState::Working, AgentState::NeedsYou, AgentState::Done));
    TEST_ASSERT_TRUE(hasLabel(f.display, "CURSOR")); // a newly installed app gets a row
    TEST_ASSERT_TRUE(hasLabel(f.display, "NEEDS YOU"));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::vermilion));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::leaf));
}

void test_no_hooks_installed_says_so() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(none, none, none));
    TEST_ASSERT_TRUE(hasLabel(f.display, "NO AI HOOKS"));
    TEST_ASSERT_TRUE(hasLabel(f.display, "INSTALL IN COMPANION"));
    TEST_ASSERT_FALSE(hasLabel(f.display, "CODEX"));
    f.tick(milliseconds(3001)); // stale: the message stays, nothing redraws
    f.display.reset();
    f.tick(milliseconds(20));
    TEST_ASSERT_EQUAL_INT(0, f.display.draws);
}

void test_stale_delivery_presents_rows_as_unknown() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, AgentState::Done, none));
    f.display.reset();
    f.tick(milliseconds(3001));
    TEST_ASSERT_EQUAL_INT(
        2, static_cast<int>(std::count(f.display.labels.begin(), f.display.labels.end(), "--")));
}

void test_unchanged_snapshot_does_not_redraw() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    f.tick(milliseconds(1000));
    f.display.reset();
    f.answer(statusOf(AgentState::Working, none, none));
    f.tick(milliseconds(20));
    TEST_ASSERT_EQUAL_INT(0, f.display.draws);
}

void test_state_change_redraws_one_plate_and_settles() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, AgentState::Working, none));
    f.tick(milliseconds(1000));
    f.display.reset();
    f.answer(statusOf(AgentState::Done, AgentState::Working, none));
    TEST_ASSERT_TRUE(hasLabel(f.display, "DONE"));
    TEST_ASSERT_TRUE(hasLabel(f.display, "CODEX"));
    TEST_ASSERT_FALSE(hasLabel(f.display, "CLAUDE")); // only the changed plate
    TEST_ASSERT_TRUE(f.display.drew(core::palette::leaf));
    f.display.reset();
    f.tick(milliseconds(600));
    TEST_ASSERT_EQUAL_INT(0, f.display.draws);
}

void test_done_earlier_is_a_settled_plate() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Done, none, none));
    f.tick(milliseconds(1000));
    f.display.reset();
    f.answer(statusOf(AgentState::DoneEarlier, none, none));
    TEST_ASSERT_TRUE(hasLabel(f.display, "DONE"));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::lightNeutral));
    TEST_ASSERT_FALSE(f.display.drew(core::palette::leaf));
    const auto frame = services::aiAgentStatusFrame(statusOf(AgentState::DoneEarlier, none, none));
    TEST_ASSERT_EQUAL_UINT8(services::agentDoneEarlierLed.green, frame.pixels[0].green);
}

void test_plates_fill_the_screen_without_header() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, AgentState::NeedsYou, AgentState::Unknown));
    TEST_ASSERT_FALSE(hasLabel(f.display, "AI STATUS"));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::blue));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::vermilion));
    TEST_ASSERT_TRUE(f.display.drew(core::palette::pale));
}

void test_repeated_close_is_safe() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    f.app.onDeactivate();
    f.app.onDeactivate();
    TEST_ASSERT_FALSE(f.status.monitoring());
    f.indicator.update();
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
}

// ---- Unit Puzzle ---------------------------------------------------------------

void test_puzzle_empty_with_no_installed_app() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(none, none, none));
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
}

void test_puzzle_shows_installed_app_without_state_dimly() {
    const auto frame =
        services::aiAgentStatusFrame(statusOf(AgentState::Unknown, AgentState::Working, none));
    TEST_ASSERT_EQUAL_INT(8, litRows(frame));
    TEST_ASSERT_EQUAL_UINT8(services::agentUnknownLed.red, frame.pixels[3 * 8 + 1].red);
    TEST_ASSERT_EQUAL_UINT8(services::agentWorkingLed.blue, frame.pixels[4 * 8 + 1].blue);
}

void test_puzzle_single_app_fills_matrix() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_TRUE(f.indicator.resolved().hasFrame);
    TEST_ASSERT_EQUAL_STRING(services::aiAgentStatusIndicatorOwner,
                             f.indicator.resolved().owner.c_str());
    const auto frame = services::aiAgentStatusFrame(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_EQUAL_INT(8, litRows(frame));
    TEST_ASSERT_EQUAL_UINT8(services::agentWorkingLed.blue, frame.pixels[63].blue);
}

// Columns 1-6: the band color, away from the end markers.
bool pixelIs(const services::IndicatorFrame& frame, int row, core::RgbColor color) {
    for (int column = 1; column < 7; ++column) {
        const auto& pixel = frame.pixels[row * 8 + column];
        if (pixel.red != color.red || pixel.green != color.green || pixel.blue != color.blue)
            return false;
    }
    return true;
}

void test_puzzle_two_apps_use_split_layout() {
    // Like the screen rows: equal bands with no gap.
    const auto frame =
        services::aiAgentStatusFrame(statusOf(AgentState::Working, AgentState::NeedsYou, none));
    TEST_ASSERT_EQUAL_INT(8, litRows(frame));
    TEST_ASSERT_TRUE(pixelIs(frame, 0, services::agentWorkingLed));
    TEST_ASSERT_TRUE(pixelIs(frame, 3, services::agentWorkingLed));
    TEST_ASSERT_TRUE(pixelIs(frame, 4, services::agentNeedsYouLed));
    TEST_ASSERT_TRUE(pixelIs(frame, 7, services::agentNeedsYouLed));
    // Codex and Cursor: Claude is not installed, so Cursor takes the lower half.
    const auto work =
        services::aiAgentStatusFrame(statusOf(AgentState::Done, none, AgentState::Working));
    TEST_ASSERT_TRUE(pixelIs(work, 3, services::agentDoneLed));
    TEST_ASSERT_TRUE(pixelIs(work, 4, services::agentWorkingLed));
    const auto three = services::aiAgentStatusFrame(
        statusOf(AgentState::Done, AgentState::Working, AgentState::NeedsYou));
    TEST_ASSERT_EQUAL_INT(8, litRows(three));
    TEST_ASSERT_TRUE(pixelIs(three, 2, services::agentDoneLed));
    TEST_ASSERT_TRUE(pixelIs(three, 3, services::agentWorkingLed));
    TEST_ASSERT_TRUE(pixelIs(three, 5, services::agentWorkingLed));
    TEST_ASSERT_TRUE(pixelIs(three, 6, services::agentNeedsYouLed));
}

bool same(const core::RgbColor& left, const core::RgbColor& right) {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

void test_puzzle_bands_have_end_markers_only_when_split() {
    const auto single = services::aiAgentStatusFrame(statusOf(AgentState::Working, none, none));
    for (const auto& pixel : single.pixels)
        TEST_ASSERT_TRUE(same(pixel, services::agentWorkingLed));
    const auto two =
        services::aiAgentStatusFrame(statusOf(AgentState::Working, AgentState::Working, none));
    TEST_ASSERT_TRUE(same(two.pixels[0], services::agentBandMarkerLed));         // Codex start
    TEST_ASSERT_TRUE(same(two.pixels[3 * 8 + 7], services::agentBandMarkerLed)); // Codex end
    TEST_ASSERT_TRUE(same(two.pixels[4 * 8], services::agentBandMarkerLed));     // Claude start
    TEST_ASSERT_TRUE(same(two.pixels[63], services::agentBandMarkerLed));        // Claude end
    TEST_ASSERT_TRUE(same(two.pixels[1], services::agentWorkingLed));
    TEST_ASSERT_TRUE(same(two.pixels[3 * 8 + 6], services::agentWorkingLed));
    const auto three = services::aiAgentStatusFrame(
        statusOf(AgentState::Done, AgentState::Working, AgentState::NeedsYou));
    for (const int index : {0, 2 * 8 + 7, 3 * 8, 5 * 8 + 7, 6 * 8, 63})
        TEST_ASSERT_TRUE(same(three.pixels[index], services::agentBandMarkerLed));
}

void test_puzzle_layout_follows_running_count() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_TRUE(pixelIs(f.indicator.resolved().frame, 7, services::agentWorkingLed));
    f.tick(milliseconds(1000));
    f.answer(statusOf(AgentState::Working, AgentState::Done, none));
    TEST_ASSERT_TRUE(pixelIs(f.indicator.resolved().frame, 3, services::agentWorkingLed));
    TEST_ASSERT_TRUE(pixelIs(f.indicator.resolved().frame, 4, services::agentDoneLed));
}

void test_closing_ai_status_releases_puzzle() {
    Fixture f;
    f.ready();
    services::IndicatorFrame pomodoroFrame{};
    pomodoroFrame.pixels[0] = {1, 2, 3};
    auto pomodoro =
        f.indicator.acquire("pomodoro", services::IndicatorPriority::BackgroundApplication);
    pomodoro.setFrame(pomodoroFrame);
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_EQUAL_STRING("ai-status", f.indicator.resolved().owner.c_str());
    f.app.onDeactivate();
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING("pomodoro", f.indicator.resolved().owner.c_str());
}

void test_puzzle_cleared_when_stale() {
    Fixture f;
    f.ready();
    f.app.onActivate();
    f.answer(statusOf(AgentState::Working, none, none));
    TEST_ASSERT_TRUE(f.indicator.resolved().hasFrame);
    f.tick(milliseconds(3001));
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_service_polls_once_per_second_with_one_request_outstanding);
    RUN_TEST(test_closed_app_polls_nothing);
    RUN_TEST(test_delivery_stale_after_three_seconds);
    RUN_TEST(test_bad_status_payload_isolated);
    RUN_TEST(test_obsolete_status_completion_ignored);
    RUN_TEST(test_host_switch_clears_and_reconnect_fetches_current_snapshot);
    RUN_TEST(test_checking_until_first_answer);
    RUN_TEST(test_only_installed_applications_have_rows);
    RUN_TEST(test_no_hooks_installed_says_so);
    RUN_TEST(test_stale_delivery_presents_rows_as_unknown);
    RUN_TEST(test_unchanged_snapshot_does_not_redraw);
    RUN_TEST(test_state_change_redraws_one_plate_and_settles);
    RUN_TEST(test_done_earlier_is_a_settled_plate);
    RUN_TEST(test_plates_fill_the_screen_without_header);
    RUN_TEST(test_repeated_close_is_safe);
    RUN_TEST(test_puzzle_empty_with_no_installed_app);
    RUN_TEST(test_puzzle_shows_installed_app_without_state_dimly);
    RUN_TEST(test_puzzle_single_app_fills_matrix);
    RUN_TEST(test_puzzle_two_apps_use_split_layout);
    RUN_TEST(test_puzzle_bands_have_end_markers_only_when_split);
    RUN_TEST(test_puzzle_layout_follows_running_count);
    RUN_TEST(test_closing_ai_status_releases_puzzle);
    RUN_TEST(test_puzzle_cleared_when_stale);
    return UNITY_END();
}
