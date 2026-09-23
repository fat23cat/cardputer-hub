#include <unity.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "apps/hosts/host_settings.h"
#include "apps/mac_control/mac_control_app.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "connectivity/companion/companion_protocol.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "core/lifecycle/system_runtime.h"
#include "services/audio/audio_service.h"
#include "services/host_control/host_control_service.h"
#include "services/network/network_service.h"

using namespace cardputer_hub;
using namespace cardputer_hub::apps;
using namespace cardputer_hub::connectivity;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;

namespace {

const InputEvent key1{InputEventType::PrintableCharacter, '1', {}, {}};
const InputEvent key2{InputEventType::PrintableCharacter, '2', {}, {}};
const InputEvent left{InputEventType::NamedKey, 0, NamedKey::Left, {}};
const InputEvent right{InputEventType::NamedKey, 0, NamedKey::Right, {}};
const InputEvent enter{InputEventType::NamedKey, 0, NamedKey::Enter, {}};

CompanionPayload encode(const CompanionEnvelope& message) {
    const auto encoded = encodeCompanionMessage(message);
    TEST_ASSERT_TRUE(encoded.has_value());
    CompanionPayload payload{};
    payload.size = encoded->size;
    std::memcpy(payload.bytes.data(), encoded->bytes.data(), encoded->size);
    return payload;
}

class FakeTransport final : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return CompanionSendResult::Sent;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        const auto payload = incoming.front();
        incoming.pop_front();
        return payload;
    }
    CompanionTransportState transportState = CompanionTransportState::Unavailable;
    std::vector<CompanionPayload> sent;
    std::deque<CompanionPayload> incoming;
};

class Memory final : public IStorageAdapter {
  public:
    StorageReadResult read(const StorageAddress&) override {
        return {StorageReadStatus::NotFound, {}};
    }
    StorageWriteStatus write(const StorageAddress&, const StorageBytes&) override {
        return StorageWriteStatus::Stored;
    }
    StorageRemoveStatus remove(const StorageAddress&) override {
        return StorageRemoveStatus::NotFound;
    }
};

class Display final : public IDisplayAdapter {
  public:
    void beginFrame() override { dirty = false; }
    void endFrame() override {
        if (dirty)
            ++presentations;
    }
    void beginTransition(SlideDirection direction) override { transitions.push_back(direction); }
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
    bool hasColor(RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue;
        });
    }
    bool hasRect(PixelPosition position, std::int32_t width, std::int32_t height,
                 RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.position.x == position.x && rectangle.position.y == position.y &&
                   rectangle.width == width && rectangle.height == height &&
                   rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue;
        });
    }
    struct Rectangle {
        PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        RgbColor color;
    };
    std::vector<std::string> texts;
    std::vector<Rectangle> rectangles;
    std::vector<SlideDirection> transitions;
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
};

class AudioAdapter final : public IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
    void end() override {}
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return false; }
    bool play(const AudioClip&) override { return true; }
};

class WifiAdapter final : public IWifiAdapter {
  public:
    WifiAdapterResult initializeStation() override { return {}; }
    WifiAdapterResult connect(const WifiNetworkConfig&) override { return {}; }
    WifiAdapterResult disconnect() override { return {}; }
    WifiAdapterState state() const override { return WifiAdapterState::Disconnected; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return std::nullopt; }
};

class BluetoothAdapter final : public IBluetoothAdapter {
  public:
    BluetoothAdapterResult initialize(const BluetoothDeviceConfig&, std::uint32_t) override {
        return {};
    }
    BluetoothAdapterResult shutdown() override { return {}; }
    BluetoothAdvertisingResult startAdvertising(std::uint32_t,
                                                std::optional<BluetoothBondReference>) override {
        return {};
    }
    BluetoothAdapterResult requestAdvertisingStop() override { return {}; }
    BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle) override { return {}; }
    BluetoothPollResult pollEvent() override { return {}; }
    BluetoothBondQueryResult bondState(BluetoothPeerHandle) override { return {}; }
    BluetoothAdapterResult beginPairing(BluetoothPeerHandle) override { return {}; }
    BluetoothAdapterResult restoreBondSecurity(BluetoothPeerHandle) override { return {}; }
    BluetoothAdapterResult respondToPairing(BluetoothPeerHandle, BluetoothPairingChallengeType,
                                            bool, std::optional<std::uint32_t>) override {
        return {};
    }
    BluetoothBondListResult bonds() override { return {}; }
    BluetoothBondReferenceResult bondReference(BluetoothPeerHandle) override { return {}; }
    BluetoothAdapterResult deleteBond(const BluetoothBondReference&) override { return {}; }
    BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle) override { return {}; }
    BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle) override { return {}; }
    BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle, const HidReport&) override {
        return {};
    }
    BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle) override { return {}; }
    BluetoothCompanionAdapterResult companionReadiness(BluetoothPeerHandle) override { return {}; }
    BluetoothCompanionAdapterResult sendCompanionChunk(BluetoothPeerHandle, const std::uint8_t*,
                                                       std::size_t) override {
        return {};
    }
    bool receiveCompanionChunk(CompanionChunk&) override { return false; }
    bool takeCompanionIncomingOverflow() override { return false; }
};

