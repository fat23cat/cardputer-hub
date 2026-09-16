#include "apps/launcher/assets/app_icons.h"
#include "apps/launcher/launcher.h"
#include "apps/launcher/launcher_graphics.h"
#include "apps/runtime/mini_app_runtime.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <unity.h>
#include <variant>
#include <vector>

using namespace cardputer_hub;

namespace {

class Display final : public core::IDisplayAdapter {
  public:
    void beginFrame() override { dirty = false; }
    void endFrame() override {
        if (dirty)
            ++presentations;
    }
    void clear(core::RgbColor) override {
        ++frames;
        texts.clear();
        drawnTexts.clear();
        rectangles.clear();
        dirty = true;
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        if ((position.x == 0 && position.y == 0 && width == 240 && height == 20) ||
            (position.x == 0 && position.y == apps::launcherRowTop && width == 240 &&
             height == 135 - apps::launcherRowTop)) {
            const auto cutoff = position.y;
            const auto limit = position.y + height;
            auto stale = [cutoff, limit](const DrawnText& text) {
                return text.position.y >= cutoff && text.position.y < limit;
            };
            drawnTexts.erase(std::remove_if(drawnTexts.begin(), drawnTexts.end(), stale),
                             drawnTexts.end());
            texts.clear();
            for (const auto& text : drawnTexts)
                texts.push_back(text.value);
        }
        rectangles.push_back({position, width, height, color});
        dirty = true;
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        texts.push_back(value);
        drawnTexts.push_back({position, value, style});
        dirty = true;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    bool hasColor(core::RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue;
        });
    }
    bool pixelHasColor(core::PixelPosition position, core::RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue && rectangle.position.x == position.x &&
                   rectangle.position.y == position.y && rectangle.width == 1 &&
                   rectangle.height == 1;
        });
    }
    std::optional<int> plateY() const {
        std::optional<int> y;
        for (const auto& rectangle : rectangles) {
            if (rectangle.width == apps::launcherRowWidth &&
                rectangle.height == apps::launcherRowHeight &&
                rectangle.position.x == apps::launcherRowLeft &&
                rectangle.color.red == core::palette::ink.red)
                y = rectangle.position.y;
        }
        return y;
    }
    struct Rectangle {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        core::RgbColor color;
    };
    struct DrawnText {
        core::PixelPosition position;
        std::string value;
        core::TextStyle style;
    };
    std::vector<std::string> texts;
    std::vector<DrawnText> drawnTexts;
    std::vector<Rectangle> rectangles;
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
};

class FakeMiniApp final : public apps::IMiniApp {
  public:
    void onActivate() override { ++activateCount; }
    void onDeactivate() override { ++deactivateCount; }
    void update(const core::InputEvents&, std::chrono::milliseconds) override { ++updateCount; }
    int activateCount = 0;
    int deactivateCount = 0;
    int updateCount = 0;
};

class CaptureHandler final : public core::IActionHandler {
  public:
    core::ActionHandlingResult handle(const core::Action& action) override {
        actions.push_back(action);
        return core::ActionHandlingResult::Handled;
    }
    std::vector<core::Action> actions;
};

struct Fixture {
    core::AppRegistry apps;
    core::CapabilityRegistry capabilities;
    apps::MiniAppRuntime runtime{apps, capabilities};
    core::ActionBus bus;
    Display display;
    CaptureHandler handler;
    FakeMiniApp first;
    FakeMiniApp second;
    apps::Launcher launcher{apps, runtime, bus, display};

    Fixture() {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(core::RegistrationResult::Registered),
                                static_cast<unsigned>(bus.registerHandler("app.open", handler)));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(core::RegistrationResult::Registered),
                                static_cast<unsigned>(bus.registerHandler("ui.back", handler)));
    }

    void registerApp(const char* id, const char* name, const char* icon,
                     std::vector<std::string> required = {}) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(core::AppRegistrationResult::Registered),
            static_cast<unsigned>(apps.registerApp(
                {id, name, icon, std::string(id) + "/home", std::move(required)})));
    }

    void bind(const char* id, apps::IMiniApp& app) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(apps::MiniAppInstanceRegistrationResult::Registered),
            static_cast<unsigned>(runtime.registerInstance(id, app)));
    }

    void tick(const core::InputEvents& input = {}, std::chrono::milliseconds elapsed = {}) {
        display.beginFrame();
        launcher.update(input, elapsed);
        display.endFrame();
    }
};

