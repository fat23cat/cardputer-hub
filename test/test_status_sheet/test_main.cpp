#include "apps/shell/status_sheet.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include "../support/ui_capture.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unity.h>
#include <vector>

using namespace cardputer_hub;
using apps::HomeStatusIndicator;

namespace {

bool sameColor(core::RgbColor left, core::RgbColor right) {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

// Records drawing per overlay region, as the Cardputer adapter receives it.
class OverlayDisplay final : public core::IDisplayAdapter {
  public:
    struct Text {
        core::PixelPosition position;
        std::string value;
        core::TextStyle style;
    };
    void beginOverlayRegion(core::PixelRectangle area) override {
        TEST_ASSERT_FALSE(region.has_value());
        TEST_ASSERT_TRUE(area.width <= 240 && area.height <= 24);
        region = area;
        regions.push_back(area);
    }
    void endOverlayRegion() override {
        TEST_ASSERT_TRUE(region.has_value());
        region.reset();
    }
    void clear(core::RgbColor color) override {
        capture.clear(color);
        ++outsideRegion;
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        expectInside(position, width, height);
        capture.rectangle(position, width, height, color);
        fills.push_back({position, width, height, color});
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        expectInside(position, core::textWidth(value, style.scale), core::systemTextHeight());
        capture.text(position, value, style);
        texts.push_back({position, value, style});
    }
    void expectInside(core::PixelPosition position, std::int32_t width, std::int32_t height) {
        if (!region) {
            ++outsideRegion;
            return;
        }
        TEST_ASSERT_TRUE(position.x >= region->origin.x && position.y >= region->origin.y);
        TEST_ASSERT_TRUE(position.x + width <= region->origin.x + region->width);
        TEST_ASSERT_TRUE(position.y + height <= region->origin.y + region->height);
    }
    const Text* find(const char* value) const {
        for (const auto& text : texts)
            if (text.value == value)
                return &text;
        return nullptr;
    }
    std::optional<core::PixelRectangle> region;
    std::vector<core::PixelRectangle> regions;
    struct Fill {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        core::RgbColor color;
    };
    std::vector<Fill> fills;
    std::vector<Text> texts;
    int outsideRegion = 0;
    test_support::UiCapture capture;
};

apps::StatusSheetInputs connectedInputs() {
    apps::StatusSheetInputs inputs;
    inputs.wifi.configured = true;
    inputs.wifi.enabled = true;
    inputs.wifi.connection = services::WifiConnectionStatus::Connected;
    inputs.host.connectionEnabled = true;
    inputs.host.connection = services::HostConnectionStatus::Ready;
    inputs.host.activeHostName = "MACBOOK PRO";
    inputs.companion = services::CompanionServiceState::Ready;
    inputs.web = services::WebAccessState::Running;
    inputs.webClients = 2;
    inputs.batteryPercent = 72;
    return inputs;
}

void test_model_for_connected_device() {
    const auto model = apps::buildStatusSheetModel(connectedInputs());

    const char* labels[] = {"WI-FI", "BLUETOOTH", "COMPANION", "WEB", "BATTERY"};
    const char* values[] = {"CONNECTED", "MACBOOK PRO", "READY", "2 CLIENTS", "72%"};
    for (std::size_t index = 0; index < model.size(); ++index) {
        TEST_ASSERT_EQUAL_STRING(labels[index], model[index].label);
        TEST_ASSERT_EQUAL_STRING(values[index], model[index].value.c_str());
        TEST_ASSERT_TRUE(sameColor(core::palette::ink, model[index].valueColor));
    }
    TEST_ASSERT_EQUAL_STRING("01", model[0].ordinal);
    TEST_ASSERT_EQUAL_STRING("05", model[4].ordinal);
    for (std::size_t index = 0; index < 4; ++index)
        TEST_ASSERT_TRUE(model[index].indicator == HomeStatusIndicator::FilledLeaf);
    TEST_ASSERT_FALSE(model[4].indicator.has_value());
}

void test_model_without_wifi_bt_companion_web_or_battery() {
    const auto model = apps::buildStatusSheetModel({});

    TEST_ASSERT_EQUAL_STRING("NOT CONFIGURED", model[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("OFF", model[1].value.c_str());
    TEST_ASSERT_EQUAL_STRING("WAITING BT", model[2].value.c_str());
    TEST_ASSERT_EQUAL_STRING("OFF", model[3].value.c_str());
    TEST_ASSERT_EQUAL_STRING("--%", model[4].value.c_str());
    for (std::size_t index = 0; index < 4; ++index)
        TEST_ASSERT_TRUE(model[index].indicator == HomeStatusIndicator::HollowQuiet);
    for (const auto& row : model)
        TEST_ASSERT_FALSE(sameColor(core::palette::vermilion, row.valueColor));
}

void test_model_shows_paused_bluetooth_and_web_clients() {
    auto inputs = connectedInputs();
    inputs.host.connection = services::HostConnectionStatus::Off;
    inputs.host.radioHeld = true;
    inputs.companion = services::CompanionServiceState::Unavailable;
    inputs.webClients = 1;

    const auto model = apps::buildStatusSheetModel(inputs);

    TEST_ASSERT_EQUAL_STRING("PAUSED", model[1].value.c_str());
    TEST_ASSERT_TRUE(model[1].indicator == HomeStatusIndicator::HollowQuiet);
    TEST_ASSERT_EQUAL_STRING("WAITING BT", model[2].value.c_str());
    TEST_ASSERT_EQUAL_STRING("1 CLIENT", model[3].value.c_str());
    TEST_ASSERT_TRUE(model[3].indicator == HomeStatusIndicator::FilledLeaf);

    inputs.webClients = 0;
    TEST_ASSERT_EQUAL_STRING("ON", apps::buildStatusSheetModel(inputs)[3].value.c_str());
    // A saved Off stays OFF while the radio is held.
    inputs.host.connectionEnabled = false;
    TEST_ASSERT_EQUAL_STRING("OFF", apps::buildStatusSheetModel(inputs)[1].value.c_str());
}

void test_model_names_side_to_update() {
    auto inputs = connectedInputs();
    inputs.companion = services::CompanionServiceState::Incompatible;
    inputs.companionAdvice = services::CompanionMismatchAdvice::UpdateFirmware;

    const auto row = apps::buildStatusSheetModel(inputs)[2];

    TEST_ASSERT_EQUAL_STRING("UPDATE FIRMWARE", row.value.c_str());
    TEST_ASSERT_TRUE(row.indicator == HomeStatusIndicator::FilledVermilion);
    TEST_ASSERT_TRUE(sameColor(core::palette::vermilion, row.valueColor));
}

void test_model_states_and_failures() {
    auto inputs = connectedInputs();
    inputs.wifi.connection = services::WifiConnectionStatus::Error;
    inputs.host.connection = services::HostConnectionStatus::Connecting;
    inputs.companion = services::CompanionServiceState::Handshaking;
    inputs.web = services::WebAccessState::Error;
    inputs.batteryPercent = 10;

    const auto model = apps::buildStatusSheetModel(inputs);

    TEST_ASSERT_EQUAL_STRING("ERROR", model[0].value.c_str());
    TEST_ASSERT_TRUE(sameColor(core::palette::vermilion, model[0].valueColor));
    TEST_ASSERT_EQUAL_STRING("CONNECTING", model[1].value.c_str());
    TEST_ASSERT_TRUE(model[1].indicator == HomeStatusIndicator::FilledBlue);
    TEST_ASSERT_EQUAL_STRING("CONNECTING", model[2].value.c_str());
    TEST_ASSERT_TRUE(model[2].indicator == HomeStatusIndicator::FilledBlue);
    TEST_ASSERT_EQUAL_STRING("ERROR", model[3].value.c_str());
    TEST_ASSERT_TRUE(model[3].indicator == HomeStatusIndicator::FilledVermilion);
    TEST_ASSERT_EQUAL_STRING("10%", model[4].value.c_str());
    TEST_ASSERT_TRUE(sameColor(core::palette::vermilion, model[4].valueColor));

    inputs.companion = services::CompanionServiceState::ProtocolError;
    TEST_ASSERT_EQUAL_STRING("ERROR", apps::buildStatusSheetModel(inputs)[2].value.c_str());
    // Unavailable with BT Ready: the Mac has no Companion running.
    inputs.host.connection = services::HostConnectionStatus::Ready;
    inputs.companion = services::CompanionServiceState::Unavailable;
    TEST_ASSERT_EQUAL_STRING("OFFLINE", apps::buildStatusSheetModel(inputs)[2].value.c_str());
    inputs.host.activeHostName.clear();
    TEST_ASSERT_EQUAL_STRING("READY", apps::buildStatusSheetModel(inputs)[1].value.c_str());
}

void test_model_shortens_long_host_names() {
    auto inputs = connectedInputs();
    inputs.host.activeHostName = "Pavel's MacBook Pro 16-inch";

    const auto value = apps::buildStatusSheetModel(inputs)[1].value;

    TEST_ASSERT_EQUAL_UINT(16, value.size());
    TEST_ASSERT_EQUAL_STRING("Pavel's MacBo...", value.c_str());
}

void test_view_draws_every_row_inside_regions() {
    OverlayDisplay display;
    apps::StatusSheetView view;

    view.render(display, apps::buildStatusSheetModel(connectedInputs()));
    display.capture.save("status_sheet_connected");

    TEST_ASSERT_EQUAL_INT(0, display.outsideRegion);
    TEST_ASSERT_EQUAL_UINT(7, display.regions.size());
    TEST_ASSERT_NOT_NULL(display.find("STATUS"));
    const auto* hint = display.find("G0 CLOSE");
    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(sameColor(core::palette::ordinal, hint->style.foreground));
    const auto* value = display.find("CONNECTED");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_INT(28, value->position.y);
    TEST_ASSERT_EQUAL_INT(core::headerStatusRight,
                          value->position.x + core::systemTextWidth("CONNECTED"));
    const auto* battery = display.find("72%");
    TEST_ASSERT_NOT_NULL(battery);
    TEST_ASSERT_EQUAL_INT(108, battery->position.y);
    TEST_ASSERT_NOT_NULL(display.find("05"));
    TEST_ASSERT_NOT_NULL(display.find("BATTERY"));
}

void test_view_places_dot_before_value() {
    OverlayDisplay display;
    apps::StatusSheetView view;

    view.render(display, apps::buildStatusSheetModel(connectedInputs()));

    // The Leaf dot's right edge sits 4 px before the value's first glyph, on
    // the COMPANION row (y 68), whatever the value's width.
    const auto valueX = core::headerStatusRight - core::systemTextWidth("READY");
    const auto* value = display.find("READY");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_INT(valueX, value->position.x);
    std::int32_t left = 240, right = 0, top = 135, bottom = 0;
    for (const auto& fill : display.fills) {
        if (!sameColor(core::palette::leaf, fill.color) || fill.position.y < 68 ||
            fill.position.y >= 78)
            continue;
        left = std::min(left, fill.position.x);
        right = std::max(right, fill.position.x + fill.width);
        top = std::min(top, fill.position.y);
        bottom = std::max(bottom, fill.position.y + fill.height);
    }
    TEST_ASSERT_EQUAL_INT(valueX - 9, left);
    TEST_ASSERT_EQUAL_INT(valueX - 4, right);
    TEST_ASSERT_EQUAL_INT(70, top);
    TEST_ASSERT_EQUAL_INT(75, bottom);
}

void test_view_redraws_only_changed_row() {
    OverlayDisplay display;
    apps::StatusSheetView view;
    auto inputs = connectedInputs();
    inputs.host.connection = services::HostConnectionStatus::Connecting;
    view.render(display, apps::buildStatusSheetModel(inputs));
    display.regions.clear();
    display.texts.clear();

    view.render(display, apps::buildStatusSheetModel(inputs));
    TEST_ASSERT_EQUAL_UINT(0, display.regions.size());

    inputs.host.connection = services::HostConnectionStatus::Ready;
    view.render(display, apps::buildStatusSheetModel(inputs));

    TEST_ASSERT_EQUAL_UINT(1, display.regions.size());
    TEST_ASSERT_EQUAL_INT(43, display.regions[0].origin.y);
    TEST_ASSERT_NOT_NULL(display.find("MACBOOK PRO"));
    TEST_ASSERT_NULL(display.find("CONNECTED"));
}

void test_view_reset_draws_the_whole_sheet_again() {
    OverlayDisplay display;
    apps::StatusSheetView view;
    const auto model = apps::buildStatusSheetModel(connectedInputs());
    view.render(display, model);
    display.regions.clear();

    view.reset();
    view.render(display, model);

    TEST_ASSERT_EQUAL_UINT(7, display.regions.size());
}

void test_problem_sheet_capture() {
    OverlayDisplay display;
    apps::StatusSheetView view;
    auto inputs = connectedInputs();
    inputs.wifi.connection = services::WifiConnectionStatus::Error;
    inputs.host.connection = services::HostConnectionStatus::Connecting;
    inputs.companion = services::CompanionServiceState::Incompatible;
    inputs.companionAdvice = services::CompanionMismatchAdvice::UpdateFirmware;
    inputs.web = services::WebAccessState::Error;
    inputs.batteryPercent = 8;

    view.render(display, apps::buildStatusSheetModel(inputs));
    display.capture.save("status_sheet_problems");

    const auto* advice = display.find("UPDATE FIRMWARE");
    TEST_ASSERT_NOT_NULL(advice);
    TEST_ASSERT_TRUE(sameColor(core::palette::vermilion, advice->style.foreground));
    // The longest value never meets a label.
    TEST_ASSERT_TRUE(advice->position.x - 9 > 26 + core::systemTextWidth("COMPANION"));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_model_for_connected_device);
    RUN_TEST(test_model_without_wifi_bt_companion_web_or_battery);
    RUN_TEST(test_model_shows_paused_bluetooth_and_web_clients);
    RUN_TEST(test_model_names_side_to_update);
    RUN_TEST(test_model_states_and_failures);
    RUN_TEST(test_model_shortens_long_host_names);
    RUN_TEST(test_view_draws_every_row_inside_regions);
    RUN_TEST(test_view_places_dot_before_value);
    RUN_TEST(test_view_redraws_only_changed_row);
    RUN_TEST(test_view_reset_draws_the_whole_sheet_again);
    RUN_TEST(test_problem_sheet_capture);
    return UNITY_END();
}
