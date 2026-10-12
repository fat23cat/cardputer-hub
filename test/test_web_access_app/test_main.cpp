#include <unity.h>

#include "../support/ui_capture.h"
#include "../support/web_access_fakes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "apps/web_access/web_access_app.h"
#include "core/display/palette.h"
#include "core/display/qr_code.h"
#include "core/display/text_layout.h"

namespace {
using namespace cardputer_hub;
using namespace core;
using std::chrono::milliseconds;

class Display final : public IDisplayAdapter {
  public:
    void clear(RgbColor color) override {
        capture.clear(color);
        texts.clear();
        styles.clear();
        spans.clear();
        inkPixels.assign(240 * 135, false);
        ++clears;
    }
    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        capture.rectangle(position, width, height, color);
        const bool ink = color.red == palette::ink.red && color.green == palette::ink.green &&
                         color.blue == palette::ink.blue;
        for (std::int32_t y = position.y; y < position.y + height && y < 135; ++y)
            for (std::int32_t x = position.x; x < position.x + width && x < 240; ++x)
                inkPixels[static_cast<std::size_t>(y * 240 + x)] = ink;
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
    }
    void drawText(PixelPosition position, const char* value, TextStyle style) override {
        capture.text(position, value, style);
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0);
        TEST_ASSERT_TRUE(position.x + core::textWidth(value, style.scale) <= 240);
        TEST_ASSERT_TRUE(position.y + std::ceil(8 * style.scale) <= 135);
        texts.emplace_back(value);
        styles.push_back(style);
        spans.push_back({position.x, position.x + core::textWidth(value, style.scale)});
        ++drawnTexts;
    }
    bool shows(const std::string& value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    bool ink(std::int32_t x, std::int32_t y) const {
        return inkPixels[static_cast<std::size_t>(y * 240 + x)];
    }
    test_support::UiCapture capture;
    std::vector<std::string> texts;
    std::vector<bool> inkPixels = std::vector<bool>(240 * 135, false);
    std::vector<core::TextStyle> styles;
    // Horizontal extent of each drawn text.
    std::vector<std::pair<std::int32_t, std::int32_t>> spans;
    int clears = 0;
    int drawnTexts = 0;
};

const InputEvent enter{InputEventType::NamedKey, 0, NamedKey::Enter, {}};
const InputEvent qKey{InputEventType::PrintableCharacter, 'q', {}, {}};

// The password as the screen groups it: "abcd efgh ijkm".
std::string grouped(const std::string& password) {
    std::string result;
    for (std::size_t index = 0; index < password.size(); ++index) {
        if (index != 0 && index % 4 == 0)
            result.push_back(' ');
        result.push_back(password[index]);
    }
    return result;
}

struct Fixture {
    test_support::FakeAccessPoint accessPoint;
    test_support::FakeHttpServer server;
    test_support::MapStorage storageAdapter;
    core::Storage storage{storageAdapter};
    test_support::CountingRandom random;
    services::WebAccessService web{accessPoint, server, storage, random};
    ActionBus actions;
    Display display;
    apps::WebAccessApp app{web, actions, display};

    Fixture() {
        TEST_ASSERT_TRUE(actions.registerHandler(services::webAccessSetEnabledActionId, web) ==
                         RegistrationResult::Registered);
    }
    void frame(const InputEvents& input = {}) {
        web.update(milliseconds(16));
        app.update(input, milliseconds(16));
    }
};

void test_off_screen_offers_start() {
    Fixture f;
    f.app.onActivate();
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("WEB ACCESS"));
    TEST_ASSERT_TRUE(f.display.shows("OFF"));
    TEST_ASSERT_TRUE(f.display.shows("ESC BACK"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER START"));
    TEST_ASSERT_TRUE(f.display.shows("BLUETOOTH PAUSES WHILE ON"));
    TEST_ASSERT_EQUAL_INT(0, f.accessPoint.starts);
}

void test_enter_dispatches_start() {
    Fixture f;
    f.app.onActivate();
    f.frame();
    f.frame({enter});
    TEST_ASSERT_EQUAL(services::WebAccessState::Running, f.web.status().state);
    TEST_ASSERT_EQUAL_INT(1, f.accessPoint.starts);
}

void test_running_screen_shows_credentials() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame();
    const auto& status = f.web.status();
    TEST_ASSERT_TRUE(f.display.shows("NETWORK"));
    TEST_ASSERT_TRUE(f.display.shows(status.ssid));
    TEST_ASSERT_TRUE(f.display.shows("PASSWORD"));
    TEST_ASSERT_TRUE(f.display.shows(grouped(status.password)));
    TEST_ASSERT_TRUE(f.display.shows("OPEN"));
    TEST_ASSERT_TRUE(f.display.shows("192.168.4.1"));
    TEST_ASSERT_TRUE(f.display.shows("LOGIN"));
    TEST_ASSERT_TRUE(f.display.shows("admin"));
    TEST_ASSERT_TRUE(f.display.shows("NO CLIENTS"));
    TEST_ASSERT_TRUE(f.display.shows("ESC BACKGROUND"));
    TEST_ASSERT_TRUE(f.display.shows("Q QR"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER STOP"));
    f.display.capture.save("web-access-running");
}

void test_password_is_drawn_twice_as_large() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame();
    const auto text = grouped(f.web.status().password);
    const auto found = std::find(f.display.texts.begin(), f.display.texts.end(), text);
    TEST_ASSERT_TRUE(found != f.display.texts.end());
    const auto& style = f.display.styles[static_cast<std::size_t>(found - f.display.texts.begin())];
    TEST_ASSERT_EQUAL_FLOAT(2.0f, style.scale);
}

void test_q_shows_the_join_code_full_screen() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame({qKey});
    TEST_ASSERT_FALSE(f.display.shows("WEB ACCESS"));
    TEST_ASSERT_TRUE(f.display.shows("SCAN"));
    TEST_ASSERT_TRUE(f.display.shows("Q CLOSE"));
    f.display.capture.save("web-access-qr");

    const auto& status = f.web.status();
    const auto code = core::QrCode::encodeText(core::wifiQrPayload(status.ssid, status.password));
    TEST_ASSERT_TRUE(code.has_value());
    const int moduleSize = apps::webAccessQrModulePixels(code->size());
    const int origin = (240 - code->size() * moduleSize) / 2;
    const int top = (135 - code->size() * moduleSize) / 2;
    TEST_ASSERT_TRUE(moduleSize >= 3);
    TEST_ASSERT_TRUE(top >= 2 * moduleSize);
    // The labels leave the two-module light border around the code empty.
    const int extent = code->size() * moduleSize;
    for (const auto& [left, right] : f.display.spans)
        TEST_ASSERT_TRUE(right <= origin - 2 * moduleSize ||
                         left >= origin + extent + 2 * moduleSize);
    for (int y = 0; y < code->size(); ++y)
        for (int x = 0; x < code->size(); ++x)
            TEST_ASSERT_EQUAL(code->module(x, y),
                              f.display.ink(origin + x * moduleSize + moduleSize / 2,
                                            top + y * moduleSize + moduleSize / 2));
}

