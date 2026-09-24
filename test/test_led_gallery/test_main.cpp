#include "apps/led_gallery/led_gallery_app.h"
#include "apps/led_gallery/led_gallery_engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unity.h>
#include <utility>
#include <vector>
using namespace cardputer_hub::apps;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace std::chrono_literals;

namespace cardputer_hub::apps {
struct LedGalleryEngineTestAccess {
    static auto& cells(LedGalleryEngine& e) { return e.cells_; }
    static auto& reagent(LedGalleryEngine& e) { return e.fieldB_; }
    static auto& particles(LedGalleryEngine& e) { return e.particles_; }
    static float feed(const LedGalleryEngine& e) { return e.feed_; }
    static float kill(const LedGalleryEngine& e) { return e.kill_; }
    static int wind(const LedGalleryEngine& e) { return e.wind_; }
    static int heat(const LedGalleryEngine& e) { return e.heat_; }
    static int targetX(const LedGalleryEngine& e) { return e.targetX_; }
    static int targetY(const LedGalleryEngine& e) { return e.targetY_; }
    static int gravity(const LedGalleryEngine& e) { return e.gravity_; }
    static void setGeneration(LedGalleryEngine& e, std::uint16_t generation) {
        e.generation_ = generation;
    }
    static int ants(const LedGalleryEngine& e) { return e.antCount_; }
    static auto& antState(LedGalleryEngine& e) { return e.ants_; }
    static int rule(const LedGalleryEngine& e) { return e.ruleIndex_; }
    static int speed(const LedGalleryEngine& e) { return e.ruleSpeed_; }
    static int rotation(const LedGalleryEngine& e) { return e.pieceRotation_; }
    static int pieceX(const LedGalleryEngine& e) { return e.pieceX_; }
    static int pieceY(const LedGalleryEngine& e) { return e.pieceY_; }
    static int strike(const LedGalleryEngine& e) { return e.pulse_; }
    static std::uint8_t row(const LedGalleryEngine& e) { return e.ruleRow_; }
    static void setRow(LedGalleryEngine& e, std::uint8_t row) { e.ruleRow_ = row; }
    static float pulse(const LedGalleryEngine& e) { return e.pulseRemainingSeconds_; }
};
} // namespace cardputer_hub::apps

