#include "apps/led_gallery/led_gallery_app.h"
#include "apps/led_gallery/led_gallery_engine.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unity.h>
#include <vector>
using namespace cardputer_hub::apps;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace std::chrono_literals;

void test_registry_and_motion() {
    TEST_ASSERT_EQUAL(10, ledGalleryEffects.size());
    constexpr const char* names[] = {"PLASMA", "LAVA",          "KALEIDOSCOPE", "AURORA",
                                     "WARP",   "COMETS",        "FIREFLIES",    "VORTEX",
                                     "RIPPLE", "PARTICLE STORM"};
    for (unsigned i = 0; i < ledGalleryEffects.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<unsigned>(ledGalleryEffects[i].id));
        TEST_ASSERT_EQUAL_STRING(names[i], ledGalleryEffects[i].name);
    }
    for (unsigned i = 0; i < ledGalleryEffects.size(); ++i) {
        LedGalleryEngine e(123);
        e.reset(ledGalleryEffects[i].id, 123);
        auto before = e.frame();
        e.advance(2000ms);
        e.advance(2000ms);
        auto after = e.frame();
        bool changed = false;
        for (unsigned p = 0; p < 64; ++p)
            changed |= before.pixels[p].red != after.pixels[p].red ||
                       before.pixels[p].green != after.pixels[p].green ||
                       before.pixels[p].blue != after.pixels[p].blue;
        TEST_ASSERT_TRUE_MESSAGE(changed, ledGalleryEffects[i].name);
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<unsigned>(e.effect()));
    }
}
void test_bounded_interactions() {
    LedGalleryEngine e(12);
    e.reset(LedGalleryEffect::Ripple, 12);
    for (int i = 0; i < 20; ++i)
        e.interact(' ');
    TEST_ASSERT_EQUAL(4, e.activeRipples());
    e.reset(LedGalleryEffect::ParticleStorm, 12);
    for (int i = 0; i < 20; ++i)
        e.interact('a');
    TEST_ASSERT_TRUE(e.activeParticles() <= 24);
    e.advance(24h);
    TEST_ASSERT_TRUE(e.activeParticles() <= 24);
}
class FakeLed : public ILEDAdapter {
  public:
    void writeFrame(const LedHardwareFrame& frame) override {
        last = frame;
        ++writes;
    }
    LedHardwareFrame last{};
    int writes = 0;
};
class FakeDisplay : public IDisplayAdapter {
  public:
    void clear(RgbColor) override {
        ++clears;
        texts.clear();
        textPositions.clear();
        arrowPositions.clear();
        arrows = 0;
    }
    void fillRectangle(PixelPosition position, std::int32_t, std::int32_t, RgbColor) override {
        ++arrows;
        arrowPositions.push_back(position);
    }
    void drawText(PixelPosition position, const char* value, TextStyle) override {
        texts.emplace_back(value);
        textPositions.push_back(position);
    }
    bool shows(const char* text) const {
        for (const auto& item : texts)
            if (item == text)
                return true;
        return false;
    }
    PixelPosition positionOf(const char* text) const {
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (texts[i] == text)
                return textPositions[i];
        return {-1, -1};
    }
    int clears = 0;
    int arrows = 0;
    std::vector<std::string> texts;
    std::vector<PixelPosition> textPositions;
    std::vector<PixelPosition> arrowPositions;
};
InputEvent key(char c) { return {InputEventType::PrintableCharacter, c, {}, {}}; }
InputEvent arrow(NamedKey k) { return {InputEventType::NamedKey, 0, k, {}}; }
void test_navigation_claim_and_no_auto_switch() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    auto background =
        indicator.acquire(pomodoroIndicatorOwner, IndicatorPriority::BackgroundApplication);
    IndicatorFrame bg{};
    bg.pixels.fill({255, 0, 0});
    background.setFrame(bg);
    LedGalleryApp app(indicator, display, 123);
    app.onActivate();
    app.update({}, 60ms);
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(ledGalleryIndicatorOwner, indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(ledGalleryBrightnessPercent, indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(1, display.clears);
    app.update({arrow(NamedKey::Left)}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<unsigned>(app.currentEffect()));
    app.update({key('/')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(app.currentEffect()));
    app.update({key(',')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<unsigned>(app.currentEffect()));
    app.update({key('0')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<unsigned>(app.currentEffect()));
    InputEvent question = key('?');
    question.modifiers.shift = true;
    indicator.update();
    const auto stormBefore = indicator.resolved().frame;
    app.update({question}, 0ms);
    indicator.update();
    const auto stormAfter = indicator.resolved().frame;
    bool burstChangedFrame = false;
    for (std::size_t i = 0; i < 64; ++i)
        burstChangedFrame |= stormBefore.pixels[i].red != stormAfter.pixels[i].red ||
                             stormBefore.pixels[i].green != stormAfter.pixels[i].green ||
                             stormBefore.pixels[i].blue != stormAfter.pixels[i].blue;
    TEST_ASSERT_TRUE(burstChangedFrame);
    app.update({key('4')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<unsigned>(app.currentEffect()));
    app.update({}, 24h);
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<unsigned>(app.currentEffect()));
    auto notification = indicator.acquire("notice", IndicatorPriority::Notification);
    IndicatorFrame notice{};
    notice.pixels.fill({0, 0, 255});
    notification.setFrame(notice);
    indicator.update();
    TEST_ASSERT_EQUAL_STRING("notice", indicator.resolved().owner.c_str());
    notification.release();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(ledGalleryIndicatorOwner, indicator.resolved().owner.c_str());
    app.onDeactivate();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(pomodoroIndicatorOwner, indicator.resolved().owner.c_str());
    app.onActivate();
    app.update({}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<unsigned>(app.currentEffect()));
    app.onDeactivate();
    indicator.update();
    TEST_ASSERT_EQUAL_STRING(pomodoroIndicatorOwner, indicator.resolved().owner.c_str());
}
void test_deterministic_frames_and_space() {
    LedGalleryEngine a(5), b(5);
    a.reset(LedGalleryEffect::Plasma, 5);
    b.reset(LedGalleryEffect::Plasma, 5);
    a.advance(200ms);
    b.advance(200ms);
    const auto af = a.frame(), bf = b.frame();
    for (std::size_t i = 0; i < 64; ++i) {
        TEST_ASSERT_EQUAL_UINT8(af.pixels[i].red, bf.pixels[i].red);
        TEST_ASSERT_EQUAL_UINT8(af.pixels[i].green, bf.pixels[i].green);
        TEST_ASSERT_EQUAL_UINT8(af.pixels[i].blue, bf.pixels[i].blue);
    }
    a.interact(' ');
    TEST_ASSERT_EQUAL(0, a.activeRipples());
    a.reset(LedGalleryEffect::Ripple, 5);
    a.interact(' ');
    TEST_ASSERT_EQUAL(1, a.activeRipples());
    a.reset(LedGalleryEffect::ParticleStorm, 5);
    const auto before = a.activeParticles();
    a.interact(' ');
    TEST_ASSERT_TRUE(a.activeParticles() > before);
    a.interact('?');
    TEST_ASSERT_TRUE(a.activeParticles() > before);
}
bool sameFrame(const IndicatorFrame& a, const IndicatorFrame& b) {
    for (std::size_t i = 0; i < 64; ++i)
        if (a.pixels[i].red != b.pixels[i].red || a.pixels[i].green != b.pixels[i].green ||
            a.pixels[i].blue != b.pixels[i].blue)
            return false;
    return true;
}
void test_local_glow_bounds_and_determinism() {
    IndicatorFrame center{}, copy{};
    detail::glow(center, 3.5f, 3.5f, {255, 120, 80}, 1.f);
    detail::glow(copy, 3.5f, 3.5f, {255, 120, 80}, 1.f);
    TEST_ASSERT_TRUE(sameFrame(center, copy));
    TEST_ASSERT_TRUE(center.pixels[3 * 8 + 3].red > 0);
    TEST_ASSERT_EQUAL_UINT8(0, center.pixels[0].red);
    IndicatorFrame edge{};
    detail::glow(edge, 0.f, 0.f, {255, 120, 80}, 1.f);
    TEST_ASSERT_TRUE(edge.pixels[0].red > 0);
    TEST_ASSERT_EQUAL_UINT8(0, edge.pixels[7 * 8 + 7].red);
}
void test_local_glow_matches_full_gaussian() {
    constexpr float positions[][2] = {{3.5f, 3.5f}, {0.f, 0.f}, {7.f, 7.f},
                                      {-1.f, 4.f},  {8.f, 2.f}, {2.35f, 5.8f}};
    constexpr float powers[] = {1.f, .35f, .02f, .01f};
    for (const auto& position : positions)
        for (float power : powers) {
            IndicatorFrame actual{}, reference{};
            detail::glow(actual, position[0], position[1], {255, 120, 80}, power);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const float dx = x - position[0], dy = y - position[1];
                    const float weight = power * std::exp(-(dx * dx + dy * dy) * 1.25f);
                    if (weight <= .015f)
                        continue;
                    reference.pixels[y * 8 + x] = {static_cast<std::uint8_t>(255.f * weight),
                                                   static_cast<std::uint8_t>(120.f * weight),
                                                   static_cast<std::uint8_t>(80.f * weight)};
                }
            TEST_ASSERT_TRUE(sameFrame(actual, reference));
        }
}
void test_output_cadence_retains_remainder_without_bursts() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 123);
    app.onActivate();
    indicator.update();
    const auto initial = indicator.resolved().frame;
    app.update({}, 20ms);
    indicator.update();
    TEST_ASSERT_TRUE(sameFrame(initial, indicator.resolved().frame));
    app.update({}, 20ms);
    indicator.update();
    TEST_ASSERT_TRUE(sameFrame(initial, indicator.resolved().frame));
    app.update({}, 20ms);
    indicator.update();
    const auto at60 = indicator.resolved().frame;
    TEST_ASSERT_FALSE(sameFrame(initial, at60));
    app.update({}, 20ms);
    indicator.update();
    TEST_ASSERT_TRUE(sameFrame(at60, indicator.resolved().frame));
    app.update({}, 20ms);
    indicator.update();
    const auto at100 = indicator.resolved().frame;
    TEST_ASSERT_FALSE(sameFrame(at60, at100));
    const int writesBefore = led.writes;
    app.update({}, 24h);
    indicator.update();
    TEST_ASSERT_TRUE(led.writes <= writesBefore + 1);
    const auto afterDelay = indicator.resolved().frame;
    app.update({}, 0ms);
    indicator.update();
    TEST_ASSERT_TRUE(sameFrame(afterDelay, indicator.resolved().frame));
    app.update({key('2')}, 0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<unsigned>(app.currentEffect()));
    TEST_ASSERT_FALSE(sameFrame(afterDelay, indicator.resolved().frame));
    app.onDeactivate();
}
void test_sixty_plus_forty_milliseconds_publishes_twice() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 123);
    app.onActivate();
    indicator.update();
    app.update({}, 60ms);
    indicator.update();
    const auto at60 = indicator.resolved().frame;
    app.update({}, 40ms);
    indicator.update();
    TEST_ASSERT_FALSE(sameFrame(at60, indicator.resolved().frame));
    app.onDeactivate();
}
void test_kaleidoscope_motion_symmetry_and_determinism() {
    LedGalleryEngine first(77), second(77);
    first.reset(LedGalleryEffect::Kaleidoscope, 77);
    second.reset(LedGalleryEffect::Kaleidoscope, 77);
    const auto initial = first.frame();
    first.advance(137ms);
    second.advance(137ms);
    const auto later = first.frame();
    TEST_ASSERT_FALSE(sameFrame(initial, later));
    bool geometryChanged = false;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto a = initial.pixels[i], b = later.pixels[i];
        const auto brightness = [](RgbColor c) {
            return std::max(c.red, std::max(c.green, c.blue));
        };
        geometryChanged |= brightness(a) != brightness(b);
    }
    TEST_ASSERT_TRUE(geometryChanged);
    TEST_ASSERT_TRUE(sameFrame(later, second.frame()));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const auto a = later.pixels[y * 8 + x];
            const auto b = later.pixels[(7 - y) * 8 + 7 - x];
            TEST_ASSERT_INT_WITHIN(2, a.red, b.red);
            TEST_ASSERT_INT_WITHIN(2, a.green, b.green);
            TEST_ASSERT_INT_WITHIN(2, a.blue, b.blue);
        }
    first.advance(24h);
    second.advance(24h);
    TEST_ASSERT_TRUE(sameFrame(first.frame(), second.frame()));
    TEST_ASSERT_EQUAL(64, first.frame().pixels.size());
}
void test_contextual_lcd_hints_and_no_r_reset() {
    FakeLed led, referenceLed;
    FakeDisplay display, referenceDisplay;
    IndicatorService indicator(led), referenceIndicator(referenceLed);
    LedGalleryApp app(indicator, display, 17), reference(referenceIndicator, referenceDisplay, 17);
    app.onActivate();
    reference.onActivate();
    app.update({}, 80ms);
    reference.update({}, 80ms);
    TEST_ASSERT_TRUE(display.shows("NEXT EFFECT"));
    TEST_ASSERT_TRUE(display.shows("1-0 DIRECT"));
    TEST_ASSERT_TRUE(display.arrows > 0);
    TEST_ASSERT_TRUE(display.positionOf("NEXT EFFECT").y >= 105);
    TEST_ASSERT_TRUE(display.positionOf("1-0 DIRECT").y >= 119);
    TEST_ASSERT_TRUE(display.positionOf("1-0 DIRECT").y + 8 <= 135);
    for (const auto& arrowPosition : display.arrowPositions)
        TEST_ASSERT_TRUE(arrowPosition.y >= 105);
    TEST_ASSERT_FALSE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_FALSE(display.shows("SPACE BURST"));
    TEST_ASSERT_FALSE(display.shows("R RESET"));
    app.update({key('9')}, 0ms);
    TEST_ASSERT_TRUE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_FALSE(display.shows("SPACE BURST"));
    TEST_ASSERT_EQUAL_INT32(display.positionOf("1-0 DIRECT").y,
                            display.positionOf("SPACE RIPPLE").y);
    TEST_ASSERT_TRUE(display.positionOf("SPACE RIPPLE").x >
                     display.positionOf("1-0 DIRECT").x + 10 * 6);
    app.update({key('0')}, 0ms);
    TEST_ASSERT_TRUE(display.shows("SPACE BURST"));
    TEST_ASSERT_FALSE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_EQUAL_INT32(display.positionOf("1-0 DIRECT").y,
                            display.positionOf("SPACE BURST").y);
    app.update({key('3')}, 0ms);
    reference.update({key('3')}, 0ms);
    indicator.update();
    referenceIndicator.update();
    const auto before = indicator.resolved().frame;
    app.update({key('r'), key('R')}, 0ms);
    indicator.update();
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<unsigned>(app.currentEffect()));
    TEST_ASSERT_TRUE(sameFrame(before, indicator.resolved().frame));
    app.update({}, 250ms);
    reference.update({}, 250ms);
    indicator.update();
    referenceIndicator.update();
    TEST_ASSERT_TRUE(sameFrame(indicator.resolved().frame, referenceIndicator.resolved().frame));
    app.onDeactivate();
    reference.onDeactivate();
}
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_and_motion);
    RUN_TEST(test_bounded_interactions);
    RUN_TEST(test_navigation_claim_and_no_auto_switch);
    RUN_TEST(test_deterministic_frames_and_space);
    RUN_TEST(test_local_glow_bounds_and_determinism);
    RUN_TEST(test_local_glow_matches_full_gaussian);
    RUN_TEST(test_output_cadence_retains_remainder_without_bursts);
    RUN_TEST(test_sixty_plus_forty_milliseconds_publishes_twice);
    RUN_TEST(test_kaleidoscope_motion_symmetry_and_determinism);
    RUN_TEST(test_contextual_lcd_hints_and_no_r_reset);
    return UNITY_END();
}