class RecordingHandler final : public IActionHandler {
  public:
    ActionHandlingResult handle(const Action& action) override {
        ++callCount;
        last = action;
        return result;
    }
    ActionHandlingResult result = ActionHandlingResult::Handled;
    int callCount = 0;
    Action last;
};

BluetoothBondReference bond(unsigned char id) {
    BluetoothBondReference value{};
    value.bytes[0] = id;
    return value;
}

class HostAdapter final : public IBluetoothAdapter {
  public:
    BluetoothAdapterResult initialize(const BluetoothDeviceConfig&,
                                      std::uint32_t generation) override {
        lifecycle = generation;
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult shutdown() override { return BluetoothAdapterResult::Success; }
    BluetoothAdvertisingResult
    startAdvertising(std::uint32_t generation,
                     std::optional<BluetoothBondReference> = std::nullopt) override {
        events.emplace_back(BluetoothEventType::AdvertisingStarted, BluetoothPeerHandle{},
                            BluetoothFailureClass::Fatal, generation);
        return BluetoothAdvertisingResult::Started;
    }
    BluetoothAdapterResult requestAdvertisingStop() override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothPollResult pollEvent() override {
        if (events.empty())
            return BluetoothPollResult::noEvent();
        auto event = events.front();
        events.pop_front();
        return BluetoothPollResult::withEvent(event);
    }
    BluetoothBondQueryResult bondState(BluetoothPeerHandle) override {
        return BluetoothBondQueryResult::Bonded;
    }
    BluetoothAdapterResult beginPairing(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult restoreBondSecurity(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult respondToPairing(BluetoothPeerHandle, BluetoothPairingChallengeType,
                                            bool, std::optional<std::uint32_t>) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothBondListResult bonds() override { return {BluetoothBondListStatus::Success, known}; }
    BluetoothBondReferenceResult bondReference(BluetoothPeerHandle peer) override {
        return {BluetoothBondReferenceStatus::Found, bond(static_cast<unsigned char>(peer.value))};
    }
    BluetoothAdapterResult deleteBond(const BluetoothBondReference&) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle) override {
        return BluetoothAdapterResult::Success;
    }
    BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle) override {
        return BluetoothHidAdapterResult::Ready;
    }
    BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle, const HidReport&) override {
        return BluetoothHidAdapterResult::Sent;
    }
    BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle) override {
        return BluetoothHidAdapterResult::Sent;
    }
    BluetoothCompanionAdapterResult companionReadiness(BluetoothPeerHandle) override {
        return BluetoothCompanionAdapterResult::NotReady;
    }
    BluetoothCompanionAdapterResult sendCompanionChunk(BluetoothPeerHandle, const std::uint8_t*,
                                                       std::size_t) override {
        return BluetoothCompanionAdapterResult::NotReady;
    }
    bool receiveCompanionChunk(CompanionChunk&) override { return false; }
    bool takeCompanionIncomingOverflow() override { return false; }
    std::uint32_t lifecycle = 0;
    std::vector<BluetoothBondReference> known{bond(1)};
    std::deque<BluetoothEvent> events;
};