const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
const core::InputEvent up{core::InputEventType::NamedKey, 0, core::NamedKey::Up, {}};
const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};

void test_empty_registry_shows_no_apps_and_escape_returns_home() {
    Fixture f;
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("NO APPS"));
    TEST_ASSERT_FALSE(f.display.shows("01"));
    f.tick({enter});
    TEST_ASSERT_TRUE(f.handler.actions.empty());
    f.tick({escape});
    TEST_ASSERT_EQUAL_UINT32(1, f.handler.actions.size());
    TEST_ASSERT_EQUAL_STRING("ui.back", f.handler.actions.front().id.c_str());
}

void test_apps_appear_in_registration_order() {
    Fixture f;
    f.registerApp("weather", "WEATHER", "weather");
    f.registerApp("system", "SYSTEM", "system");
    f.bind("weather", f.first);
    f.bind("system", f.second);
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.shows("01"));
    TEST_ASSERT_TRUE(f.display.shows("WEATHER"));
    TEST_ASSERT_TRUE(f.display.shows("02"));
    TEST_ASSERT_TRUE(f.display.shows("SYSTEM"));
    TEST_ASSERT_TRUE(f.display.shows("1/2"));
}

void test_selection_starts_at_first_and_does_not_wrap() {
    Fixture f;
    f.registerApp("alpha", "ALPHA", "");
    f.registerApp("beta", "BETA", "");
    f.bind("alpha", f.first);
    f.bind("beta", f.second);
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_EQUAL_INT(apps::launcherRowTop, f.display.plateY().value_or(-1));
    f.tick({up});
    TEST_ASSERT_EQUAL_INT(apps::launcherRowTop, f.display.plateY().value_or(-1));
    TEST_ASSERT_TRUE(f.display.shows("1/2"));
    f.tick({down, down});
    f.tick({}, std::chrono::milliseconds(400));
    TEST_ASSERT_EQUAL_INT(apps::launcherRowTop + apps::launcherRowHeight,
                          f.display.plateY().value_or(-1));
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
}

void test_three_row_window_keeps_selection_visible() {
    Fixture f;
    FakeMiniApp extras[3];
    f.registerApp("one", "ONE", "");
    f.registerApp("two", "TWO", "");
    f.registerApp("three", "THREE", "");
    f.registerApp("four", "FOUR", "");
    f.registerApp("five", "FIVE", "");
    f.bind("one", f.first);
    f.bind("two", f.second);
    f.bind("three", extras[0]);
    f.bind("four", extras[1]);
    f.bind("five", extras[2]);
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.shows("ONE"));
    TEST_ASSERT_TRUE(f.display.shows("TWO"));
    TEST_ASSERT_TRUE(f.display.shows("THREE"));
    TEST_ASSERT_FALSE(f.display.shows("FOUR"));
    f.tick({down, down, down});
    TEST_ASSERT_TRUE(f.display.shows("TWO"));
    TEST_ASSERT_TRUE(f.display.shows("THREE"));
    TEST_ASSERT_TRUE(f.display.shows("FOUR"));
    TEST_ASSERT_FALSE(f.display.shows("ONE"));
    TEST_ASSERT_TRUE(f.display.shows("4/5"));
}

