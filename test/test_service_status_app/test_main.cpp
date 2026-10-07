#include <unity.h>

#include "../support/service_status_fixture.h"
#include "../support/ui_capture.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "apps/service_status/service_status_app.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

namespace {
using namespace cardputer_hub;
using namespace core;
using std::chrono::milliseconds;
using std::chrono::minutes;

class Display final : public IDisplayAdapter {
  public:
    void clear(RgbColor color) override {
        capture.clear(color);
        texts.clear();
        rectangles.clear();
        ++draws;
    }
    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        capture.rectangle(position, width, height, color);
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height, color});
        ++draws;
    }
    void drawText(PixelPosition position, const char* value, TextStyle style) override {
        capture.text(position, value, style);
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0);
        TEST_ASSERT_TRUE(position.x + core::textWidth(value, style.scale) <= 240);
        TEST_ASSERT_TRUE(position.y + std::ceil(8 * style.scale) <= 135);
        texts.emplace_back(value);
        ++draws;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    bool filled(RgbColor color, std::int32_t width) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& item) {
            return item.width == width && item.color.red == color.red &&
                   item.color.green == color.green && item.color.blue == color.blue;
        });
    }
    struct Rectangle {
        PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        RgbColor color;
    };
    test_support::UiCapture capture;
    std::vector<std::string> texts;
    std::vector<Rectangle> rectangles;
    int draws = 0;
};

const InputEvent down{InputEventType::PrintableCharacter, '.', {}, {}};
const InputEvent up{InputEventType::NamedKey, 0, NamedKey::Up, {}};
const InputEvent refresh{InputEventType::PrintableCharacter, 'r', {}, {}};

struct AppFixture {
    test_support::ServiceStatusFixture status;
    ActionBus actions;
    Display display;
    apps::ServiceStatusApp app{status.service, actions, display};

    AppFixture() {
        TEST_ASSERT_TRUE(actions.registerHandler(services::serviceStatusRefreshActionId,
                                                 status.service) == RegistrationResult::Registered);
    }
    void frame(const InputEvents& input = {}) { app.update(input, milliseconds(16)); }
    void answer(const char* indicator, const char* description = "") {
        status.http.answer(200, test_support::statusBody(indicator, description));
        status.tick();
    }
};

void test_opening_starts_checks_and_shows_four_pending_rows() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.frame();
    TEST_ASSERT_TRUE(f.status.service.active());
    TEST_ASSERT_TRUE(f.display.shows("SERVICES HEALTH"));
    TEST_ASSERT_TRUE(f.display.shows("CHECKING"));
    for (const auto& source : services::statusSources)
        TEST_ASSERT_TRUE(f.display.shows(source.name));
    TEST_ASSERT_EQUAL_INT(
        4, static_cast<int>(std::count(f.display.texts.begin(), f.display.texts.end(), "--")));
    f.display.capture.save("service-status-checking");
}

void test_levels_show_labels_and_marks_without_the_page_summary() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.answer("none", "All Systems Operational");
    f.answer("minor", "Partially Degraded Service");
    f.answer("critical", "Major outage");
    f.answer("maintenance", "Service Under Maintenance");
    f.frame();
    for (const char* label : {"OK", "MINOR", "CRITICAL", "MAINT"})
        TEST_ASSERT_TRUE_MESSAGE(f.display.shows(label), label);
    TEST_ASSERT_FALSE(f.display.shows("ALL SYSTEMS OPERATIONAL"));
    TEST_ASSERT_TRUE(f.display.filled(palette::leaf, 7));
    TEST_ASSERT_TRUE(f.display.filled(palette::vermilion, 7));
    TEST_ASSERT_TRUE(f.display.filled(palette::blue, 7));
    f.display.capture.save("service-status-list");
}

