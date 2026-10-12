#include "../support/ui_capture.h"
#include "../support/web_access_fakes.h"
#include "apps/led_canvas/led_canvas_app.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"
#include <algorithm>
#include <cmath>
#include <unity.h>
using namespace cardputer_hub;
using namespace std::chrono_literals;
namespace {
class Display final : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor c) override {
        texts.clear();
        capture.clear(c);
        ++clears;
    }
    void fillRectangle(core::PixelPosition p, std::int32_t w, std::int32_t h,
                       core::RgbColor c) override {
        capture.rectangle(p, w, h, c);
        TEST_ASSERT_TRUE(p.x >= 0 && p.y >= 0 && p.x + w <= 240 && p.y + h <= 135);
    }
    void drawText(core::PixelPosition p, const char* t, core::TextStyle s) override {
        capture.text(p, t, s);
        texts.emplace_back(t);
        TEST_ASSERT_TRUE(p.x >= 0 && p.y >= 0 && p.x + core::textWidth(t, s.scale) <= 240 &&
                         p.y + std::ceil(8 * s.scale) <= 135);
    }
    bool shows(const char* t) const {
        return std::find(texts.begin(), texts.end(), t) != texts.end();
    }
    test_support::UiCapture capture;
    std::vector<std::string> texts;
    unsigned clears = 0;
};
class Led final : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame&) override {}
};
struct Fixture {
    test_support::FakeAccessPoint ap;
    test_support::FakeHttpServer server;
    test_support::MapStorage adapter;
    core::Storage storage{adapter};
    test_support::CountingRandom random;
    services::WebAccessService web{ap, server, storage, random};
    services::LedCanvasService canvas;
    core::ActionBus actions;
    Led led;
    services::IndicatorService indicator{led};
    Display display;
    apps::LedCanvasApp app{canvas, web, actions, indicator, display};
    Fixture() {
        actions.registerHandler(services::webAccessSetEnabledActionId, web);
        actions.registerHandler(services::ledCanvasHoldActionId, canvas);
        actions.registerHandler(services::ledCanvasClearActionId, canvas);
        app.onActivate();
    }
    void frame(const core::InputEvents& keys = {}, std::chrono::milliseconds dt = 16ms) {
        web.update(dt);
        app.update(keys, dt);
        indicator.update();
    }
    void stroke() {
        services::LedCanvasBatch b;
        b.client = 1;
        b.seq = 1;
        b.count = 1;
        b.points[0] = {0, 896, 896, 12, 0, 1};
        canvas.submit(b);
    }
};
const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent space{core::InputEventType::PrintableCharacter, ' ', {}, {}};
const core::InputEvent del{core::InputEventType::NamedKey, 0, core::NamedKey::Backspace, {}};
const core::InputEvent q{core::InputEventType::PrintableCharacter, 'q', {}, {}};
void test_enter_starts_web_access_once() {
    Fixture f;
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("WEB ACCESS OFF"));
    f.display.capture.save("led-canvas-off");
    f.frame({enter, enter});
    TEST_ASSERT_EQUAL(1, f.ap.starts);
    TEST_ASSERT_TRUE(f.display.shows("WAITING FOR A TOUCH"));
    f.display.capture.save("led-canvas-waiting");
}
void test_web_stop_ends_pointers_and_shows_off() {
    Fixture f;
    f.frame({enter});
    f.stroke();
    f.frame({}, 100ms);
    TEST_ASSERT_TRUE(f.display.shows("LIVE  1 FINGER"));
    f.display.capture.save("led-canvas-live");
    f.actions.dispatch({services::webAccessSetEnabledActionId, "test", {{"enabled", false}}});
    f.frame();
    TEST_ASSERT_EQUAL(0, f.canvas.status().fingers);
    TEST_ASSERT_EQUAL(0, f.canvas.status().queued);
    TEST_ASSERT_TRUE(f.display.shows("WEB OFF"));
    TEST_ASSERT_EQUAL_STRING(apps::ledCanvasAppId, f.indicator.resolved().owner.c_str());
}
void test_escape_releases_claim_and_clears_queue() {
    Fixture f;
    auto lower =
        f.indicator.acquire("pomodoro", services::IndicatorPriority::BackgroundApplication);
    lower.setFrame({});
    f.frame({enter});
    f.stroke();
    TEST_ASSERT_FALSE(f.app.handleBack());
    f.app.onDeactivate();
    f.indicator.update();
    TEST_ASSERT_FALSE(f.canvas.status().open);
    TEST_ASSERT_EQUAL(0, f.canvas.status().queued);
    TEST_ASSERT_EQUAL_STRING("pomodoro", f.indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL(services::WebAccessState::Running, f.web.status().state);
}
void test_space_and_del_dispatch_actions() {
    Fixture f;
    f.frame({enter});
    f.stroke();
    f.frame({}, 100ms);
    f.frame({space});
    TEST_ASSERT_TRUE(f.canvas.status().settings.hold);
    TEST_ASSERT_TRUE(f.display.shows("SPACE RESUME"));
    f.frame({del});
    TEST_ASSERT_EQUAL(1, f.canvas.status().clearGeneration);
    for (const auto& p : f.indicator.resolved().frame.pixels)
        TEST_ASSERT_EQUAL(0, p.red);
}
void test_q_shows_shared_join_code_and_escape_returns() {
    Fixture f;
    f.frame({enter});
    f.frame({q});
    TEST_ASSERT_TRUE(f.display.shows("SCAN"));
    f.frame({enter, space, del});
    TEST_ASSERT_FALSE(f.canvas.status().settings.hold);
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("LED CANVAS"));
    TEST_ASSERT_FALSE(f.app.handleBack());
}
void test_idle_screen_is_not_redrawn() {
    Fixture f;
    f.frame({enter});
    auto n = f.display.clears;
    f.frame();
    f.frame();
    TEST_ASSERT_EQUAL(n, f.display.clears);
}
void test_final_dark_frame_is_published_after_the_interval() {
    Fixture f;
    f.frame({enter});
    auto settings = f.canvas.status().settings;
    settings.fade = services::LedCanvasFade::Short;
    f.canvas.setSettings(settings);
    f.stroke();
    f.frame({}, 100ms);
    f.canvas.endPointers();
    // The last channel crosses the dark threshold between 33 ms output ticks.
    for (int i = 0; i < 2000; ++i)
        f.frame({}, 1ms);
    for (const auto& p : f.indicator.resolved().frame.pixels) {
        TEST_ASSERT_EQUAL(0, p.red);
        TEST_ASSERT_EQUAL(0, p.green);
        TEST_ASSERT_EQUAL(0, p.blue);
    }
}
void test_stop_ends_a_pointer_even_with_an_end_point_pending() {
    Fixture f;
    f.frame({enter});
    f.stroke();
    f.frame({}, 100ms);
    services::LedCanvasBatch end;
    end.client = 1;
    end.seq = 2;
    end.count = 1;
    end.points[0] = {0, 896, 896, 12, 0, 2};
    f.canvas.submit(end);
    f.actions.dispatch({services::webAccessSetEnabledActionId, "test", {{"enabled", false}}});
    f.frame({}, 10000ms);
    for (const auto& p : f.indicator.resolved().frame.pixels)
        TEST_ASSERT_EQUAL(0, p.red);
}
void test_error_offers_retry() {
    Fixture f;
    f.ap.failStart = true;
    f.frame({enter});
    TEST_ASSERT_TRUE(f.display.shows("WEB ACCESS ERROR"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER RETRY"));
    f.ap.failStart = false;
    f.frame({enter});
    TEST_ASSERT_TRUE(f.display.shows("WAITING FOR A TOUCH"));
}
} // namespace
void setUp() {}
void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_starts_web_access_once);
    RUN_TEST(test_web_stop_ends_pointers_and_shows_off);
    RUN_TEST(test_escape_releases_claim_and_clears_queue);
    RUN_TEST(test_space_and_del_dispatch_actions);
    RUN_TEST(test_q_shows_shared_join_code_and_escape_returns);
    RUN_TEST(test_idle_screen_is_not_redrawn);
    RUN_TEST(test_error_offers_retry);
    RUN_TEST(test_final_dark_frame_is_published_after_the_interval);
    RUN_TEST(test_stop_ends_a_pointer_even_with_an_end_point_pending);
    return UNITY_END();
}
