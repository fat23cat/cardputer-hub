#include "../support/ui_capture.h"
#include "core/display/text_layout.h"
#include <unity.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "apps/pomodoro/pomodoro_app.h"
#include "apps/runtime/mini_app_runtime.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"

using namespace cardputer_hub;
using namespace cardputer_hub::apps;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace std::chrono_literals;

namespace {
const InputEvent space{InputEventType::PrintableCharacter, ' ', {}, {}};
const InputEvent resetKey{InputEventType::PrintableCharacter, 'r', {}, {}};
const InputEvent skipKey{InputEventType::NamedKey, 0, NamedKey::Right, {}};
const InputEvent skipAlias{InputEventType::PrintableCharacter, 's', {}, {}};

class Display final : public IDisplayAdapter {
  public:
    void beginFrame() override { dirty = false; }
    void endFrame() override {
        if (dirty)
            ++presentations;
    }
    void clear(RgbColor color) override {
        capture.clear(color);
        ++frames;
        texts.clear();
        rectangles.clear();
        dirty = true;
    }
    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        capture.rectangle(position, width, height, color);
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height, color});
        dirty = true;
    }
    void drawText(PixelPosition position, const char* value, TextStyle style) override {
        capture.text(position, value, style);
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + core::textWidth(value, style.scale) <= 240);
        TEST_ASSERT_TRUE(position.y + std::ceil(8 * style.scale) <= 135);
        texts.push_back(value);
        dirty = true;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    test_support::UiCapture capture;
    struct Rectangle {
        PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        RgbColor color;
    };
    std::vector<std::string> texts;
    std::vector<Rectangle> rectangles;
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
};

PomodoroDurations durations() { return {25min, 5min, 15min}; }

void test_registry_requires_no_capabilities() {
    AppRegistry apps;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(apps.registerApp(
                                {pomodoroAppId, "POMODORO", "pomodoro", "pomodoro", {}})));
    const auto* descriptor = apps.find(pomodoroAppId);
    TEST_ASSERT_NOT_NULL(descriptor);
    TEST_ASSERT_TRUE(descriptor->requiredCapabilities.empty());
    TEST_ASSERT_EQUAL_STRING("POMODORO", descriptor->displayName.c_str());
}

void test_initial_view_and_space_pause_resume_reset_skip() {
    Display display;
    PomodoroService pomodoro(durations());
    PomodoroApp app(pomodoro, display);
    app.onActivate();
    app.update({}, {});
    display.capture.save("pomodoro");
    TEST_ASSERT_TRUE(display.shows("FOCUS"));
    TEST_ASSERT_TRUE(display.shows("1 / 4"));
    TEST_ASSERT_TRUE(display.shows("SPACE  START"));

    app.update({space}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    TEST_ASSERT_TRUE(display.shows("SPACE  PAUSE"));

    app.update({space}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Paused),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    TEST_ASSERT_TRUE(display.shows("PAUSED"));

    app.update({space}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));

    app.update({skipKey}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::ShortBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_TRUE(display.shows("SHORT BREAK"));

    app.update({skipAlias}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::Work),
                            static_cast<unsigned>(pomodoro.snapshot().phase));

    for (int i = 0; i < 5; ++i)
        app.update({skipKey}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroPhase::LongBreak),
                            static_cast<unsigned>(pomodoro.snapshot().phase));
    TEST_ASSERT_TRUE(display.shows("LONG BREAK"));

    app.update({resetKey}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Idle),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    TEST_ASSERT_TRUE(display.shows("FOCUS"));
    TEST_ASSERT_TRUE(display.shows("SPACE  START"));
}

void test_deactivate_does_not_stop_service_and_reopen_renders_snapshot() {
    Display display;
    PomodoroService pomodoro({10s, 5s, 15s});
    PomodoroApp app(pomodoro, display);
    AppRegistry apps;
    CapabilityRegistry capabilities;
    MiniAppRuntime runtime(apps, capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(apps.registerApp(
                                {pomodoroAppId, "POMODORO", "pomodoro", "pomodoro", {}})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppInstanceRegistrationResult::Registered),
                            static_cast<unsigned>(runtime.registerInstance(pomodoroAppId, app)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate(pomodoroAppId)));
    app.update({space}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppDeactivationResult::Deactivated),
                            static_cast<unsigned>(runtime.deactivate()));
    pomodoro.update(4s);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PomodoroRunState::Running),
                            static_cast<unsigned>(pomodoro.snapshot().runState));
    TEST_ASSERT_EQUAL_INT64(std::chrono::milliseconds(6s).count(),
                            pomodoro.snapshot().remaining.count());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate(pomodoroAppId)));
    app.update({}, {});
    TEST_ASSERT_TRUE(display.shows("FOCUS"));
    TEST_ASSERT_TRUE(display.shows("SPACE  PAUSE"));
}

