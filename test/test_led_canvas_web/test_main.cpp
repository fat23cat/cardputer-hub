#include "services/led_canvas/led_canvas_web_endpoint.h"
#include <unity.h>
using namespace cardputer_hub;
namespace {
struct Fixture {
    services::LedCanvasService canvas;
    services::LedCanvasWebEndpoint endpoint{canvas};
    connectivity::HttpResponse call(connectivity::HttpMethod method, std::string_view path,
                                    std::string body = "", std::string marker = "1") {
        connectivity::HttpRequest r;
        r.method = method;
        r.requestMarker = marker;
        r.body = body;
        return endpoint.handle(r, path);
    }
};
using connectivity::HttpMethod;
constexpr char stroke[] = R"({"client":"9f2c41d0","seq":42,"p":[[0,896,896,12,0,1]]})";
void test_strokes_rejected_while_closed() {
    Fixture f;
    auto r = f.call(HttpMethod::Post, "/strokes", stroke);
    TEST_ASSERT_EQUAL(409, r.status);
    TEST_ASSERT_EQUAL_STRING(R"({"error":"app-closed"})", r.body.c_str());
}
void test_batch_accepted_and_retry_ignored() {
    Fixture f;
    f.canvas.setActive(true);
    TEST_ASSERT_EQUAL(200, f.call(HttpMethod::Post, "/strokes", stroke).status);
    TEST_ASSERT_EQUAL(200, f.call(HttpMethod::Post, "/strokes", stroke).status);
    TEST_ASSERT_EQUAL(1, f.canvas.status().queued);
    TEST_ASSERT_EQUAL_STRING(R"({"open":true,"fingers":1})", f.endpoint.summaryJson().c_str());
}
void test_settings_put_validates_and_bumps_revision() {
    Fixture f;
    auto r = f.call(HttpMethod::Put, "/settings",
                    R"({"mode":"embers","fade":"long","ink":6,"hold":true})");
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL(1, f.canvas.status().revision);
    TEST_ASSERT_TRUE(f.canvas.status().settings.hold);
    for (const auto* body : {R"({"mode":"speed","fade":"long","ink":6,"hold":true})",
                             R"({"mode":"solid","fade":"long","ink":7,"hold":true})",
                             R"({"mode":"solid","fade":"long","ink":0,"hold":1})",
                             R"({"mode":"solid","fade":"long","ink":0})",
                             R"({"mode":"solid","fade":"long","ink":0,"hold":true,"ink":1})"})
        TEST_ASSERT_EQUAL(400, f.call(HttpMethod::Put, "/settings", body).status);
    TEST_ASSERT_EQUAL(1, f.canvas.status().revision);
}
void test_strict_batch_limits() {
    Fixture f;
    f.canvas.setActive(true);
    for (const auto* body :
         {R"({"client":"invalid!","seq":42,"p":[[0,896,896,12,0,1]]})",
          R"({"client":"00000001","seq":42,"p":[[5,896,896,12,0,1]]})",
          R"({"client":"00000001","seq":42,"p":[[0,2048,896,12,0,1]]})",
          R"({"client":"00000001","seq":42,"p":[[0,896,896,4,0,1]]})",
          R"({"client":"00000001","seq":42,"p":[[0,896,896,12,256,1]]})",
          R"({"client":"00000001","seq":42,"p":[[0,896,896,12,0,4]]})",
          R"({"client":"00000001","seq":4294967296,"p":[]})",
          R"({"client":"00000001","seq":1.5,"p":[]})", R"({"client":"00000001","seq":01,"p":[]})",
          R"({"client":"00000001","seq":1,"p":[[0,1,1,12,0,1],]})",
          R"({"client":"00000001","seq":1,"seq":2,"p":[[0,1,1,12,0,1]]})"})
        TEST_ASSERT_EQUAL(400, f.call(HttpMethod::Post, "/strokes", body).status);
    std::string b = R"({"client":"00000001","seq":1,"p":[)";
    for (unsigned i = 0; i < 65; ++i) {
        if (i)
            b += ',';
        b += "[0,896,896,12,0,1]";
    }
    b += "]}";
    TEST_ASSERT_EQUAL(400, f.call(HttpMethod::Post, "/strokes", b).status);
    TEST_ASSERT_EQUAL(0, f.canvas.status().queued);
}
void test_color_phase_is_decoded_and_bounded() {
    Fixture f;
    f.canvas.setActive(true);
    TEST_ASSERT_EQUAL(200,
                      f.call(HttpMethod::Post, "/strokes",
                             R"({"client":"00000001","seq":1,"p":[[0,896,896,12,0,1,7500,2]]})")
                          .status);
    const auto due = f.canvas.takeDue(std::chrono::milliseconds{100});
    TEST_ASSERT_TRUE(due);
    TEST_ASSERT_TRUE(due->point.hasColor);
    TEST_ASSERT_EQUAL(7500, due->point.hue);
    TEST_ASSERT_EQUAL(2, due->point.mixInk);
    for (const auto* p : {"[0,896,896,12,0,1,10000,2]", "[0,896,896,12,0,1,7500,3]",
                          "[0,896,896,12,0,1,7500]", "[0,896,896,12,0,1,7500,2,0]"}) {
        TEST_ASSERT_EQUAL(400,
                          f.call(HttpMethod::Post, "/strokes",
                                 std::string(R"({"client":"00000001","seq":2,"p":[)") + p + "]}")
                              .status);
    }
    TEST_ASSERT_EQUAL(0, f.canvas.status().queued);
}
void test_marker_size_clear_and_reset() {
    Fixture f;
    f.canvas.setActive(true);
    TEST_ASSERT_EQUAL(403, f.call(HttpMethod::Post, "/strokes", stroke, "").status);
    TEST_ASSERT_EQUAL(413, f.call(HttpMethod::Post, "/strokes", std::string(5121, ' ')).status);
    f.call(HttpMethod::Post, "/strokes", stroke);
    f.endpoint.reset();
    TEST_ASSERT_EQUAL(0, f.canvas.status().fingers);
    TEST_ASSERT_EQUAL(0, f.canvas.status().queued);
    TEST_ASSERT_EQUAL(204, f.call(HttpMethod::Post, "/clear").status);
    f.canvas.setActive(false);
    TEST_ASSERT_EQUAL(409, f.call(HttpMethod::Post, "/clear").status);
    TEST_ASSERT_EQUAL(405, f.call(HttpMethod::Get, "/clear").status);
    TEST_ASSERT_EQUAL(404, f.call(HttpMethod::Get, "/missing").status);
}
} // namespace
void setUp() {}
void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_strokes_rejected_while_closed);
    RUN_TEST(test_batch_accepted_and_retry_ignored);
    RUN_TEST(test_settings_put_validates_and_bumps_revision);
    RUN_TEST(test_strict_batch_limits);
    RUN_TEST(test_marker_size_clear_and_reset);
    RUN_TEST(test_color_phase_is_decoded_and_bounded);
    return UNITY_END();
}