class HostMemory final : public IStorageAdapter {
  public:
    StorageReadResult read(const StorageAddress&) override {
        return {bytes.empty() ? StorageReadStatus::NotFound : StorageReadStatus::Found, bytes};
    }
    StorageWriteStatus write(const StorageAddress&, const StorageBytes& value) override {
        bytes = value;
        return StorageWriteStatus::Stored;
    }
    StorageRemoveStatus remove(const StorageAddress&) override {
        return StorageRemoveStatus::NotFound;
    }
    StorageBytes bytes;
};

std::vector<MacControlPage> twoPages() {
    auto pages = productionMacControlPages();
    pages.push_back(macControlPageWithBinding({1, "NOTES", "", "com.apple.Notes"}));
    return pages;
}

void test_slot_numbers_and_paging_model() {
    TEST_ASSERT_EQUAL_UINT8(1, macControlSlotForKey('1'));
    TEST_ASSERT_EQUAL_UINT8(6, macControlSlotForKey('6'));
    TEST_ASSERT_EQUAL_UINT8(0, macControlSlotForKey('0'));
    const auto pages = twoPages();
    TEST_ASSERT_NOT_NULL(macControlBindingAt(pages[0], 1));
    TEST_ASSERT_EQUAL_STRING("TELEGRAM", macControlBindingAt(pages[0], 1)->label.data());
    TEST_ASSERT_NULL(macControlBindingAt(pages[0], 2));
    TEST_ASSERT_EQUAL_STRING("NOTES", macControlBindingAt(pages[1], 1)->label.data());
    TEST_ASSERT_TRUE(macControlBindingAt(pages[0], 1)->bundleId ==
                     macControlBindingAt(pages[0], 1)->bundleId);
    TEST_ASSERT_FALSE(macControlCanMovePrevious(0));
    TEST_ASSERT_TRUE(macControlCanMoveNext(0, pages.size()));
    TEST_ASSERT_TRUE(macControlCanMovePrevious(1));
    TEST_ASSERT_FALSE(macControlCanMoveNext(1, pages.size()));
}

void test_production_page_binds_telegram_to_slot_one() {
    const auto pages = productionMacControlPages();
    TEST_ASSERT_EQUAL_UINT32(1, pages.size());
    const auto* binding = macControlBindingAt(pages[0], 1);
    TEST_ASSERT_NOT_NULL(binding);
    TEST_ASSERT_EQUAL_UINT8(1, binding->slot);
    TEST_ASSERT_EQUAL_STRING("TELEGRAM", binding->label.data());
    TEST_ASSERT_TRUE(binding->iconId.empty());
    TEST_ASSERT_EQUAL_STRING(telegramBundleId, std::string(binding->bundleId).c_str());
    for (std::uint8_t slot = 2; slot <= 6; ++slot)
        TEST_ASSERT_NULL(macControlBindingAt(pages[0], slot));
}

struct ControlFixture {
    HostMemory memory;
    Storage storage{memory};
    ConfigurationService config{storage};
    HostAdapter adapter;
    BluetoothService bluetooth{adapter};
    HostService hosts{bluetooth, config};
    FakeTransport transport;
    CapabilityRegistry capabilities;
    CompanionService companion{transport, capabilities};
    HostControlService hostControl{hosts, companion, capabilities};
    ActionBus bus;
    Display display;
    RecordingHandler recorder;
    MacControlApp app;

