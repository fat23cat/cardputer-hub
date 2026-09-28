#include "core/display/slide_transition.h"
#include <algorithm>
#include <array>
#include <unity.h>
using namespace cardputer_hub::core;
using namespace std::chrono_literals;
namespace {
void test_slide_finishes_in_220_ms_with_cubic_ease_out_and_bounded_frames() {
    SlideTransition slide;
    TEST_ASSERT_FALSE(slide.active());
    slide.start(SlideDirection::Forward);
    TEST_ASSERT_TRUE(slide.active());
    TEST_ASSERT_EQUAL(0, slide.offset());
    slide.advance(15ms);
    TEST_ASSERT_EQUAL(0, slide.offset());
    slide.advance(1ms);
    TEST_ASSERT_TRUE(slide.offset() > 0);
    const auto firstStep = slide.offset();
    slide.advance(16ms);
    TEST_ASSERT_TRUE(slide.offset() - firstStep < firstStep);
    slide.advance(187ms);
    TEST_ASSERT_TRUE(slide.active());
    TEST_ASSERT_TRUE(slide.offset() < 240);
    slide.advance(1ms);
    TEST_ASSERT_FALSE(slide.active());
    TEST_ASSERT_EQUAL(240, slide.offset());
}
void test_slide_restart_and_large_or_negative_time_are_safe() {
    SlideTransition slide;
    slide.start(SlideDirection::Forward);
    slide.advance(100ms);
    slide.start(SlideDirection::Backward);
    TEST_ASSERT_TRUE(slide.direction() == SlideDirection::Backward);
    TEST_ASSERT_EQUAL(0, slide.offset());
    slide.advance(-10ms);
    TEST_ASSERT_EQUAL(0, slide.offset());
    slide.advance(std::chrono::milliseconds::max());
    TEST_ASSERT_FALSE(slide.active());
    TEST_ASSERT_EQUAL(240, slide.offset());
}
void test_interruption_snapshot_preserves_every_pixel_without_gaps_in_both_directions() {
    const std::array<std::uint16_t, 8> from{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint16_t, 8> to{11, 12, 13, 14, 15, 16, 17, 18};
    auto frame = from;
    composeSlideSnapshot(frame.data(), to.data(), 4, 2, 1, SlideDirection::Forward);
    const std::array<std::uint16_t, 8> forward{2, 3, 4, 11, 6, 7, 8, 15};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(forward.data(), frame.data(), 8);
    frame = from;
    composeSlideSnapshot(frame.data(), to.data(), 4, 2, 1, SlideDirection::Backward);
    const std::array<std::uint16_t, 8> backward{14, 1, 2, 3, 18, 5, 6, 7};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(backward.data(), frame.data(), 8);
    for (const auto direction : {SlideDirection::Forward, SlideDirection::Backward}) {
        frame = from;
        composeSlideSnapshot(frame.data(), to.data(), 4, 2, 0, direction);
        TEST_ASSERT_EQUAL_UINT16_ARRAY(from.data(), frame.data(), 8);
        composeSlideSnapshot(frame.data(), to.data(), 4, 2, 4, direction);
        TEST_ASSERT_EQUAL_UINT16_ARRAY(to.data(), frame.data(), 8);
    }
}
void test_incremental_snapshot_advances_as_one_complete_frame_and_refreshes_incoming_pixels() {
    const std::array<std::uint16_t, 4> from{1, 2, 3, 4};
    std::array<std::uint16_t, 4> to{11, 12, 13, 14};
    auto frame = from;

    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 0, 1, SlideDirection::Forward);
    const std::array<std::uint16_t, 4> forwardOne{2, 3, 4, 11};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(forwardOne.data(), frame.data(), 4);

    to[0] = 21;
    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 1, 1, SlideDirection::Forward);
    const std::array<std::uint16_t, 4> forwardRefreshed{2, 3, 4, 21};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(forwardRefreshed.data(), frame.data(), 4);

    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 1, 3, SlideDirection::Forward);
    const std::array<std::uint16_t, 4> forwardThree{4, 21, 12, 13};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(forwardThree.data(), frame.data(), 4);

    frame = from;
    to = {11, 12, 13, 14};
    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 0, 1, SlideDirection::Backward);
    const std::array<std::uint16_t, 4> backwardOne{14, 1, 2, 3};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(backwardOne.data(), frame.data(), 4);

    to[3] = 24;
    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 1, 3, SlideDirection::Backward);
    const std::array<std::uint16_t, 4> backwardThree{12, 13, 24, 1};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(backwardThree.data(), frame.data(), 4);

    advanceSlideSnapshot(frame.data(), to.data(), 4, 1, 3, 4, SlideDirection::Backward);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(to.data(), frame.data(), 4);
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_slide_finishes_in_220_ms_with_cubic_ease_out_and_bounded_frames);
    RUN_TEST(test_slide_restart_and_large_or_negative_time_are_safe);
    RUN_TEST(test_interruption_snapshot_preserves_every_pixel_without_gaps_in_both_directions);
    RUN_TEST(
        test_incremental_snapshot_advances_as_one_complete_frame_and_refreshes_incoming_pixels);
    return UNITY_END();
}