void test_remaining_formats_as_mm_ss_and_clamps_overflow() {
    PomodoroSnapshot snapshot{};
    char text[6] = {};
    snapshot.remaining = 25min;
    formatPomodoroRemaining(snapshot, text);
    TEST_ASSERT_EQUAL_STRING("25:00", text);
    snapshot.remaining = std::chrono::milliseconds(5 * 60 * 1000 + 7 * 1000);
    formatPomodoroRemaining(snapshot, text);
    TEST_ASSERT_EQUAL_STRING("05:07", text);
    snapshot.remaining = std::chrono::milliseconds(-1);
    formatPomodoroRemaining(snapshot, text);
    TEST_ASSERT_EQUAL_STRING("00:00", text);
    snapshot.remaining = std::chrono::hours(3);
    formatPomodoroRemaining(snapshot, text);
    TEST_ASSERT_EQUAL_STRING("99:59", text);
}

void test_activates_without_led_and_idle_lcd_progress() {
    Display display;
    PomodoroService pomodoro;
    PomodoroApp app(pomodoro, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_EQUAL(24, pomodoroLcdFilledSegments(pomodoro.snapshot()));
    TEST_ASSERT_EQUAL_UINT8(1, pomodoroCycleDisplay(pomodoro.snapshot()));
}

void test_countdown_and_pause_repaint_only_changed_regions() {
    Display display;
    PomodoroService pomodoro({61s, 5s, 15s});
    PomodoroApp app(pomodoro, display);
    app.onActivate();
    app.update({space}, {});
    const auto fullClears = display.frames;
    display.rectangles.clear();
    display.texts.clear();
    pomodoro.update(1s);
    app.update({}, {});
    TEST_ASSERT_EQUAL_INT(fullClears, display.frames);
    TEST_ASSERT_TRUE(display.texts.empty());
    TEST_ASSERT_FALSE(display.rectangles.empty());
    for (const auto& rectangle : display.rectangles) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(143, rectangle.position.x);
        TEST_ASSERT_LESS_OR_EQUAL_INT(163, rectangle.position.x + rectangle.width);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(36, rectangle.position.y);
        TEST_ASSERT_LESS_OR_EQUAL_INT(64, rectangle.position.y + rectangle.height);
    }
    display.rectangles.clear();
    pomodoro.update(1s); // 01:00 -> 00:59 changes three digits.
    app.update({}, {});
    const auto erasedDigits = std::count_if(display.rectangles.begin(), display.rectangles.end(),
                                            [](const Display::Rectangle& rectangle) {
                                                return rectangle.position.y == 36 &&
                                                       rectangle.width == 20 &&
                                                       rectangle.height == 28;
                                            });
    TEST_ASSERT_EQUAL_UINT(3, erasedDigits);
    display.rectangles.clear();
    pomodoro.update(2s); // The last progress segment becomes empty.
    app.update({}, {});
    TEST_ASSERT_TRUE(std::any_of(display.rectangles.begin(), display.rectangles.end(),
                                 [](const Display::Rectangle& rectangle) {
                                     return rectangle.position.x == 219 &&
                                            rectangle.position.y == 88 && rectangle.width == 8 &&
                                            rectangle.height == 6 &&
                                            rectangle.color.red == palette::pale.red;
                                 }));
    display.capture.save("pomodoro-countdown");
    display.rectangles.clear();
    app.update({space}, {});
    TEST_ASSERT_EQUAL_INT(fullClears, display.frames);
    TEST_ASSERT_TRUE(display.shows("PAUSED"));
    TEST_ASSERT_EQUAL_UINT(1, display.rectangles.size());
    TEST_ASSERT_EQUAL_INT(118, display.rectangles[0].position.y);
    display.rectangles.clear();
    app.update({skipKey}, {});
    TEST_ASSERT_EQUAL_INT(fullClears, display.frames);
    TEST_ASSERT_TRUE(display.shows("SHORT BREAK"));
    const auto leafSegments = std::count_if(display.rectangles.begin(), display.rectangles.end(),
                                            [](const Display::Rectangle& rectangle) {
                                                return rectangle.position.y == 88 &&
                                                       rectangle.width == 8 &&
                                                       rectangle.color.green == palette::leaf.green;
                                            });
    TEST_ASSERT_EQUAL_UINT(24, leafSegments);
    display.capture.save("pomodoro-break");
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_requires_no_capabilities);
    RUN_TEST(test_initial_view_and_space_pause_resume_reset_skip);
    RUN_TEST(test_deactivate_does_not_stop_service_and_reopen_renders_snapshot);
    RUN_TEST(test_remaining_formats_as_mm_ss_and_clamps_overflow);
    RUN_TEST(test_activates_without_led_and_idle_lcd_progress);
    RUN_TEST(test_countdown_and_pause_repaint_only_changed_regions);
    return UNITY_END();
}