    ControlFixture() : app(bus, hostControl, display, twoPages()) {
        TEST_ASSERT_TRUE(hosts.start() == HostResult::Success);
        TEST_ASSERT_TRUE(hosts.selectHost(hosts.settings().hosts.front().id) ==
                         HostResult::Success);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(RegistrationResult::Registered),
            static_cast<unsigned>(bus.registerHandler(hostAppActivateActionId, recorder)));
        recorder.result = ActionHandlingResult::Handled;
    }

    CompanionEnvelope lastSent() const {
        const auto decoded =
            decodeCompanionMessage(transport.sent.back().bytes.data(), transport.sent.back().size);
        TEST_ASSERT_TRUE(decoded.has_value());
        return *decoded;
    }

    CompanionEnvelope sentAt(std::size_t index) const {
        const auto decoded =
            decodeCompanionMessage(transport.sent[index].bytes.data(), transport.sent[index].size);
        TEST_ASSERT_TRUE(decoded.has_value());
        return *decoded;
    }

    void completeHandshake() {
        transport.transportState = CompanionTransportState::Ready;
        const auto sentBefore = transport.sent.size();
        const std::uint8_t versions[] = {1};
        transport.incoming.push_back(encode(makeHello(versions, 1)));
        companion.update(std::chrono::milliseconds::zero());
        const auto ack = sentAt(sentBefore);
        const auto capsRequest = sentAt(sentBefore + 1);
        auto capabilitiesResponse =
            makeResponse(ack.session, capsRequest.requestId, CompanionOperation::Capabilities,
                         CompanionStatus::Ok);
        const CompanionCapability ids[] = {CompanionCapability::AppActive,
                                           CompanionCapability::AppActivate,
                                           CompanionCapability::AppActiveEvents};
        TEST_ASSERT_TRUE(setCapabilityList(capabilitiesResponse, ids, 3));
        transport.incoming.push_back(encode(capabilitiesResponse));
        companion.update(std::chrono::milliseconds::zero());
        auto activeResponse = makeResponse(ack.session, lastSent().requestId,
                                           CompanionOperation::AppActive, CompanionStatus::Ok);
        TEST_ASSERT_TRUE(setBundleIdentifier(activeResponse, "dev.zed.Zed"));
        transport.incoming.push_back(encode(activeResponse));
        companion.update(std::chrono::milliseconds::zero());
        while (companion.takeCompletedRequest().has_value()) {
        }
    }

    void useHostControl() {
        bus = ActionBus{};
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(RegistrationResult::Registered),
            static_cast<unsigned>(bus.registerHandler(hostAppActivateActionId, hostControl)));
    }

    void pumpServices(std::chrono::milliseconds elapsed = {}) {
        companion.update(elapsed);
        hostControl.update();
    }

    void deliverActivate(CompanionStatus status) {
        const auto request = lastSent();
        transport.incoming.push_back(encode(makeResponse(companion.session(), request.requestId,
                                                         CompanionOperation::AppActivate, status)));
        companion.update(std::chrono::milliseconds::zero());
    }

    void completeActivate(CompanionStatus status) {
        deliverActivate(status);
        hostControl.update();
    }
};

void test_registry_requires_companion_and_renders_grid() {
    AppRegistry registry;
    CapabilityRegistry capabilities;
    MiniAppRuntime runtime(registry, capabilities);
    ControlFixture f;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(registry.registerApp({macControlAppId,
                                                                        "MAC CONTROL",
                                                                        "mac-control",
                                                                        "mac-control",
                                                                        {companionCapabilityId}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(MiniAppInstanceRegistrationResult::Registered),
        static_cast<unsigned>(runtime.registerInstance(macControlAppId, f.app)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppEligibility::MissingCapability),
                            static_cast<unsigned>(runtime.eligibility(macControlAppId)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::MissingCapability),
                            static_cast<unsigned>(runtime.activate(macControlAppId)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CapabilityRegistrationResult::Registered),
        static_cast<unsigned>(capabilities.registerCapability(companionCapabilityId)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate(macControlAppId)));
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("TELEGRAM"));
    TEST_ASSERT_FALSE(f.display.shows("EMPTY"));
    TEST_ASSERT_FALSE(f.display.shows("MAC CONTROL"));
    TEST_ASSERT_FALSE(f.display.shows("ESC BACK"));
    TEST_ASSERT_TRUE(f.display.hasRect({80, 0}, 1, 135, palette::ink));
    TEST_ASSERT_TRUE(f.display.hasRect({160, 0}, 1, 135, palette::ink));
    TEST_ASSERT_TRUE(f.display.hasRect({0, 67}, 240, 1, palette::ink));
}

void test_wake_consumed_input_does_not_activate_a_slot() {
    ControlFixture f;
    f.useHostControl();
    f.completeHandshake();
    f.app.onActivate();
    const auto sentBeforeWake = f.transport.sent.size();

    // SystemRuntime already consumed the wake press, so this frame has no `1`.
    f.app.update({}, {});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Idle),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT(sentBeforeWake, f.transport.sent.size());

    f.app.update({key1}, {});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT(sentBeforeWake + 1, f.transport.sent.size());
}

