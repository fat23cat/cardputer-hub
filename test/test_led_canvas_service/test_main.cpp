#include "services/led_canvas/led_canvas_service.h"
#include <unity.h>
using namespace cardputer_hub::services;
using namespace std::chrono_literals;
namespace {
LedCanvasBatch batch(std::uint32_t client = 1, std::uint32_t seq = 1) {
    LedCanvasBatch b;
    b.client = client;
    b.seq = seq;
    b.count = 1;
    b.points[0] = {0, 896, 896, 12, 0, 1};
    return b;
}
void test_batch_is_played_after_delay() {
    LedCanvasService s;
    s.setActive(true);
    TEST_ASSERT_TRUE(s.submit(batch()));
    TEST_ASSERT_FALSE(s.takeDue(99ms));
    auto p = s.takeDue(1ms);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL(896, p->point.x);
    TEST_ASSERT_EQUAL(100, p->time);
}
void test_repeated_seq_is_ignored() {
    LedCanvasService s;
    s.setActive(true);
    s.submit(batch());
    s.submit(batch());
    TEST_ASSERT_EQUAL(1, s.status().queued);
}
void test_clients_get_separate_pointers() {
    LedCanvasService s;
    s.setActive(true);
    s.submit(batch());
    s.submit(batch(2));
    TEST_ASSERT_EQUAL(2, s.status().fingers);
    auto a = s.takeDue(100ms);
    auto b = s.takeDue(0ms);
    TEST_ASSERT_TRUE(a && b);
    TEST_ASSERT_NOT_EQUAL(a->pointer, b->pointer);
}
void test_silent_pointer_ends_after_500ms() {
    LedCanvasService s;
    s.setActive(true);
    s.submit(batch());
    (void)s.takeDue(100ms);
    (void)s.takeDue(399ms);
    TEST_ASSERT_EQUAL(1, s.status().fingers);
    (void)s.takeDue(1ms);
    TEST_ASSERT_EQUAL(0, s.status().fingers);
    TEST_ASSERT_EQUAL(1, s.takeEnded());
}
void test_full_queue_drops_oldest() {
    LedCanvasService s;
    s.setActive(true);
    for (unsigned i = 1; i <= 257; ++i) {
        auto b = batch(1, i);
        b.points[0].x = i;
        s.submit(b);
    }
    TEST_ASSERT_EQUAL(256, s.status().queued);
    auto p = s.takeDue(100ms);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL(2, p->point.x);
}
void test_hold_action_bumps_revision() {
    LedCanvasService s;
    TEST_ASSERT_EQUAL(cardputer_hub::core::ActionHandlingResult::Handled,
                      s.handle({ledCanvasHoldActionId, "test", {{"hold", true}}}));
    TEST_ASSERT_TRUE(s.status().settings.hold);
    TEST_ASSERT_EQUAL(1, s.status().revision);
    TEST_ASSERT_EQUAL(cardputer_hub::core::ActionHandlingResult::Rejected,
                      s.handle({ledCanvasHoldActionId, "test", {{"hold", std::int32_t{1}}}}));
}
void test_close_and_clear_drop_stale_input() {
    LedCanvasService s;
    TEST_ASSERT_FALSE(s.submit(batch()));
    s.setActive(true);
    s.submit(batch());
    s.clear();
    TEST_ASSERT_EQUAL(0, s.status().queued);
    TEST_ASSERT_EQUAL(0, s.status().fingers);
    TEST_ASSERT_EQUAL(1, s.status().clearGeneration);
    s.submit(batch(1, 2));
    s.setActive(false);
    TEST_ASSERT_EQUAL(0, s.status().queued);
    TEST_ASSERT_FALSE(s.takeDue(1000ms));
    s.setActive(true);
    TEST_ASSERT_TRUE(s.submit(batch()));
}
void test_playout_orders_clients_and_preserves_dt() {
    LedCanvasService s;
    s.setActive(true);
    auto a = batch();
    a.count = 2;
    a.points[1] = a.points[0];
    a.points[1].dt = 100;
    s.submit(a);
    (void)s.takeDue(50ms);
    s.submit(batch(2));
    auto p = s.takeDue(50ms);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL(100, p->time);
    p = s.takeDue(50ms);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL(150, p->time);
    p = s.takeDue(50ms);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL(200, p->time);
}
void test_same_client_batches_cannot_overtake_pending_end() {
    LedCanvasService s;
    s.setActive(true);
    auto first = batch();
    first.count = 3;
    first.points[1] = first.points[0];
    first.points[1].dt = 100;
    first.points[1].flags = 0;
    first.points[2] = first.points[1];
    first.points[2].flags = 2;
    TEST_ASSERT_TRUE(s.submit(first));
    TEST_ASSERT_FALSE(s.takeDue(60ms));
    auto next = batch(1, 2);
    next.points[0].dt = 10;
    TEST_ASSERT_TRUE(s.submit(next));
    TEST_ASSERT_TRUE(s.submit(batch(2)));
    const std::array<unsigned, 5> times{{100, 160, 200, 300, 310}};
    const std::array<unsigned, 5> flags{{1, 1, 0, 2, 1}};
    std::uint8_t pointer = 0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto point = s.takeDue(i == 0 ? 300ms : 0ms);
        TEST_ASSERT_TRUE(point);
        TEST_ASSERT_EQUAL(times[i], point->time);
        TEST_ASSERT_EQUAL(flags[i], point->point.flags);
        if (i == 0)
            pointer = point->pointer;
        if (i == 1)
            TEST_ASSERT_NOT_EQUAL(pointer, point->pointer);
        else
            TEST_ASSERT_EQUAL(pointer, point->pointer);
    }
}
void test_later_arrival_and_clear_reset_the_playout_timeline() {
    LedCanvasService s;
    s.setActive(true);
    s.submit(batch());
    (void)s.takeDue(600ms);
    auto next = batch(1, 2);
    next.points[0].dt = 10;
    s.submit(next);
    auto point = s.takeDue(110ms);
    TEST_ASSERT_TRUE(point);
    TEST_ASSERT_EQUAL(710, point->time);
    auto backlog = batch(1, 3);
    backlog.count = 64;
    backlog.points.fill({0, 896, 896, 12, 255, 0});
    s.submit(backlog);
    s.clear();
    s.submit(batch(1, 4));
    point = s.takeDue(100ms);
    TEST_ASSERT_TRUE(point);
    TEST_ASSERT_EQUAL(810, point->time);
}
void test_lru_replacement_ends_old_client() {
    LedCanvasService s;
    s.setActive(true);
    for (unsigned i = 1; i <= 4; ++i)
        s.submit(batch(i));
    s.submit(batch(5));
    TEST_ASSERT_EQUAL(4, s.status().fingers);
    TEST_ASSERT_EQUAL(4, s.status().queued);
    TEST_ASSERT_EQUAL(31, s.takeEnded());
}
void test_dropped_end_still_expires_the_playback_pointer() {
    LedCanvasService s;
    s.setActive(true);
    s.submit(batch());
    (void)s.takeDue(100ms);
    auto end = batch(1, 2);
    end.points[0].flags = 2;
    s.submit(end);
    for (unsigned i = 1; i <= 256; ++i)
        s.submit(batch(2, i));
    // The old client's End was dropped by overflow; it still needs an engine stop.
    while (s.takeDue(100ms)) {
    }
    (void)s.takeDue(500ms);
    TEST_ASSERT_TRUE(s.takeEnded() & 1);
}
void test_invalid_batch_is_atomic() {
    LedCanvasService s;
    s.setActive(true);
    auto b = batch();
    b.count = 2;
    b.points[1].slot = 5;
    TEST_ASSERT_FALSE(s.submit(b));
    TEST_ASSERT_EQUAL(0, s.status().queued);
    TEST_ASSERT_EQUAL(0, s.status().fingers);
}
} // namespace
void setUp() {}
void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_batch_is_played_after_delay);
    RUN_TEST(test_repeated_seq_is_ignored);
    RUN_TEST(test_clients_get_separate_pointers);
    RUN_TEST(test_silent_pointer_ends_after_500ms);
    RUN_TEST(test_full_queue_drops_oldest);
    RUN_TEST(test_hold_action_bumps_revision);
    RUN_TEST(test_close_and_clear_drop_stale_input);
    RUN_TEST(test_playout_orders_clients_and_preserves_dt);
    RUN_TEST(test_lru_replacement_ends_old_client);
    RUN_TEST(test_same_client_batches_cannot_overtake_pending_end);
    RUN_TEST(test_later_arrival_and_clear_reset_the_playout_timeline);
    RUN_TEST(test_invalid_batch_is_atomic);
    RUN_TEST(test_dropped_end_still_expires_the_playback_pointer);
    return UNITY_END();
}
