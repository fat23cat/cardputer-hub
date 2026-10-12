#include "apps/led_canvas/led_canvas_engine.h"
#include <unity.h>
using namespace cardputer_hub;
using namespace std::chrono_literals;
namespace {
core::LedCanvasPlayback point(unsigned pointer = 0, unsigned flags = 1, unsigned x = 896,
                              unsigned y = 896, unsigned time = 0) {
    return {{0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), 6, 0,
             static_cast<std::uint8_t>(flags)},
            static_cast<std::uint8_t>(pointer),
            time};
}
void test_brush_center_full_and_halo_dimmer() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Solid, core::LedCanvasFade::Medium, 6, false});
    e.point(point());
    auto f = e.frame();
    TEST_ASSERT_EQUAL(255, f.pixels[27].red);
    TEST_ASSERT_TRUE(f.pixels[28].red > 0 && f.pixels[28].red < 255);
    TEST_ASSERT_EQUAL(0, f.pixels[0].red);
}
void test_mix_red_and_green_make_yellow() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Mix, core::LedCanvasFade::Medium, 0, true});
    e.point(point());
    e.point(point(1));
    auto f = e.frame();
    TEST_ASSERT_EQUAL(255, f.pixels[27].red);
    TEST_ASSERT_EQUAL(255, f.pixels[27].green);
    e.advance(33ms);
    TEST_ASSERT_EQUAL(255, e.frame().pixels[27].red);
}
void test_fade_hold_and_stationary_pointer() {
    apps::LedCanvasEngine e;
    core::LedCanvasSettings s{core::LedCanvasMode::Solid, core::LedCanvasFade::Medium, 6, false};
    e.setSettings(s);
    e.point(point());
    e.endPointers(1);
    e.advance(1000ms);
    TEST_ASSERT_INT_WITHIN(1, 94, e.frame().pixels[27].red);
    s.hold = true;
    e.setSettings(s);
    auto f = e.frame();
    e.advance(1000ms);
    TEST_ASSERT_EQUAL(f.pixels[27].red, e.frame().pixels[27].red);
    s.hold = false;
    e.setSettings(s);
    e.point(point());
    e.advance(33ms);
    TEST_ASSERT_TRUE(e.frame().pixels[27].red > 240);
    e.endPointers(1);
    e.advance(10000ms);
    TEST_ASSERT_FALSE(e.live());
}
void test_sampling_covers_fast_stroke() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Solid, core::LedCanvasFade::Long, 6, true});
    e.point(point(0, 1, 128, 128));
    e.point(point(0, 0, 1920, 128, 100));
    for (unsigned i = 0; i < 8; ++i)
        TEST_ASSERT_EQUAL(255, e.frame().pixels[i].red);
}
void test_rainbow_steps_on_entering_led_only() {
    apps::LedCanvasEngine e;
    e.point(point());
    auto f = e.frame();
    e.point(point(0, 0));
    TEST_ASSERT_EQUAL(f.pixels[27].green, e.frame().pixels[27].green);
    e.point(point(0, 0, 1152));
    TEST_ASSERT_TRUE(e.frame().pixels[28].green > f.pixels[27].green);
}
void test_embers_cool_and_hold() {
    apps::LedCanvasEngine e;
    core::LedCanvasSettings s{core::LedCanvasMode::Embers, core::LedCanvasFade::Medium, 0, false};
    e.setSettings(s);
    e.point(point());
    e.endPointers(1);
    TEST_ASSERT_EQUAL(236, e.frame().pixels[27].green);
    e.advance(700ms);
    TEST_ASSERT_EQUAL(60, e.frame().pixels[27].green);
    s.hold = true;
    e.setSettings(s);
    e.advance(1000ms);
    TEST_ASSERT_EQUAL(60, e.frame().pixels[27].green);
}
void test_velocity_depends_on_speed() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Velocity, core::LedCanvasFade::Medium, 0, true});
    e.point(point());
    TEST_ASSERT_EQUAL(200, e.frame().pixels[27].blue);
    e.point(point(0, 0, 1920, 896, 8));
    TEST_ASSERT_EQUAL(255, e.frame().pixels[31].red);
}
void test_ripple_moves_and_expires_while_held() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Ripple, core::LedCanvasFade::Medium, 0, true});
    e.point(point());
    e.endPointers(1);
    auto before = e.frame();
    e.advance(400ms);
    TEST_ASSERT_TRUE(e.frame().pixels[30].red > before.pixels[30].red);
    e.clear();
    TEST_ASSERT_FALSE(e.live());
}
void test_kaleidoscope_has_four_colored_mirrors() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Kaleidoscope, core::LedCanvasFade::Medium, 0, true});
    e.point(point(0, 1, 128, 128));
    auto f = e.frame();
    TEST_ASSERT_TRUE(f.pixels[0].red > 0);
    TEST_ASSERT_TRUE(f.pixels[7].green > 0);
    TEST_ASSERT_TRUE(f.pixels[63].blue > 0);
    TEST_ASSERT_TRUE(f.pixels[56].red > 0);
}
void test_solid_inks_and_clear() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Solid, core::LedCanvasFade::Medium, 3, true});
    e.point(point());
    auto f = e.frame();
    TEST_ASSERT_EQUAL(0, f.pixels[27].red);
    TEST_ASSERT_EQUAL(200, f.pixels[27].green);
    TEST_ASSERT_EQUAL(255, f.pixels[27].blue);
    e.clear();
    TEST_ASSERT_FALSE(e.live());
}
void test_point_color_phase_survives_another_browser_and_missing_samples() {
    for (const auto mode : {core::LedCanvasMode::Rainbow, core::LedCanvasMode::Kaleidoscope}) {
        apps::LedCanvasEngine e;
        e.setSettings({mode, core::LedCanvasFade::Medium, 0, true});
        auto a = point(0, 1, 128, 128);
        a.point.hasColor = true;
        a.point.hue = 0;
        e.point(a);
        auto other = point(5, 1, 896, 896, 10);
        other.point.hasColor = true;
        other.point.hue = 5000;
        e.point(other);
        // Repainting a stationary finger must retain its own phase.
        e.advance(33ms);
        auto frame = e.frame();
        TEST_ASSERT_EQUAL(255, frame.pixels[0].red);
        // A thinned stroke supplies its ending phase, independent of skipped path steps.
        auto moved = point(0, 0, 1920, 128, 50);
        moved.point.hasColor = true;
        moved.point.hue = 7500;
        e.point(moved);
        frame = e.frame();
        TEST_ASSERT_INT_WITHIN(1, 128, frame.pixels[7].red);
        TEST_ASSERT_EQUAL(0, frame.pixels[7].green);
        TEST_ASSERT_EQUAL(255, frame.pixels[7].blue);
    }
}
void test_mix_uses_the_browser_stroke_ink() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Mix, core::LedCanvasFade::Medium, 0, true});
    e.point(point());
    auto p = point(5, 1, 1920, 128);
    p.point.hasColor = true;
    p.point.mixInk = 2;
    e.point(p);
    TEST_ASSERT_EQUAL(30, e.frame().pixels[7].red);
    TEST_ASSERT_EQUAL(70, e.frame().pixels[7].green);
    TEST_ASSERT_EQUAL(255, e.frame().pixels[7].blue);
}
void test_ripple_ring_uses_the_supplied_phase() {
    apps::LedCanvasEngine e;
    e.setSettings({core::LedCanvasMode::Ripple, core::LedCanvasFade::Medium, 0, true});
    auto p = point();
    p.point.hasColor = true;
    p.point.hue = 5000;
    e.point(p);
    e.endPointers(1);
    e.advance(400ms);
    const auto frame = e.frame();
    TEST_ASSERT_EQUAL(0, frame.pixels[30].red);
    TEST_ASSERT_TRUE(frame.pixels[30].green > 0);
    TEST_ASSERT_EQUAL(frame.pixels[30].green, frame.pixels[30].blue);
}
void test_clear_restarts_generated_colors_like_the_browser() {
    const std::array<core::LedCanvasMode, 4> modes{
        {core::LedCanvasMode::Rainbow, core::LedCanvasMode::Mix, core::LedCanvasMode::Ripple,
         core::LedCanvasMode::Kaleidoscope}};
    for (const auto mode : modes) {
        const core::LedCanvasSettings settings{mode, core::LedCanvasFade::Medium, 0, true};
        apps::LedCanvasEngine used;
        used.setSettings(settings);
        used.point(point());
        used.point(point(0, 0, 1920, 896, 50));
        used.clear();
        used.point(point());
        apps::LedCanvasEngine fresh;
        fresh.setSettings(settings);
        fresh.point(point());
        const auto actual = used.frame(), expected = fresh.frame();
        for (std::size_t i = 0; i < 64; ++i) {
            TEST_ASSERT_EQUAL(expected.pixels[i].red, actual.pixels[i].red);
            TEST_ASSERT_EQUAL(expected.pixels[i].green, actual.pixels[i].green);
            TEST_ASSERT_EQUAL(expected.pixels[i].blue, actual.pixels[i].blue);
        }
    }
}
} // namespace
void setUp() {}
void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_brush_center_full_and_halo_dimmer);
    RUN_TEST(test_mix_red_and_green_make_yellow);
    RUN_TEST(test_fade_hold_and_stationary_pointer);
    RUN_TEST(test_sampling_covers_fast_stroke);
    RUN_TEST(test_rainbow_steps_on_entering_led_only);
    RUN_TEST(test_embers_cool_and_hold);
    RUN_TEST(test_velocity_depends_on_speed);
    RUN_TEST(test_ripple_moves_and_expires_while_held);
    RUN_TEST(test_kaleidoscope_has_four_colored_mirrors);
    RUN_TEST(test_solid_inks_and_clear);
    RUN_TEST(test_clear_restarts_generated_colors_like_the_browser);
    RUN_TEST(test_point_color_phase_survives_another_browser_and_missing_samples);
    RUN_TEST(test_mix_uses_the_browser_stroke_ink);
    RUN_TEST(test_ripple_ring_uses_the_supplied_phase);
    return UNITY_END();
}