void test_direct_keys_and_paging() {
    ControlFixture f;
    f.app.onActivate();
    f.app.update({key1}, {});
    TEST_ASSERT_EQUAL_INT(1, f.recorder.callCount);
    TEST_ASSERT_EQUAL_STRING(hostAppActivateActionId, f.recorder.last.id.c_str());
    TEST_ASSERT_EQUAL_STRING(macControlAppId, f.recorder.last.source.c_str());
    const auto* bundle = f.recorder.last.findParameter(hostAppActivateBundleParameter);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL_STRING(telegramBundleId, std::get<std::string>(*bundle).c_str());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::PendingTakeover),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_EQUAL_UINT8(1, f.app.sourceSlot());
    TEST_ASSERT_EQUAL_INT(0, f.app.sourceRect().origin.x);
    TEST_ASSERT_EQUAL_INT(0, f.app.sourceRect().origin.y);
    TEST_ASSERT_EQUAL_INT(80, f.app.sourceRect().width);
    TEST_ASSERT_EQUAL_INT(67, f.app.sourceRect().height);

    f.app.update({key1, key2}, {});
    TEST_ASSERT_EQUAL_INT(1, f.recorder.callCount);

    ControlFixture g;
    g.app.onActivate();
    g.app.update({key2}, {});
    TEST_ASSERT_EQUAL_INT(0, g.recorder.callCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(g.app.view()));

    g.app.update({right}, {});
    TEST_ASSERT_EQUAL_UINT32(1, g.app.pageIndex());
    TEST_ASSERT_FALSE(g.display.transitions.empty());
    TEST_ASSERT_TRUE(g.display.transitions.back() == SlideDirection::Forward);
    g.app.update({key1}, {});
    TEST_ASSERT_EQUAL_INT(1, g.recorder.callCount);
    TEST_ASSERT_EQUAL_STRING(
        "com.apple.Notes",
        std::get<std::string>(*g.recorder.last.findParameter(hostAppActivateBundleParameter))
            .c_str());
    g.app.onDeactivate();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(g.app.view()));
    TEST_ASSERT_FALSE(g.app.animating());

    ControlFixture h;
    h.app.onActivate();
    h.app.update({left}, {});
    TEST_ASSERT_EQUAL_UINT32(0, h.app.pageIndex());
    TEST_ASSERT_TRUE(h.display.transitions.empty());
    h.app.update({right}, {});
    h.app.update({right}, {});
    TEST_ASSERT_EQUAL_UINT32(1, h.app.pageIndex());
}

void test_pressed_tile_lights_in_place_without_motion() {
    ControlFixture f;
    f.useHostControl();
    f.completeHandshake();
    MacControlApp right(f.bus, f.hostControl, f.display,
                        {macControlPageWithBinding({3, "NOTES", "", "com.apple.Notes"})});
    right.onActivate();
    right.update({{InputEventType::PrintableCharacter, '3', {}, {}}}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::PendingTakeover),
                            static_cast<unsigned>(right.view()));
    TEST_ASSERT_EQUAL_INT(160, right.takeoverRect().origin.x);
    TEST_ASSERT_EQUAL_INT(0, right.takeoverRect().origin.y);
    TEST_ASSERT_EQUAL_INT(80, right.takeoverRect().width);
    TEST_ASSERT_EQUAL_INT(67, right.takeoverRect().height);
    TEST_ASSERT_FALSE(f.display.hasColor(palette::blue));
    TEST_ASSERT_FALSE(f.display.hasColor(palette::leaf));
    TEST_ASSERT_FALSE(f.display.hasColor(palette::vermilion));
    TEST_ASSERT_FALSE(f.display.shows("OPENING"));
    TEST_ASSERT_TRUE(f.display.shows("NOTES"));

    right.update({}, std::chrono::milliseconds(macControlSuccessHoldMs / 2));
    TEST_ASSERT_EQUAL_INT(160, right.takeoverRect().origin.x);
    TEST_ASSERT_EQUAL_INT(0, right.takeoverRect().origin.y);
    TEST_ASSERT_EQUAL_INT(80, right.takeoverRect().width);
    TEST_ASSERT_EQUAL_INT(67, right.takeoverRect().height);
    TEST_ASSERT_FALSE(f.display.shows("OPENING"));
}

