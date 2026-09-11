#include "apps/hosts/host_settings.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unity.h>
#include <vector>

using namespace cardputer_hub;
namespace {
class Memory final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {core::StorageReadStatus::NotFound, {}};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes&) override {
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
};
class Display final : public core::IDisplayAdapter {
  public:
    void beginFrame() override {
        TEST_ASSERT_FALSE(inFrame);
        inFrame = true;
        dirty = false;
    }
    void endFrame() override {
        TEST_ASSERT_TRUE(inFrame);
        inFrame = false;
        if (dirty)
            ++presentations;
    }
    void beginTransition(core::SlideDirection direction) override {
        transitions.push_back(direction);
    }
    std::vector<core::SlideDirection> transitions;
    bool inFrame = false, dirty = false;
    int presentations = 0;
    struct Rectangle {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
    };
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height});
        std::ostringstream line;
        line << "R " << position.x << ' ' << position.y << ' ' << width << ' ' << height << ' '
             << unsigned(color.red) << ' ' << unsigned(color.green) << ' ' << unsigned(color.blue);
        commands.push_back(line.str());
        dirty = true;
    }
    void clear(core::RgbColor color) override {
        ++frames;
        dirty = true;
        texts.clear();
        commands = {"C " + std::to_string(color.red) + " " + std::to_string(color.green) + " " +
                    std::to_string(color.blue)};
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        texts.push_back(value);
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        std::ostringstream line;
        line << "T " << position.x << ' ' << position.y << ' ' << unsigned(style.scale) << ' '
             << unsigned(style.foreground.red) << ' ' << unsigned(style.foreground.green) << ' '
             << unsigned(style.foreground.blue) << ' ' << unsigned(style.background.red) << ' '
             << unsigned(style.background.green) << ' ' << unsigned(style.background.blue) << ' '
             << std::quoted(value);
        commands.push_back(line.str());
        dirty = true;
    }
    void capture(const char* name) const {
        if (const auto* directory = std::getenv("CARDPUTER_UI_CAPTURE_DIR")) {
            std::ofstream output(std::string(directory) + "/" + name + ".draw");
            TEST_ASSERT_TRUE(output.good());
            for (const auto& command : commands)
                output << command << '\n';
        }
    }
    std::vector<std::string> commands;
    std::vector<Rectangle> rectangles;
    int frames = 0;
    std::vector<std::string> texts;
};
class Actions final : public core::IActionHandler {
  public:
    core::ActionHandlingResult handle(const core::Action& action) override {
        seen.push_back(action);
        return core::ActionHandlingResult::Handled;
    }
    std::vector<core::Action> seen;
};
// Use the production Service state view without starting its hardware backend.
// This adapter must remain unused by the UI: intentions go through ActionBus.
class Adapter final : public connectivity::IBluetoothAdapter {
  public:
    connectivity::BluetoothAdapterResult initialize(const connectivity::BluetoothDeviceConfig&,
                                                    std::uint32_t) override {
        TEST_FAIL_MESSAGE("UI touched hardware");
        return {};
    }
    connectivity::BluetoothAdapterResult shutdown() override { return {}; }
    connectivity::BluetoothAdvertisingResult
    startAdvertising(std::uint32_t, std::optional<connectivity::BluetoothBondReference>) override {
        return {};
    }
    connectivity::BluetoothAdapterResult requestAdvertisingStop() override { return {}; }
    connectivity::BluetoothAdapterResult
    disconnectPeer(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothPollResult pollEvent() override { return {}; }
    connectivity::BluetoothBondQueryResult bondState(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult beginPairing(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    restoreBondSecurity(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    respondToPairing(connectivity::BluetoothPeerHandle, connectivity::BluetoothPairingChallengeType,
                     bool, std::optional<std::uint32_t>) override {
        return {};
    }
    connectivity::BluetoothBondListResult bonds() override { return {}; }
    connectivity::BluetoothBondReferenceResult
    bondReference(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    deleteBond(const connectivity::BluetoothBondReference&) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    deleteBondForPeer(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothHidAdapterResult
    hidReadiness(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothHidAdapterResult sendHidReport(connectivity::BluetoothPeerHandle,
                                                          const connectivity::HidReport&) override {
        TEST_FAIL_MESSAGE("UI sent host input");
        return {};
    }
    connectivity::BluetoothHidAdapterResult
    releaseHidReports(connectivity::BluetoothPeerHandle) override {
        return {};
    }
};
struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService config{storage};
    Adapter adapter;
    connectivity::BluetoothService bluetooth{adapter};
    services::HostService hosts{bluetooth, config};
    core::ActionBus bus;
    Actions actions;
    Display display;
    apps::HostSettings ui{hosts, bus, display};
    Fixture() {
        services::HostConfiguration value;
        value.hosts = {{8, "Office laptop", {}}, {9, "Travel laptop", {}}};
        value.hosts[0].bond.bytes[0] = 1;
        value.hosts[1].bond.bytes[0] = 2;
        value.nextHostId = 10;
        TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
        for (const auto* id : {"host.select", "host.rename", "host.bluetooth"})
            bus.registerHandler(id, actions);
    }
};
core::InputEvent character(char c) { return {core::InputEventType::PrintableCharacter, c, {}, {}}; }
const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};

void test_navigation_uses_unmodified_arrow_keys_but_rename_keeps_punctuation() {
    Fixture f;
    f.ui.update({character('.'), character('.'), character('.'), character(';'), enter, enter});
    TEST_ASSERT_EQUAL_UINT(1, f.actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.select", f.actions.seen.back().id.c_str());
    TEST_ASSERT_EQUAL_INT(8, std::get<std::int32_t>(*f.actions.seen.back().findParameter("id")));
    f.ui.update({character('r'), character(';'), character('.'), enter});
    TEST_ASSERT_EQUAL_UINT(2, f.actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.rename", f.actions.seen.back().id.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Office laptop;.",
        std::get<std::string>(*f.actions.seen.back().findParameter("name")).c_str());
}

void test_bluetooth_footer_is_quiet_and_x_has_no_destructive_action() {
    Fixture f;
    f.ui.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Esc Home") ==
                     f.display.texts.end());
    for (const auto& text : f.display.texts) {
        TEST_ASSERT_TRUE(text.find("Move") == std::string::npos);
        TEST_ASSERT_TRUE(text.find("Enter") == std::string::npos);
        TEST_ASSERT_TRUE(text.find("Forget all") == std::string::npos);
    }
    f.ui.update({character('x')});
    TEST_ASSERT_FALSE(f.ui.modal());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_host_menu_back_and_incremental_navigation() {
    Fixture f;
    f.ui.update({down, down, enter});
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    f.display.capture("host-actions");
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    f.ui.update({down, enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "DELETE HOST") !=
                     f.display.texts.end());
    f.display.capture("host-delete");
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    f.ui.update({escape});
    TEST_ASSERT_TRUE(f.ui.modal());
    f.ui.update({escape});
    TEST_ASSERT_FALSE(f.ui.modal());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_focus_move_only_repaints_changed_rows_without_clearing_screen() {
    Fixture f;
    f.ui.update({});
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL(1, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(5, f.display.texts.size());
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 24 &&
                         rectangle.position.y + rectangle.height <= 94);
    }
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({});
    TEST_ASSERT_TRUE(f.display.texts.empty());
    TEST_ASSERT_TRUE(f.display.rectangles.empty());
}

void test_scrolling_repaints_list_content_and_return_from_rename_invalidates_list_cache() {
    Fixture f;
    auto value = f.config.value();
    value.hosts.push_back({10, "Third laptop", {}});
    value.hosts.back().bond.bytes[0] = 3;
    value.nextHostId = 11;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.ui.update({down, down, down});
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL(1, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(8, f.display.texts.size());
    TEST_ASSERT_EQUAL_STRING("02", f.display.texts.front().c_str());
    TEST_ASSERT_EQUAL_STRING("Add device", f.display.texts[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Third laptop", f.display.texts.back().c_str());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 24 &&
                         rectangle.position.y + rectangle.height <= 94);
    }
    f.ui.update({character('r')});
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    f.ui.update({escape});
    TEST_ASSERT_EQUAL(3, f.display.frames);
    TEST_ASSERT_EQUAL_STRING("BLUETOOTH", f.display.texts.front().c_str());
    f.display.texts.clear();
    f.ui.update({});
    TEST_ASSERT_TRUE(f.display.texts.empty());
}

const core::InputEvent settingsChord{
    core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {false, false, false, false, true}};
void test_shell_boots_simple_home_and_opens_settings_before_bluetooth() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "OFF") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(),
                               "BLUETOOTH SETTINGS") == f.display.texts.end());
    f.display.capture("home");
    const auto frames = f.display.frames;
    shell.update({enter, character('b')});
    TEST_ASSERT_EQUAL(frames, f.display.frames);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    shell.update({settingsChord});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Bluetooth") !=
                     f.display.texts.end());
    f.display.capture("settings");
    const auto settingsFrames = f.display.frames;
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL(settingsFrames, f.display.frames);
    shell.update({enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "BLUETOOTH") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    f.display.capture("bluetooth");
    shell.update({settingsChord}); // From the BLE list, return to Settings.
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    shell.update({character('`')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    shell.update({settingsChord, enter, character('`')}); // Preserve Esc Home in BLE.
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_shell_back_cancels_rename_before_returning_home_and_preserves_host_intent() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    shell.update({settingsChord, enter, character('.'), character('.'), character('r')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "HOST NAME") !=
                     f.display.texts.end());
    f.display.capture("rename");
    const auto editingFrames = f.display.frames;
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL(editingFrames, f.display.frames);
    shell.update({escape});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "BLUETOOTH") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    shell.update({escape});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_home_and_panel_show_selected_host_and_do_not_navigate_on_state_updates() {
    Fixture f;
    auto value = f.config.value();
    value.activeHost = 9;
    value.hosts.back().name = "Work laptop for projects";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    f.display.capture("home-selected");
    shell.update({settingsChord, enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED") !=
                     f.display.texts.end());
    f.display.capture("bluetooth-selected");
    value.hosts.back().name = "Renamed host";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.display.texts.clear();
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "HOME") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Renamed host") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_plain_tab_opens_settings_and_leaves_editing_intact() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};
    shell.update({tab});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    shell.update({enter, down, down, character('r')});
    const auto presentations = f.display.presentations;
    shell.update({tab});
    TEST_ASSERT_TRUE(f.ui.modal());
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_page_transitions_follow_navigation_and_ignore_focus_or_status_refresh() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    TEST_ASSERT_TRUE(f.display.transitions.empty());
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL_UINT(1, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);
    shell.update({enter});
    TEST_ASSERT_EQUAL_UINT(2, f.display.transitions.size());
    shell.update({down, down});
    TEST_ASSERT_EQUAL_UINT(2, f.display.transitions.size());
    shell.update({enter});
    TEST_ASSERT_EQUAL_UINT(3, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);
    shell.update({character('r')});
    TEST_ASSERT_EQUAL_UINT(4, f.display.transitions.size());
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    shell.update({escape});
    TEST_ASSERT_EQUAL_UINT(5, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
    shell.update({escape, settingsChord, escape});
    TEST_ASSERT_EQUAL_UINT(8, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
    const auto transitions = f.display.transitions.size();
    shell.update({}, std::chrono::milliseconds(500));
    TEST_ASSERT_EQUAL_UINT(transitions, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_system_button_opens_settings_without_radio_actions_and_preserves_modals() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    const core::InputEvent menu{core::InputEventType::NamedKey, 0, core::NamedKey::SystemMenu, {}};
    shell.update({menu});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    const auto presentations = f.display.presentations;
    shell.update({menu});
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    shell.update({enter});
    shell.update({menu});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    shell.update({enter, down, down, enter});
    TEST_ASSERT_TRUE(f.ui.modal());
    const auto modalPresentations = f.display.presentations;
    shell.update({menu});
    TEST_ASSERT_TRUE(f.ui.modal());
    TEST_ASSERT_EQUAL(modalPresentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_home_name_fits_without_changing_the_saved_label() {
    Fixture f;
    auto value = f.config.value();
    value.activeHost = 9;
    value.hosts.back().name = std::string(24, 'W');
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    const auto label = apps::fitHomeHostName(value.hosts.back().name);
    TEST_ASSERT_TRUE(label.size() < 24);
    TEST_ASSERT_EQUAL_STRING("...", label.substr(label.size() - 3).c_str());
    TEST_ASSERT_EQUAL_STRING("MACBOOK PRO", apps::fitHomeHostName("MacBook Pro").c_str());
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    f.display.capture("home-long-name");
    const auto oldCommands = f.display.commands;
    value.hosts.back().name = "MacBook Pro";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    shell.update({});
    TEST_ASSERT_TRUE(oldCommands != f.display.commands);
    TEST_ASSERT_EQUAL_STRING("MacBook Pro", f.config.value().hosts.back().name.c_str());
    f.display.capture("home-compact-name");
}

void test_home_wave_is_bounded_and_pauses_in_settings() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    const auto frames = f.display.frames;
    const auto presentations = f.display.presentations;
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(499));
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    shell.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
    TEST_ASSERT_EQUAL(frames, f.display.frames);
    TEST_ASSERT_TRUE(f.display.texts.empty());
    TEST_ASSERT_FALSE(f.display.rectangles.empty());
    for (const auto& r : f.display.rectangles)
        TEST_ASSERT_TRUE(r.position.y >= 99 && r.position.y + r.height <= 135);
    f.display.capture("home-wave");
    shell.update({settingsChord});
    const auto settingsPresentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(32000));
    TEST_ASSERT_EQUAL(settingsPresentations, f.display.presentations);
    shell.update({enter});
    const auto blePresentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(32000));
    TEST_ASSERT_EQUAL(blePresentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_home_shows_unavailable_telemetry_and_updates_only_battery_region() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.bus, f.display, f.ui);
    shell.update({});
    for (const auto* label : {"--:--", "OFFLINE", "--%"})
        TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), label) !=
                         f.display.texts.end());
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(0), 100);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "100%") !=
                     f.display.texts.end());
    for (const auto& r : f.display.rectangles)
        TEST_ASSERT_TRUE(r.position.y + r.height <= 24);
    // The battery region contains only the clearing rectangle and percentage text.
    TEST_ASSERT_EQUAL_UINT(1, f.display.rectangles.size());
    f.display.capture("home-battery");
    const auto presentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(0), 100);
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
}