void test_registry_and_motion() {
    TEST_ASSERT_EQUAL(20, ledGalleryEffects.size());
    constexpr const char* names[] = {
        "PLASMA",        "LAVA",           "KALEIDOSCOPE", "AURORA",
        "WARP",          "COMETS",         "FIREFLIES",    "VORTEX",
        "RIPPLE",        "PARTICLE STORM", "GAME OF LIFE", "REACTION DIFFUSION",
        "FIRE",          "GRAVITY WELL",   "SWARM",        "FALLING SAND",
        "LANGTON'S ANT", "TETRIS DREAM",   "RULE MACHINE", "ELECTRIC STORM"};
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
InputEvent fnDigit(int digit) {
    auto event =
        arrow(static_cast<NamedKey>(static_cast<int>(NamedKey::F1) + (digit == 0 ? 9 : digit - 1)));
    event.modifiers.fn = true;
    return event;
}
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
    TEST_ASSERT_EQUAL_UINT8(defaultIndicatorBrightnessPercent,
                            indicator.resolved().brightnessPercent);
    TEST_ASSERT_EQUAL(1, display.clears);
    app.update({arrow(NamedKey::Left)}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(19, static_cast<unsigned>(app.currentEffect()));
    app.update({key('/')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(app.currentEffect()));
    app.update({key(',')}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(19, static_cast<unsigned>(app.currentEffect()));
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
    TEST_ASSERT_TRUE(display.shows("01/20"));
    TEST_ASSERT_TRUE(display.shows("< > EFFECT"));
    TEST_ASSERT_TRUE(display.shows("1-0 / FN+1-0"));
    TEST_ASSERT_EQUAL(121, display.positionOf("< > EFFECT").y);
    TEST_ASSERT_EQUAL(121, display.positionOf("1-0 / FN+1-0").y);
    TEST_ASSERT_FALSE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_FALSE(display.shows("SPACE BURST"));
    TEST_ASSERT_FALSE(display.shows("R RESET"));
    app.update({key('9')}, 0ms);
    TEST_ASSERT_TRUE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_FALSE(display.shows("SPACE BURST"));
    TEST_ASSERT_EQUAL(107, display.positionOf("SPACE RIPPLE").y);
    TEST_ASSERT_TRUE(display.positionOf("SPACE RIPPLE").y < display.positionOf("1-0 / FN+1-0").y);
    app.update({key('0')}, 0ms);
    TEST_ASSERT_TRUE(display.shows("KEY BURST  SPACE BURST"));
    TEST_ASSERT_FALSE(display.shows("SPACE RIPPLE"));
    TEST_ASSERT_TRUE(display.positionOf("KEY BURST  SPACE BURST").y <
                     display.positionOf("1-0 / FN+1-0").y);
    app.update({fnDigit(8)}, 0ms);
    TEST_ASSERT_TRUE(display.shows("A/D MOVE  W ROT  S SOFT  SPACE HARD"));
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
void test_second_bank_and_feedback() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 42);
    app.onActivate();
    for (int digit = 1; digit <= 9; ++digit) {
        app.update({fnDigit(digit)}, 0ms);
        TEST_ASSERT_EQUAL_UINT8(9 + digit, static_cast<unsigned>(app.currentEffect()));
    }
    app.update({fnDigit(0)}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(19, static_cast<unsigned>(app.currentEffect()));
    TEST_ASSERT_TRUE(display.shows("20/20"));
    auto ignored = key('w');
    ignored.modifiers.fn = true;
    app.update({ignored}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(19, static_cast<unsigned>(app.currentEffect()));
    app.update({arrow(NamedKey::Right)}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(app.currentEffect()));
    app.update({fnDigit(3)}, 0ms);
    TEST_ASSERT_EQUAL_UINT8(12, static_cast<unsigned>(app.currentEffect()));
    TEST_ASSERT_TRUE(display.shows("A/D WIND  W/S HEAT  SPACE FLASH"));
    app.update({key('d')}, 0ms);
    TEST_ASSERT_TRUE(display.shows("WIND 1"));
    app.update({}, 1100ms);
    TEST_ASSERT_FALSE(display.shows("WIND 1"));
    TEST_ASSERT_EQUAL_UINT8(12, static_cast<unsigned>(app.currentEffect()));
    app.onDeactivate();
}
void test_feedback_survives_large_elapsed_on_input() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 42);
    app.onActivate();
    app.update({fnDigit(3)}, 0ms);
    app.update({key('d')}, 1100ms);
    TEST_ASSERT_TRUE(display.shows("WIND 1"));
    app.update({}, 999ms);
    TEST_ASSERT_TRUE(display.shows("WIND 1"));
    app.update({}, 1ms);
    TEST_ASSERT_FALSE(display.shows("WIND 1"));
    app.onDeactivate();
}
void test_space_feedback_describes_the_action() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 42);
    app.onActivate();
    for (const auto& action :
         {std::pair{2, "REAGENT ADDED"}, std::pair{3, "FLASH"}, std::pair{6, "SAND ADDED"}}) {
        app.update({fnDigit(action.first)}, 0ms);
        app.update({key(' ')}, 0ms);
        TEST_ASSERT_TRUE(display.shows(action.second));
    }
    app.onDeactivate();
}
void test_new_effects_are_bounded_and_reproducible() {
    for (unsigned i = 10; i < ledGalleryEffects.size(); ++i) {
        LedGalleryEngine first(71), second(71);
        const auto effect = static_cast<LedGalleryEffect>(i);
        first.reset(effect, 71);
        second.reset(effect, 71);
        first.interact(' ');
        second.interact(' ');
        for (char action : {'a', 'd', 'w', 's'}) {
            first.interact(action);
            second.interact(action);
        }
        first.advance(24h);
        second.advance(24h);
        TEST_ASSERT_TRUE_MESSAGE(sameFrame(first.frame(), second.frame()),
                                 ledGalleryEffects[i].name);
        TEST_ASSERT_EQUAL(64, first.frame().pixels.size());
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<unsigned>(first.effect()));
        for (int step = 0; step < 20; ++step)
            first.advance(100ms);
        TEST_ASSERT_TRUE(first.activeParticles() <= 24);
    }
}
void test_permanent_entities_survive_real_elapsed_time() {
    for (auto effect : {LedGalleryEffect::GravityWell, LedGalleryEffect::Swarm,
                        LedGalleryEffect::ElectricStorm}) {
        LedGalleryEngine engine(31);
        engine.reset(effect, 31);
        const auto expected = engine.activeParticles();
        TEST_ASSERT_TRUE(expected >= 3);
        for (int i = 0; i < 10010; ++i)
            engine.advance(100ms);
        TEST_ASSERT_EQUAL(expected, engine.activeParticles());
        const auto before = engine.frame();
        for (int i = 0; i < 20; ++i)
            engine.advance(100ms);
        TEST_ASSERT_FALSE_MESSAGE(sameFrame(before, engine.frame()),
                                  ledGalleryEffects[static_cast<unsigned>(effect)].name);
    }
}
void test_fn_named_non_digits_are_ignored() {
    FakeLed led;
    FakeDisplay display;
    IndicatorService indicator(led);
    LedGalleryApp app(indicator, display, 3);
    app.onActivate();
    for (auto named : {NamedKey::Left, NamedKey::Right, NamedKey::F11, NamedKey::Up}) {
        auto event = arrow(named);
        event.modifiers.fn = true;
        app.update({event}, 0ms);
        TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(app.currentEffect()));
    }
    app.onDeactivate();
}
void test_fire_controls_and_wind_strength() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine weak(9), strong(9);
    weak.reset(LedGalleryEffect::Fire, 9);
    strong.reset(LedGalleryEffect::Fire, 9);
    for (int i = 0; i < 20; ++i)
        weak.interact('d');
    TEST_ASSERT_EQUAL(3, Probe::wind(weak));
    for (int i = 0; i < 20; ++i)
        weak.interact('a');
    TEST_ASSERT_EQUAL(-3, Probe::wind(weak));
    for (int i = 0; i < 20; ++i)
        weak.interact('w');
    TEST_ASSERT_EQUAL(9, Probe::heat(weak));
    for (int i = 0; i < 20; ++i)
        weak.interact('s');
    TEST_ASSERT_EQUAL(1, Probe::heat(weak));
    weak.reset(LedGalleryEffect::Fire, 9);
    weak.interact('d');
    for (int i = 0; i < 3; ++i)
        strong.interact('d');
    for (int i = 0; i < 10; ++i) {
        weak.advance(80ms);
        strong.advance(80ms);
    }
    TEST_ASSERT_FALSE(sameFrame(weak.frame(), strong.frame()));
    const auto before = weak.frame();
    weak.interact(' ');
    TEST_ASSERT_FALSE(sameFrame(before, weak.frame()));
}
void test_rule_seed_and_bounded_controls() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(21);
    engine.reset(LedGalleryEffect::RuleMachine, 21);
    for (int x = 0; x < 8; ++x)
        TEST_ASSERT_EQUAL((Probe::row(engine) >> x) & 1U, Probe::cells(engine)[x]);
    for (int i = 0; i < 30; ++i)
        engine.interact('w');
    TEST_ASSERT_EQUAL(10, Probe::speed(engine));
    for (int i = 0; i < 30; ++i)
        engine.interact('s');
    TEST_ASSERT_EQUAL(2, Probe::speed(engine));
    for (int i = 0; i < 5; ++i)
        engine.interact('d');
    TEST_ASSERT_EQUAL(0, Probe::rule(engine));
    engine.interact(' ');
    for (int x = 0; x < 8; ++x)
        TEST_ASSERT_EQUAL((Probe::row(engine) >> x) & 1U, Probe::cells(engine)[x]);
}
void test_rule_automatic_reseed_is_visible_and_used() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(21);
    engine.reset(LedGalleryEffect::RuleMachine, 21);
    Probe::setRow(engine, 0);
    Probe::cells(engine).fill(0);
    engine.advance(250ms);
    const auto seed = Probe::row(engine);
    TEST_ASSERT_TRUE(seed != 0);
    for (int x = 0; x < 8; ++x)
        TEST_ASSERT_EQUAL((seed >> x) & 1U, Probe::cells(engine)[x]);
    engine.advance(250ms);
    constexpr std::uint8_t rule = 30;
    for (int x = 0; x < 8; ++x) {
        const int pattern = ((seed >> ((x + 7) % 8) & 1U) << 2) | ((seed >> x & 1U) << 1) |
                            (seed >> ((x + 1) % 8) & 1U);
        TEST_ASSERT_EQUAL((rule >> pattern) & 1U, Probe::cells(engine)[x]);
        TEST_ASSERT_EQUAL((seed >> x) & 1U, Probe::cells(engine)[8 + x]);
    }
}
void test_pulse_duration_independent_of_call_count() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    for (auto effect : {LedGalleryEffect::GravityWell, LedGalleryEffect::Swarm}) {
        LedGalleryEngine a(17), b(17);
        a.reset(effect, 17);
        b.reset(effect, 17);
        a.interact(' ');
        b.interact(' ');
        for (int i = 0; i < 10; ++i)
            a.advance(50ms);
        for (int i = 0; i < 5; ++i)
            b.advance(100ms);
        TEST_ASSERT_FLOAT_WITHIN(.001f, Probe::pulse(a), Probe::pulse(b));
        TEST_ASSERT_TRUE(Probe::pulse(a) > 0);
        for (int i = 0; i < (effect == LedGalleryEffect::Swarm ? 14 : 20); ++i) {
            TEST_ASSERT_FLOAT_WITHIN(.8f, Probe::particles(a)[i].x, Probe::particles(b)[i].x);
            TEST_ASSERT_FLOAT_WITHIN(.8f, Probe::particles(a)[i].y, Probe::particles(b)[i].y);
        }
        a.advance(200ms);
        b.advance(200ms);
        TEST_ASSERT_FLOAT_WITHIN(.001f, 0.f, Probe::pulse(a));
        TEST_ASSERT_FLOAT_WITHIN(.001f, 0.f, Probe::pulse(b));
    }
}
void test_life_recovery_and_actions() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(29), copy(29);
    engine.reset(LedGalleryEffect::GameOfLife, 29);
    copy.reset(LedGalleryEffect::GameOfLife, 29);
    auto count = [](LedGalleryEngine& e) {
        return std::count_if(Probe::cells(e).begin(), Probe::cells(e).end(),
                             [](std::uint8_t cell) { return cell != 0; });
    };
    Probe::cells(engine).fill(0);
    Probe::cells(copy).fill(0);
    engine.interact(' ');
    copy.interact(' ');
    TEST_ASSERT_TRUE(count(engine) > 0);
    TEST_ASSERT_TRUE(sameFrame(engine.frame(), copy.frame()));
    engine.interact('g');
    copy.interact('g');
    TEST_ASSERT_TRUE(sameFrame(engine.frame(), copy.frame()));
    Probe::cells(engine).fill(0);
    engine.advance(150ms);
    TEST_ASSERT_TRUE(count(engine) > 0);
}
void test_life_space_injects_deterministic_local_cluster() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine first(29), second(29);
    first.reset(LedGalleryEffect::GameOfLife, 29);
    second.reset(LedGalleryEffect::GameOfLife, 29);
    Probe::cells(first).fill(0);
    Probe::cells(second).fill(0);
    first.interact(' ');
    second.interact(' ');
    TEST_ASSERT_EQUAL_MEMORY(Probe::cells(first).data(), Probe::cells(second).data(), 64);
    TEST_ASSERT_EQUAL(5, std::count(Probe::cells(first).begin(), Probe::cells(first).end(), 1));
    bool local = false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            std::array<std::uint8_t, 64> expected{};
            expected[y * 8 + x] = 1;
            expected[y * 8 + (x + 7) % 8] = 1;
            expected[y * 8 + (x + 1) % 8] = 1;
            expected[((y + 7) % 8) * 8 + x] = 1;
            expected[((y + 1) % 8) * 8 + x] = 1;
            local |= expected == Probe::cells(first);
        }
    TEST_ASSERT_TRUE(local);
}
void test_reaction_controls_and_injection() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(41);
    engine.reset(LedGalleryEffect::ReactionDiffusion, 41);
    for (int i = 0; i < 100; ++i) {
        engine.interact('w');
        engine.interact('d');
    }
    TEST_ASSERT_FLOAT_WITHIN(.0001f, .065f, Probe::feed(engine));
    TEST_ASSERT_FLOAT_WITHIN(.0001f, .075f, Probe::kill(engine));
    for (int i = 0; i < 100; ++i) {
        engine.interact('s');
        engine.interact('a');
    }
    TEST_ASSERT_FLOAT_WITHIN(.0001f, .018f, Probe::feed(engine));
    TEST_ASSERT_FLOAT_WITHIN(.0001f, .04f, Probe::kill(engine));
    const auto before = Probe::reagent(engine);
    engine.interact(' ');
    TEST_ASSERT_TRUE(before != Probe::reagent(engine));
}
void test_reaction_stays_structured_and_injects_one_patch() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(1);
    engine.reset(LedGalleryEffect::ReactionDiffusion, 1);
    Probe::reagent(engine).fill(0);
    engine.interact(' ');
    int minX = 8, maxX = -1, minY = 8, maxY = -1, count = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (Probe::reagent(engine)[y * 8 + x] > .5f) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
                ++count;
            }
    TEST_ASSERT_TRUE(count >= 3 && count <= 4);
    TEST_ASSERT_TRUE(maxX - minX <= 1 && maxY - minY <= 1);
    engine.reset(LedGalleryEffect::ReactionDiffusion, 1);
    for (int i = 0; i < 800; ++i)
        engine.advance(80ms);
    const auto [low, high] =
        std::minmax_element(Probe::reagent(engine).begin(), Probe::reagent(engine).end());
    TEST_ASSERT_TRUE(*high > .2f);
    TEST_ASSERT_TRUE(*high - *low > .08f);
}
void test_sand_fills_fades_and_restarts() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(1);
    engine.reset(LedGalleryEffect::FallingSand, 1);
    int filled = 0;
    for (int i = 0; i < 1200 && filled < 64; ++i) {
        engine.advance(80ms);
        const int next = std::count(Probe::cells(engine).begin(), Probe::cells(engine).end(), 1);
        TEST_ASSERT_TRUE(next >= filled);
        filled = next;
    }
    TEST_ASSERT_EQUAL(64, filled);
    const auto bright = engine.frame().pixels[0].red;
    for (int i = 0; i < 5; ++i)
        engine.advance(80ms);
    const auto dim = engine.frame().pixels[0].red;
    TEST_ASSERT_TRUE(dim > 0 && dim < bright);
    for (int i = 0; i < 5; ++i)
        engine.advance(80ms);
    TEST_ASSERT_EQUAL(0, std::count(Probe::cells(engine).begin(), Probe::cells(engine).end(), 1));
    engine.advance(80ms);
    TEST_ASSERT_TRUE(std::count(Probe::cells(engine).begin(), Probe::cells(engine).end(), 1) > 0);
}
void test_sand_keeps_settled_layers_until_full() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    for (int direction = 0; direction < 4; ++direction)
        for (int fullLayers : {3, 8}) {
            LedGalleryEngine engine(1);
            engine.reset(LedGalleryEffect::FallingSand, 1);
            for (int turn = 0; turn < direction; ++turn)
                engine.interact('d');
            auto& cells = Probe::cells(engine);
            cells.fill(0);
            const auto index = [direction](int depth, int lane) {
                const int x = direction == 1 ? depth : direction == 3 ? 7 - depth : lane;
                const int y = direction == 0 ? 7 - depth : direction == 2 ? depth : lane;
                return y * 8 + x;
            };
            for (int depth = 0; depth < fullLayers; ++depth)
                for (int lane = 0; lane < 8; ++lane)
                    cells[index(depth, lane)] = 1;
            if (fullLayers == 3)
                for (int lane = 0; lane < 4; ++lane)
                    cells[index(3, lane)] = 1;
            Probe::setGeneration(engine, 7);
            engine.advance(80ms);
            for (int depth = 0; depth < fullLayers; ++depth)
                for (int lane = 0; lane < 8; ++lane)
                    TEST_ASSERT_EQUAL(1, cells[index(depth, lane)]);
            TEST_ASSERT_EQUAL(fullLayers == 3 ? 29 : 64, std::count(cells.begin(), cells.end(), 1));
        }
}
void test_particle_dynamics_match_elapsed_partitions() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    for (auto effect : {LedGalleryEffect::GravityWell, LedGalleryEffect::Swarm}) {
        LedGalleryEngine fine(3), coarse(3);
        fine.reset(effect, 3);
        coarse.reset(effect, 3);
        for (auto* engine : {&fine, &coarse}) {
            for (int i = 0; i < (effect == LedGalleryEffect::Swarm ? 14 : 20); ++i) {
                Probe::particles(*engine)[i].x = 7;
                Probe::particles(*engine)[i].y = 7;
                Probe::particles(*engine)[i].vx = 0;
                Probe::particles(*engine)[i].vy = 0;
            }
            Probe::particles(*engine)[0].x = 3;
            Probe::particles(*engine)[0].y = 3;
            Probe::particles(*engine)[0].vx = 1;
        }
        for (int i = 0; i < 10; ++i)
            fine.advance(50ms);
        for (int i = 0; i < 5; ++i)
            coarse.advance(100ms);
        TEST_ASSERT_FLOAT_WITHIN(.15f, Probe::particles(fine)[0].vx,
                                 Probe::particles(coarse)[0].vx);
    }
}
void test_attractor_and_swarm_controls() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    for (auto effect : {LedGalleryEffect::GravityWell, LedGalleryEffect::Swarm}) {
        LedGalleryEngine a(13), b(13);
        a.reset(effect, 13);
        b.reset(effect, 13);
        for (int i = 0; i < 20; ++i) {
            a.interact('a');
            a.interact('w');
        }
        TEST_ASSERT_EQUAL(0, Probe::targetX(a));
        TEST_ASSERT_EQUAL(0, Probe::targetY(a));
        for (int i = 0; i < 20; ++i) {
            a.interact('d');
            a.interact('s');
        }
        TEST_ASSERT_EQUAL(7, Probe::targetX(a));
        TEST_ASSERT_EQUAL(7, Probe::targetY(a));
        a.reset(effect, 13);
        a.interact(' ');
        TEST_ASSERT_TRUE(Probe::pulse(a) > 0);
        a.advance(100ms);
        b.advance(100ms);
        TEST_ASSERT_FALSE(sameFrame(a.frame(), b.frame()));
        for (int i = 0; i < 10; ++i)
            a.advance(100ms);
        TEST_ASSERT_FLOAT_WITHIN(.001f, 0.f, Probe::pulse(a));
    }
}
void test_swarm_uses_neighbor_alignment_and_cohesion() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine close(5), far(5);
    close.reset(LedGalleryEffect::Swarm, 5);
    far.reset(LedGalleryEffect::Swarm, 5);
    for (int i = 0; i < 14; ++i) {
        Probe::particles(close)[i].x = Probe::particles(far)[i].x = 7;
        Probe::particles(close)[i].y = Probe::particles(far)[i].y = 7;
        Probe::particles(close)[i].vx = Probe::particles(far)[i].vx = 0;
        Probe::particles(close)[i].vy = Probe::particles(far)[i].vy = 0;
    }
    Probe::particles(close)[0].x = Probe::particles(far)[0].x = 3;
    Probe::particles(close)[0].y = Probe::particles(far)[0].y = 3;
    Probe::particles(close)[1].x = 4;
    Probe::particles(close)[1].y = 3;
    Probe::particles(close)[1].vx = 2;
    Probe::particles(far)[1].x = 7;
    Probe::particles(far)[1].y = 7;
    close.advance(100ms);
    far.advance(100ms);
    TEST_ASSERT_TRUE(Probe::particles(close)[0].vx > Probe::particles(far)[0].vx);
    LedGalleryEngine aligned(5), unaligned(5);
    aligned.reset(LedGalleryEffect::Swarm, 5);
    unaligned.reset(LedGalleryEffect::Swarm, 5);
    for (int i = 0; i < 14; ++i) {
        Probe::particles(aligned)[i] = Probe::particles(close)[i];
        Probe::particles(unaligned)[i] = Probe::particles(close)[i];
    }
    Probe::particles(aligned)[0].x = Probe::particles(unaligned)[0].x = 3;
    Probe::particles(aligned)[0].y = Probe::particles(unaligned)[0].y = 3;
    Probe::particles(aligned)[0].vx = Probe::particles(unaligned)[0].vx = 0;
    Probe::particles(aligned)[1].x = Probe::particles(unaligned)[1].x = 4;
    Probe::particles(aligned)[1].y = Probe::particles(unaligned)[1].y = 3;
    Probe::particles(aligned)[1].vx = 2;
    Probe::particles(unaligned)[1].vx = 0;
    aligned.advance(100ms);
    unaligned.advance(100ms);
    TEST_ASSERT_TRUE(Probe::particles(aligned)[0].vx > Probe::particles(unaligned)[0].vx);
}
void test_swarm_recovers_after_scatter() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine engine(17);
    engine.reset(LedGalleryEffect::Swarm, 17);
    engine.interact(' ');
    for (int i = 0; i < 7; ++i)
        engine.advance(100ms);
    auto dispersion = [&engine]() {
        float centerX = 0, centerY = 0, spread = 0;
        for (int i = 0; i < 14; ++i) {
            centerX += Probe::particles(engine)[i].x;
            centerY += Probe::particles(engine)[i].y;
        }
        centerX /= 14;
        centerY /= 14;
        for (int i = 0; i < 14; ++i) {
            const float dx = Probe::particles(engine)[i].x - centerX;
            const float dy = Probe::particles(engine)[i].y - centerY;
            spread += dx * dx + dy * dy;
        }
        return spread / 14;
    };
    const float scattered = dispersion();
    for (int i = 0; i < 50; ++i)
        engine.advance(100ms);
    TEST_ASSERT_TRUE(dispersion() < scattered);
}
void test_sand_ant_tetris_and_storm_controls() {
    using Probe = cardputer_hub::apps::LedGalleryEngineTestAccess;
    LedGalleryEngine sand(7);
    sand.reset(LedGalleryEffect::FallingSand, 7);
    sand.interact(' ');
    TEST_ASSERT_TRUE(std::count(Probe::cells(sand).begin(), Probe::cells(sand).end(), 1) > 0);
    for (int i = 0; i < 4; ++i)
        sand.interact('d');
    TEST_ASSERT_EQUAL(0, Probe::gravity(sand));
    sand.interact('a');
    TEST_ASSERT_EQUAL(3, Probe::gravity(sand));
    sand.reset(LedGalleryEffect::FallingSand, 7);
    Probe::cells(sand).fill(0);
    Probe::cells(sand)[3 * 8 + 3] = 1;
    sand.advance(80ms);
    TEST_ASSERT_TRUE(Probe::cells(sand)[4 * 8 + 3]);
    sand.reset(LedGalleryEffect::FallingSand, 7);
    Probe::cells(sand).fill(0);
    Probe::cells(sand)[3 * 8 + 3] = 1;
    sand.interact('a');
    sand.advance(80ms);
    TEST_ASSERT_TRUE(Probe::cells(sand)[3 * 8 + 4]);

    LedGalleryEngine ant(7);
    ant.reset(LedGalleryEffect::LangtonsAnt, 7);
    for (int i = 0; i < 10; ++i)
        ant.interact(' ');
    TEST_ASSERT_EQUAL(4, Probe::ants(ant));
    for (int i = 0; i < 1000; ++i)
        ant.advance(150ms);
    TEST_ASSERT_EQUAL(4, Probe::ants(ant));
    for (int i = 0; i < Probe::ants(ant); ++i) {
        TEST_ASSERT_TRUE(Probe::antState(ant)[i].x < 8);
        TEST_ASSERT_TRUE(Probe::antState(ant)[i].y < 8);
    }

    LedGalleryEngine tetris(7);
    tetris.reset(LedGalleryEffect::TetrisDream, 7);
    const int initialX = Probe::pieceX(tetris);
    tetris.interact('a');
    TEST_ASSERT_TRUE(Probe::pieceX(tetris) <= initialX);
    for (int i = 0; i < 20; ++i)
        tetris.interact('a');
    TEST_ASSERT_TRUE(Probe::pieceX(tetris) >= 0);
    tetris.reset(LedGalleryEffect::TetrisDream, 7);
    tetris.interact('w');
    TEST_ASSERT_EQUAL(1, Probe::rotation(tetris));
    tetris.interact(' ');
    TEST_ASSERT_TRUE(std::count_if(Probe::cells(tetris).begin(), Probe::cells(tetris).end(),
                                   [](std::uint8_t cell) { return cell != 0; }) > 0);
    bool rotated = false;
    for (int i = 0; i < 300; ++i) {
        tetris.advance(350ms);
        rotated |= Probe::rotation(tetris) != 0;
    }
    TEST_ASSERT_TRUE(rotated);

    LedGalleryEngine storm(7);
    storm.reset(LedGalleryEffect::ElectricStorm, 7);
    for (int i = 0; i < 20; ++i) {
        storm.interact('a');
        storm.interact('w');
    }
    TEST_ASSERT_EQUAL(0, Probe::targetX(storm));
    TEST_ASSERT_EQUAL(0, Probe::targetY(storm));
    for (int i = 0; i < 20; ++i) {
        storm.interact('d');
        storm.interact('s');
    }
    TEST_ASSERT_EQUAL(7, Probe::targetX(storm));
    TEST_ASSERT_EQUAL(7, Probe::targetY(storm));
    storm.reset(LedGalleryEffect::ElectricStorm, 7);
    LedGalleryEngine ambient(7);
    ambient.reset(LedGalleryEffect::ElectricStorm, 7);
    storm.interact(' ');
    TEST_ASSERT_TRUE(Probe::strike(storm) > 0);
    storm.advance(80ms);
    ambient.advance(80ms);
    TEST_ASSERT_FALSE(sameFrame(storm.frame(), ambient.frame()));
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
    RUN_TEST(test_second_bank_and_feedback);
    RUN_TEST(test_feedback_survives_large_elapsed_on_input);
    RUN_TEST(test_space_feedback_describes_the_action);
    RUN_TEST(test_new_effects_are_bounded_and_reproducible);
    RUN_TEST(test_permanent_entities_survive_real_elapsed_time);
    RUN_TEST(test_fn_named_non_digits_are_ignored);
    RUN_TEST(test_fire_controls_and_wind_strength);
    RUN_TEST(test_rule_seed_and_bounded_controls);
    RUN_TEST(test_rule_automatic_reseed_is_visible_and_used);
    RUN_TEST(test_pulse_duration_independent_of_call_count);
    RUN_TEST(test_life_recovery_and_actions);
    RUN_TEST(test_life_space_injects_deterministic_local_cluster);
    RUN_TEST(test_reaction_controls_and_injection);
    RUN_TEST(test_reaction_stays_structured_and_injects_one_patch);
    RUN_TEST(test_sand_fills_fades_and_restarts);
    RUN_TEST(test_sand_keeps_settled_layers_until_full);
    RUN_TEST(test_particle_dynamics_match_elapsed_partitions);
    RUN_TEST(test_attractor_and_swarm_controls);
    RUN_TEST(test_swarm_uses_neighbor_alignment_and_cohesion);
    RUN_TEST(test_swarm_recovers_after_scatter);
    RUN_TEST(test_sand_ant_tetris_and_storm_controls);
    return UNITY_END();
}