void test_takeover_animation_success_and_failure() {
    ControlFixture f;
    f.useHostControl();
    f.completeHandshake();
    f.app.onActivate();
    f.app.update({key1}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::PendingTakeover),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_FALSE(f.display.hasColor(palette::blue));
    TEST_ASSERT_EQUAL_INT(80, f.app.takeoverRect().width);
    TEST_ASSERT_EQUAL_INT(67, f.app.takeoverRect().height);
    TEST_ASSERT_FALSE(f.display.shows("OPENING"));
    TEST_ASSERT_TRUE(f.display.shows("TELEGRAM"));

    f.app.update({right, key2}, {});
    TEST_ASSERT_EQUAL_UINT32(0, f.app.pageIndex());

    f.completeActivate(CompanionStatus::Ok);
    f.app.update({}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::SuccessTakeover),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_TRUE(f.display.hasColor(palette::leaf));
    TEST_ASSERT_FALSE(f.display.shows("OPENED"));
    TEST_ASSERT_TRUE(f.display.shows("TELEGRAM"));
    f.app.update({}, std::chrono::milliseconds(macControlSuccessHoldMs - 1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::SuccessTakeover),
                            static_cast<unsigned>(f.app.view()));
    f.app.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_EQUAL_UINT8(0, f.app.sourceSlot());
    TEST_ASSERT_FALSE(f.app.animating());
    const auto presentations = f.display.presentations;
    f.display.beginFrame();
    f.app.update({}, std::chrono::milliseconds(20));
    f.display.endFrame();
    TEST_ASSERT_EQUAL_INT(presentations, f.display.presentations);

    f.app.update({key1}, {});
    f.completeActivate(CompanionStatus::NotFound);
    f.app.update({}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::FailureTakeover),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_TRUE(f.display.hasColor(palette::vermilion));
    TEST_ASSERT_FALSE(f.display.shows("NOT FOUND"));
    TEST_ASSERT_FALSE(f.display.shows("FAILED"));
    TEST_ASSERT_TRUE(f.display.shows("TELEGRAM"));
    f.app.update({}, std::chrono::milliseconds(macControlFailureHoldMs - 1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::FailureTakeover),
                            static_cast<unsigned>(f.app.view()));
    f.app.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.app.view()));
    TEST_ASSERT_EQUAL_INT(0, f.app.sourceRect().origin.x);
}

void test_mac_control_observes_host_control_without_driving_lifecycle() {
    ControlFixture f;
    f.useHostControl();
    f.completeHandshake();
    f.app.onActivate();
    f.app.update({key1}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::PendingTakeover),
                            static_cast<unsigned>(f.app.view()));

    f.deliverActivate(CompanionStatus::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    f.app.update({}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::PendingTakeover),
                            static_cast<unsigned>(f.app.view()));

    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Succeeded),
                            static_cast<unsigned>(f.hostControl.status().state));
    f.app.update({}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::SuccessTakeover),
                            static_cast<unsigned>(f.app.view()));
}

void test_pending_takeover_capability_loss_closes_and_does_not_replay() {
    ControlFixture f;
    AppRegistry registry;
    MiniAppRuntime runtime(registry, f.capabilities);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(registry.registerApp({macControlAppId,
                                                                        "MAC CONTROL",
                                                                        "mac-control",
                                                                        "mac-control",
                                                                        {companionCapabilityId}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(MiniAppInstanceRegistrationResult::Registered),
        static_cast<unsigned>(runtime.registerInstance(macControlAppId, f.app)));
    f.useHostControl();
    f.completeHandshake();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate(macControlAppId)));
    f.app.update({key1}, {});
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Pending),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_TRUE(f.app.animating());
    const auto sentAfterCommand = f.transport.sent.size();

    f.transport.transportState = CompanionTransportState::Unavailable;
    f.pumpServices();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlFailure::Unavailable),
                            static_cast<unsigned>(f.hostControl.status().failure));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(MiniAppUpdateResult::DeactivatedMissingCapability),
        static_cast<unsigned>(runtime.update({}, {})));
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.app.view()));

    f.completeHandshake();
    f.hostControl.update();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(HostControlCommandState::Failed),
                            static_cast<unsigned>(f.hostControl.status().state));
    bool replayed = false;
    for (std::size_t index = sentAfterCommand; index < f.transport.sent.size(); ++index) {
        if (f.sentAt(index).operation == CompanionOperation::AppActivate &&
            f.sentAt(index).kind == CompanionKind::Request)
            replayed = true;
    }
    TEST_ASSERT_FALSE(replayed);
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppEligibility::Eligible),
                            static_cast<unsigned>(runtime.eligibility(macControlAppId)));
}