void test_settings_dispatch_selection_and_do_not_redraw_an_unchanged_screen() {
    Memory memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    services::HostConfiguration value;
    value.hosts = {{8, "Office laptop", {}}, {9, "Travel laptop", {}}};
    value.hosts[0].bond.bytes[0] = 1;
    value.hosts[1].bond.bytes[0] = 2;
    value.nextHostId = 10;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    Adapter adapter;
    connectivity::BluetoothService bluetooth(adapter);
    services::HostService hosts(bluetooth, config);
    core::ActionBus bus;
    Actions actions;
    bus.registerHandler("host.select", actions);
    Display display;
    apps::HostSettings ui(hosts, bus, display);
    ui.update({});
    TEST_ASSERT_EQUAL(1, display.frames);
    ui.update({});
    TEST_ASSERT_EQUAL(1, display.frames);
    core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
    core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
    ui.update({down, down, down, enter, enter});
    TEST_ASSERT_EQUAL_UINT(1, actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.select", actions.seen[0].id.c_str());
    TEST_ASSERT_EQUAL_INT(9, std::get<std::int32_t>(*actions.seen[0].findParameter("id")));
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_plain_tab_opens_settings_and_leaves_editing_intact);
    RUN_TEST(test_page_transitions_follow_navigation_and_ignore_focus_or_status_refresh);
    RUN_TEST(test_system_button_opens_settings_without_radio_actions_and_preserves_modals);
    RUN_TEST(test_home_name_fits_without_changing_the_saved_label);
    RUN_TEST(test_home_wave_is_bounded_and_pauses_in_settings);
    RUN_TEST(test_home_shows_unavailable_telemetry_and_updates_only_battery_region);
    RUN_TEST(test_host_menu_back_and_incremental_navigation);
    RUN_TEST(test_bluetooth_footer_is_quiet_and_x_has_no_destructive_action);
    RUN_TEST(test_home_and_panel_show_selected_host_and_do_not_navigate_on_state_updates);
    RUN_TEST(test_shell_boots_simple_home_and_opens_settings_before_bluetooth);
    RUN_TEST(test_shell_back_cancels_rename_before_returning_home_and_preserves_host_intent);
    RUN_TEST(test_scrolling_repaints_list_content_and_return_from_rename_invalidates_list_cache);
    RUN_TEST(test_navigation_uses_unmodified_arrow_keys_but_rename_keeps_punctuation);
    RUN_TEST(test_focus_move_only_repaints_changed_rows_without_clearing_screen);
    RUN_TEST(test_settings_dispatch_selection_and_do_not_redraw_an_unchanged_screen);
    return UNITY_END();
}