void test_a_failed_page_is_named_below_the_list() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.answer("none");
    f.status.http.answer(200, "<html>captive portal</html>");
    f.status.tick();
    f.answer("none");
    f.answer("none");
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("ANTHROPIC NOT READABLE"));
    TEST_ASSERT_TRUE(f.display.shows("ERROR"));

    f.status.tick(std::chrono::seconds(60));
    f.status.http.failNetwork();
    f.status.tick();
    f.status.http.failNetwork();
    f.status.tick();
    f.answer("none");
    f.answer("none");
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("2 PAGES FAILED"));
}

void test_there_is_no_selection_plate() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.status.answerRoundOverWifi("none");
    f.frame();
    const auto& rectangles = f.display.rectangles;
    TEST_ASSERT_FALSE(std::any_of(rectangles.begin(), rectangles.end(), [](const auto& item) {
        return item.width == 228 && item.height == 16 && item.color.red == palette::ink.red &&
               item.color.blue == palette::ink.blue;
    }));
    // Up/Down change nothing.
    const auto draws = f.display.draws;
    f.frame({down, up});
    TEST_ASSERT_EQUAL_INT(draws, f.display.draws);
}

void test_an_unchanged_screen_is_not_repainted() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.frame();
    const auto draws = f.display.draws;
    f.frame();
    f.status.tick(milliseconds(500));
    f.frame();
    TEST_ASSERT_EQUAL_INT(draws, f.display.draws);
}

void test_the_header_counts_since_the_last_check_in_ten_second_steps() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("CHECKING"));
    f.status.answerRoundOverWifi("none");
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("0 SEC AGO"));
    f.status.tick(milliseconds(9999));
    const auto draws = f.display.draws;
    f.frame();
    TEST_ASSERT_EQUAL_INT(draws, f.display.draws);
    f.status.tick(milliseconds(1));
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("10 SEC AGO"));
    f.status.tick(milliseconds(40000));
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("50 SEC AGO"));
    f.display.capture.save("service-status-list-waiting");
    // At a minute the next round starts.
    f.status.tick(milliseconds(10000));
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("CHECKING"));
    TEST_ASSERT_EQUAL_STRING("CHECKING",
                             apps::serviceStatusHeader(f.status.service.snapshot()).c_str());
}

void test_r_checks_every_page_again() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.status.answerRoundOverWifi("none");
    f.frame({refresh});
    TEST_ASSERT_EQUAL_UINT(5, f.status.http.urls.size());
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("CHECKING"));
}

void test_without_any_connection_the_caption_explains_it() {
    AppFixture f;
    f.app.onActivate();
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("NO WI-FI OR COMPANION"));
    TEST_ASSERT_FALSE(f.display.shows("CHECKING"));
    f.display.capture.save("service-status-offline");
}

void test_pages_read_through_the_mac_look_the_same() {
    AppFixture f;
    f.status.connectCompanion();
    f.app.onActivate();
    f.status.answerCompanion(connectivity::ServiceStatusLevel::Major, "Elevated error rates");
    f.frame();
    TEST_ASSERT_FALSE(f.display.shows("ELEVATED ERROR RATES"));
    TEST_ASSERT_TRUE(f.display.shows("MAJOR"));
}

void test_closing_stops_the_checks() {
    AppFixture f;
    f.status.connectWifi();
    f.app.onActivate();
    f.app.onDeactivate();
    TEST_ASSERT_FALSE(f.status.service.active());
    f.status.tick(minutes(5));
    TEST_ASSERT_EQUAL_UINT(1, f.status.http.urls.size());
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_opening_starts_checks_and_shows_four_pending_rows);
    RUN_TEST(test_levels_show_labels_and_marks_without_the_page_summary);
    RUN_TEST(test_a_failed_page_is_named_below_the_list);
    RUN_TEST(test_there_is_no_selection_plate);
    RUN_TEST(test_an_unchanged_screen_is_not_repainted);
    RUN_TEST(test_the_header_counts_since_the_last_check_in_ten_second_steps);
    RUN_TEST(test_r_checks_every_page_again);
    RUN_TEST(test_without_any_connection_the_caption_explains_it);
    RUN_TEST(test_pages_read_through_the_mac_look_the_same);
    RUN_TEST(test_closing_stops_the_checks);
    return UNITY_END();
}