void test_runtime_capability_loss_closes_mac_control() {
    AppRegistry registry;
    CapabilityRegistry capabilities;
    MiniAppRuntime runtime(registry, capabilities);
    ControlFixture f;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(registry.registerApp({macControlAppId,
                                                                        "MAC CONTROL",
                                                                        "mac-control",
                                                                        "mac-control",
                                                                        {companionCapabilityId}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(MiniAppInstanceRegistrationResult::Registered),
        static_cast<unsigned>(runtime.registerInstance(macControlAppId, f.app)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CapabilityRegistrationResult::Registered),
        static_cast<unsigned>(capabilities.registerCapability(companionCapabilityId)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate(macControlAppId)));
    TEST_ASSERT_TRUE(runtime.activeAppId() == macControlAppId);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CapabilityRemovalResult::Removed),
        static_cast<unsigned>(capabilities.removeCapability(companionCapabilityId)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(MiniAppUpdateResult::DeactivatedMissingCapability),
        static_cast<unsigned>(runtime.update({}, {})));
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.app.view()));
}

struct ShellFixture {
    Memory memory;
    Storage storage{memory};
    ConfigurationService config{storage};
    AudioAdapter audioAdapter;
    AudioService audio{config, audioAdapter};
    BluetoothAdapter bluetoothAdapter;
    BluetoothService bluetooth{bluetoothAdapter};
    HostService hosts{bluetooth, config};
    WifiAdapter wifiAdapter;
    WiFiService wifi{wifiAdapter};
    NetworkService network{wifi, config};
    ActionBus bus;
    Display display;
    HostSettings hostSettings{hosts, bus, display};
    WiFiSettings wifiSettings{network, bus, display};
    AppRegistry appRegistry;
    CapabilityRegistry capabilities;
    MiniAppRuntime miniApps{appRegistry, capabilities};
    FakeTransport transport;
    CompanionService companion{transport, capabilities};
    HostControlService hostControl{hosts, companion, capabilities};
    MacControlApp macControl{bus, hostControl, display};

    ShellFixture() {
        TEST_ASSERT_TRUE(config.load() == ConfigurationResult::Success);
        TEST_ASSERT_TRUE(audio.start() == AudioResult::Success);
        TEST_ASSERT_TRUE(bus.registerHandler("audio.volume.step", audio) ==
                         RegistrationResult::Registered);
        TEST_ASSERT_TRUE(appRegistry.registerApp({macControlAppId,
                                                  "MAC CONTROL",
                                                  "mac-control",
                                                  "mac-control",
                                                  {companionCapabilityId}}) ==
                         AppRegistrationResult::Registered);
        TEST_ASSERT_TRUE(miniApps.registerInstance(macControlAppId, macControl) ==
                         MiniAppInstanceRegistrationResult::Registered);
    }

    ApplicationShell makeShell() {
        return ApplicationShell(hosts, network, bus, display, hostSettings, wifiSettings, audio,
                                miniApps, capabilities);
    }
};

class RuntimePlatform final : public IPlatformAdapter {
  public:
    void begin() override {}
    void update() override {}
};

class RuntimeKeyboard final : public IKeyboardAdapter {
  public:
    KeyboardPollResult poll(InputEvents& events) override {
        events = next_;
        const KeyboardPollResult result{press_};
        next_.clear();
        press_ = false;
        return result;
    }

    void press(InputEvents events) {
        next_ = std::move(events);
        press_ = true;
    }

  private:
    InputEvents next_;
    bool press_ = false;
};

class RuntimeBacklight final : public IBacklightAdapter {
  public:
    std::uint8_t level() const override { return level_; }
    void setLevel(std::uint8_t level) override { level_ = level; }

  private:
    std::uint8_t level_ = 128;
};

class RuntimeLogSink final : public ILogSink {
  public:
    void write(const LogRecord&) override {}
};