void test_known_icon_and_fallback_are_14_by_14() {
    Fixture f;
    f.registerApp("system", "SYSTEM", "system");
    f.registerApp("weather", "WEATHER", "missing");
    f.bind("system", f.first);
    f.bind("weather", f.second);
    f.launcher.activate();
    f.tick();
    bool systemBone = false;
    for (int y = 0; y < apps::assets::appIconSize && !systemBone; ++y) {
        for (int x = 0; x < apps::assets::appIconSize; ++x) {
            if (apps::assets::systemAppIcon[y][x] == '#' &&
                f.display.pixelHasColor({apps::launcherIconX + x, apps::launcherRowTop + 11 + y},
                                        core::palette::bone)) {
                systemBone = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(systemBone);
    bool fallbackInk = false;
    for (int y = 0; y < apps::assets::appIconSize; ++y) {
        for (int x = 0; x < apps::assets::appIconSize; ++x) {
            if (apps::assets::fallbackAppIcon[y][x] == '#' &&
                f.display.pixelHasColor({apps::launcherIconX + x,
                                         apps::launcherRowTop + apps::launcherRowHeight + 11 + y},
                                        core::palette::ink))
                fallbackInk = true;
        }
    }
    TEST_ASSERT_TRUE(fallbackInk);
}

void test_selected_icon_is_inverted() {
    Fixture f;
    f.registerApp("system", "SYSTEM", "system");
    f.bind("system", f.first);
    f.launcher.activate();
    f.tick();
    bool bone = false;
    bool ink = false;
    for (int y = 0; y < apps::assets::appIconSize; ++y) {
        for (int x = 0; x < apps::assets::appIconSize; ++x) {
            if (apps::assets::systemAppIcon[y][x] != '#')
                continue;
            const core::PixelPosition pixel{apps::launcherIconX + x, apps::launcherRowTop + 11 + y};
            bone = bone || f.display.pixelHasColor(pixel, core::palette::bone);
            ink = ink || f.display.pixelHasColor(pixel, core::palette::ink);
        }
    }
    TEST_ASSERT_TRUE(bone);
    TEST_ASSERT_FALSE(ink);
}

void test_availability_indicators_use_leaf_and_vermilion() {
    Fixture f;
    f.registerApp("ready", "READY", "");
    f.registerApp("orphan", "ORPHAN", "");
    f.registerApp("weather", "WEATHER", "", {"WIFI"});
    f.bind("ready", f.first);
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.hasColor(core::palette::leaf));
    TEST_ASSERT_TRUE(f.display.hasColor(core::palette::vermilion));
}

void test_unavailable_enter_keeps_selection_and_shows_reason() {
    Fixture f;
    f.registerApp("weather", "WEATHER", "", {"WIFI"});
    f.bind("weather", f.first);
    f.launcher.activate();
    f.tick();
    f.tick({enter});
    TEST_ASSERT_TRUE(f.handler.actions.empty());
    TEST_ASSERT_EQUAL_INT(0, f.first.activateCount);
    f.tick({}, apps::Launcher::overlayEnterDuration);
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("WEATHER"));
}

void test_missing_instance_and_unknown_reasons() {
    Fixture f;
    f.registerApp("orphan", "ORPHAN", "");
    f.launcher.activate();
    f.tick({enter});
    f.tick({}, apps::Launcher::overlayEnterDuration);
    TEST_ASSERT_TRUE(f.display.shows("APP NOT READY"));
}

void test_overlay_animation_is_elapsed_driven_and_settles() {
    Fixture f;
    f.registerApp("weather", "WEATHER", "", {"WIFI"});
    f.bind("weather", f.first);
    f.launcher.activate();
    f.tick({enter});
    TEST_ASSERT_FALSE(f.display.shows("REQUIRES WIFI"));
    f.tick({}, apps::Launcher::overlayEnterDuration);
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
    f.tick({}, apps::Launcher::overlayHoldDuration);
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
    f.tick({}, apps::Launcher::overlayExitDuration);
    TEST_ASSERT_FALSE(f.display.shows("REQUIRES WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    const auto presentations = f.display.presentations;
    f.tick({}, std::chrono::milliseconds(40));
    TEST_ASSERT_EQUAL_INT(presentations, f.display.presentations);
    TEST_ASSERT_FALSE(f.launcher.animating());
}

void test_repeated_enter_restarts_overlay_duration() {
    Fixture f;
    f.registerApp("weather", "WEATHER", "", {"WIFI"});
    f.bind("weather", f.first);
    f.launcher.activate();
    f.tick({enter});
    f.tick({}, apps::Launcher::overlayEnterDuration);
    f.tick({}, std::chrono::milliseconds(1000));
    f.tick({enter});
    f.tick({}, apps::Launcher::overlayHoldDuration);
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
    f.tick({}, apps::Launcher::overlayExitDuration);
    TEST_ASSERT_FALSE(f.display.shows("REQUIRES WIFI"));
}

void test_long_capability_overlay_reason_is_truncated_to_display_width() {
    Fixture f;
    f.registerApp("weather", "WEATHER", "", {"COMPANION_AUTHENTICATED_SESSION"});
    f.bind("weather", f.first);
    f.launcher.activate();
    f.tick({enter});
    f.tick({}, apps::Launcher::overlayEnterDuration);
    TEST_ASSERT_FALSE(f.display.shows("REQUIRES COMPANION_AUTHENTICATED_SESSION"));
    bool truncated = false;
    for (const auto& text : f.display.texts) {
        if (text.rfind("REQUIRES ", 0) == 0 && text.size() >= 3 &&
            text.compare(text.size() - 3, 3, "...") == 0) {
            truncated = true;
            TEST_ASSERT_TRUE(6 + static_cast<int>(text.size()) * 6 <= 240);
        }
    }
    TEST_ASSERT_TRUE(truncated);
}

void test_successful_enter_dispatches_exact_app_open() {
    Fixture f;
    f.registerApp("system", "SYSTEM", "system");
    f.bind("system", f.first);
    f.launcher.activate();
    f.tick({enter});
    TEST_ASSERT_EQUAL_UINT32(1, f.handler.actions.size());
    TEST_ASSERT_EQUAL_STRING("app.open", f.handler.actions.front().id.c_str());
    const auto* value = f.handler.actions.front().findParameter("appId");
    TEST_ASSERT_TRUE(value != nullptr);
    const auto* appId = std::get_if<std::string>(value);
    TEST_ASSERT_TRUE(appId != nullptr);
    TEST_ASSERT_EQUAL_STRING("system", appId->c_str());
    TEST_ASSERT_EQUAL_INT(0, f.first.activateCount);
}

void test_selection_is_remembered_across_activate() {
    Fixture f;
    f.registerApp("alpha", "ALPHA", "");
    f.registerApp("beta", "BETA", "");
    f.bind("alpha", f.first);
    f.bind("beta", f.second);
    f.launcher.activate();
    f.tick({down});
    f.tick({}, std::chrono::milliseconds(400));
    f.launcher.deactivate();
    f.launcher.activate();
    f.tick();
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_EQUAL_INT(apps::launcherRowTop + apps::launcherRowHeight,
                          f.display.plateY().value_or(-1));
}

void test_selection_plate_moves_immediately_then_settles() {
    Fixture f;
    f.registerApp("alpha", "ALPHA", "");
    f.registerApp("beta", "BETA", "");
    f.bind("alpha", f.first);
    f.bind("beta", f.second);
    f.launcher.activate();
    f.tick();
    const auto start = f.display.plateY().value_or(-1);
    f.tick({down}, std::chrono::milliseconds(16));
    const auto moving = f.display.plateY().value_or(-1);
    TEST_ASSERT_TRUE(moving > start);
    TEST_ASSERT_TRUE(moving < start + apps::launcherRowHeight);
    f.tick({}, std::chrono::milliseconds(400));
    TEST_ASSERT_EQUAL_INT(start + apps::launcherRowHeight, f.display.plateY().value_or(-1));
    TEST_ASSERT_FALSE(f.launcher.animating());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_registry_shows_no_apps_and_escape_returns_home);
    RUN_TEST(test_apps_appear_in_registration_order);
    RUN_TEST(test_selection_starts_at_first_and_does_not_wrap);
    RUN_TEST(test_three_row_window_keeps_selection_visible);
    RUN_TEST(test_known_icon_and_fallback_are_14_by_14);
    RUN_TEST(test_selected_icon_is_inverted);
    RUN_TEST(test_availability_indicators_use_leaf_and_vermilion);
    RUN_TEST(test_unavailable_enter_keeps_selection_and_shows_reason);
    RUN_TEST(test_missing_instance_and_unknown_reasons);
    RUN_TEST(test_overlay_animation_is_elapsed_driven_and_settles);
    RUN_TEST(test_repeated_enter_restarts_overlay_duration);
    RUN_TEST(test_long_capability_overlay_reason_is_truncated_to_display_width);
    RUN_TEST(test_successful_enter_dispatches_exact_app_open);
    RUN_TEST(test_selection_is_remembered_across_activate);
    RUN_TEST(test_selection_plate_moves_immediately_then_settles);
    return UNITY_END();
}