void test_q_or_escape_closes_the_join_code() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame({qKey});
    f.frame({qKey});
    TEST_ASSERT_TRUE(f.display.shows("WEB ACCESS"));
    TEST_ASSERT_TRUE(f.display.shows(grouped(f.web.status().password)));

    f.frame({qKey});
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("WEB ACCESS"));
    // Without the code, Escape leaves the app.
    TEST_ASSERT_FALSE(f.app.handleBack());
}

void test_enter_is_ignored_while_the_join_code_is_shown() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame({qKey});
    f.frame({enter});
    TEST_ASSERT_EQUAL(services::WebAccessState::Running, f.web.status().state);
}

void test_q_does_nothing_while_off() {
    Fixture f;
    f.app.onActivate();
    f.frame();
    f.frame({qKey});
    TEST_ASSERT_TRUE(f.display.shows("ENTER START"));
    TEST_ASSERT_FALSE(f.app.handleBack());
}

void test_client_count_redraws_header_only() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame();
    const auto clears = f.display.clears;
    f.accessPoint.clients = 1;
    f.web.update(services::WebAccessService::clientPollInterval);
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("1 CLIENT"));
    TEST_ASSERT_EQUAL_INT(clears, f.display.clears);
    f.accessPoint.clients = 2;
    f.web.update(services::WebAccessService::clientPollInterval);
    f.frame();
    TEST_ASSERT_TRUE(f.display.shows("2 CLIENTS"));
}

void test_unchanged_status_draws_nothing() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame();
    const auto texts = f.display.drawnTexts;
    f.frame();
    f.frame();
    TEST_ASSERT_EQUAL_INT(texts, f.display.drawnTexts);
}

void test_enter_while_running_stops() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.frame({enter});
    TEST_ASSERT_EQUAL(services::WebAccessState::Off, f.web.status().state);
    TEST_ASSERT_FALSE(f.accessPoint.running);
    TEST_ASSERT_TRUE(f.display.shows("ENTER START"));
}

void test_leaving_the_app_keeps_the_service_running() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    TEST_ASSERT_FALSE(f.app.handleBack());
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(services::WebAccessState::Running, f.web.status().state);
    TEST_ASSERT_TRUE(f.accessPoint.running);
}

void test_failure_offers_retry() {
    Fixture f;
    f.accessPoint.failStart = true;
    f.app.onActivate();
    f.frame({enter});
    TEST_ASSERT_TRUE(f.display.shows("ERROR"));
    TEST_ASSERT_TRUE(f.display.shows("WI-FI DID NOT START"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER RETRY"));
    f.accessPoint.failStart = false;
    f.frame({enter});
    TEST_ASSERT_EQUAL(services::WebAccessState::Running, f.web.status().state);
}

void test_reopening_redraws_everything() {
    Fixture f;
    f.app.onActivate();
    f.frame({enter});
    f.app.onDeactivate();
    const auto clears = f.display.clears;
    f.app.onActivate();
    f.frame();
    TEST_ASSERT_EQUAL_INT(clears + 1, f.display.clears);
    TEST_ASSERT_TRUE(f.display.shows(grouped(f.web.status().password)));
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_off_screen_offers_start);
    RUN_TEST(test_enter_dispatches_start);
    RUN_TEST(test_running_screen_shows_credentials);
    RUN_TEST(test_password_is_drawn_twice_as_large);
    RUN_TEST(test_q_shows_the_join_code_full_screen);
    RUN_TEST(test_q_or_escape_closes_the_join_code);
    RUN_TEST(test_enter_is_ignored_while_the_join_code_is_shown);
    RUN_TEST(test_q_does_nothing_while_off);
    RUN_TEST(test_client_count_redraws_header_only);
    RUN_TEST(test_unchanged_status_draws_nothing);
    RUN_TEST(test_enter_while_running_stops);
    RUN_TEST(test_leaving_the_app_keeps_the_service_running);
    RUN_TEST(test_failure_offers_retry);
    RUN_TEST(test_reopening_redraws_everything);
    return UNITY_END();
}