// Drives the whole production path: keyboard -> SystemRuntime ->
// DisplayPowerController -> ApplicationShell -> Mini App -> ActionBus.
struct WakePathFixture {
    ShellFixture shell;
    RecordingHandler recorder;
    RuntimePlatform platform;
    RuntimeKeyboard keyboard;
    RuntimeBacklight backlight;
    RuntimeLogSink logSink;
    Logger logger{logSink, LogLevel::Info};
    const BuildInfo buildInfo{"Test Hub", "1.0.0", "test", "test"};
    DisplayPowerController displayPower{backlight};
    SystemRuntime runtime;
    ApplicationShell applicationShell;

    WakePathFixture()
        : runtime(platform, keyboard, shell.display, displayPower, logger, buildInfo),
          applicationShell(shell.makeShell()) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(CapabilityRegistrationResult::Registered),
            static_cast<unsigned>(shell.capabilities.registerCapability(companionCapabilityId)));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(RegistrationResult::Registered),
            static_cast<unsigned>(shell.bus.registerHandler(hostAppActivateActionId, recorder)));
        runtime.start();
        step(std::chrono::milliseconds(2000));
    }

    void step(std::chrono::milliseconds elapsed) {
        const auto& input = runtime.update(elapsed);
        applicationShell.update(input, std::chrono::milliseconds(0), std::nullopt,
                                runtime.displayOff());
    }

    void pressAndStep(InputEvents events) {
        keyboard.press(std::move(events));
        step(std::chrono::milliseconds(20));
    }

    void idleUntilOff() {
        for (const auto duration :
             {DisplayPowerController::idleThreshold, DisplayPowerController::dimRampDuration,
              DisplayPowerController::dimHoldDuration, DisplayPowerController::offRampDuration})
            step(duration);
        TEST_ASSERT_TRUE(runtime.displayOff());
    }
};

void test_off_mac_control_key_wakes_without_dispatching_host_action() {
    WakePathFixture f;
    f.pressAndStep({enter});
    f.pressAndStep({enter});
    TEST_ASSERT_TRUE(f.shell.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.shell.miniApps.activeAppId() == macControlAppId);
    f.idleUntilOff();

    f.pressAndStep({key1});

    TEST_ASSERT_EQUAL_INT(0, f.recorder.callCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Waking),
                            static_cast<unsigned>(f.displayPower.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MacControlView::Grid),
                            static_cast<unsigned>(f.shell.macControl.view()));
    TEST_ASSERT_TRUE(f.shell.miniApps.hasActiveApp());

    f.step(DisplayPowerController::wakeRampDuration);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(DisplayPowerState::Awake),
                            static_cast<unsigned>(f.displayPower.state()));

    f.pressAndStep({key1});

    TEST_ASSERT_EQUAL_INT(1, f.recorder.callCount);
    TEST_ASSERT_EQUAL_STRING(hostAppActivateActionId, f.recorder.last.id.c_str());
    TEST_ASSERT_EQUAL_STRING(macControlAppId, f.recorder.last.source.c_str());
}

void test_shell_restores_launcher_when_companion_is_lost() {
    ShellFixture f;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CapabilityRegistrationResult::Registered),
        static_cast<unsigned>(f.capabilities.registerCapability(companionCapabilityId)));
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.miniApps.activeAppId() == macControlAppId);
    TEST_ASSERT_TRUE(f.display.shows("TELEGRAM"));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CapabilityRemovalResult::Removed),
        static_cast<unsigned>(f.capabilities.removeCapability(companionCapabilityId)));
    shell.update({}, std::chrono::milliseconds(16));
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_slot_numbers_and_paging_model);
    RUN_TEST(test_production_page_binds_telegram_to_slot_one);
    RUN_TEST(test_registry_requires_companion_and_renders_grid);
    RUN_TEST(test_direct_keys_and_paging);
    RUN_TEST(test_wake_consumed_input_does_not_activate_a_slot);
    RUN_TEST(test_pressed_tile_lights_in_place_without_motion);
    RUN_TEST(test_takeover_animation_success_and_failure);
    RUN_TEST(test_mac_control_observes_host_control_without_driving_lifecycle);
    RUN_TEST(test_pending_takeover_capability_loss_closes_and_does_not_replay);
    RUN_TEST(test_runtime_capability_loss_closes_mac_control);
    RUN_TEST(test_shell_restores_launcher_when_companion_is_lost);
    RUN_TEST(test_off_mac_control_key_wakes_without_dispatching_host_action);
    return UNITY_END();
}
