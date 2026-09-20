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
    void clear(RgbColor) override {
        ++frames;
        texts.clear();
        rectangles.clear();
        dirty = true;
    }
    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height, color});
        dirty = true;
    }
    void drawText(PixelPosition position, const char* value, TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + static_cast<std::int32_t>(std::string(value).size()) * 6 *
                                          style.scale <=
                         240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        texts.push_back(value);
        dirty = true;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
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

void test_activates_without_led_and_idle_lcd_progress() {
    Display display;
    PomodoroService pomodoro;
    PomodoroApp app(pomodoro, display);
    app.onActivate();
    app.update({}, {});
    TEST_ASSERT_EQUAL(24, pomodoroLcdFilledSegments(pomodoro.snapshot()));
    TEST_ASSERT_EQUAL_UINT8(1, pomodoroCycleDisplay(pomodoro.snapshot()));
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_requires_no_capabilities);
    RUN_TEST(test_initial_view_and_space_pause_resume_reset_skip);
    RUN_TEST(test_deactivate_does_not_stop_service_and_reopen_renders_snapshot);
    RUN_TEST(test_activates_without_led_and_idle_lcd_progress);
    return UNITY_END();
}
