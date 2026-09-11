#include <unity.h>

#include <chrono>
#include <string>
#include <vector>

#include "core/lifecycle/system_runtime.h"

namespace {

using cardputer_hub::core::BuildInfo;
using cardputer_hub::core::IDisplayAdapter;
using cardputer_hub::core::IKeyboardAdapter;
using cardputer_hub::core::ILogSink;
using cardputer_hub::core::InputEvents;
using cardputer_hub::core::InputEventType;
using cardputer_hub::core::IPlatformAdapter;
using cardputer_hub::core::Logger;
using cardputer_hub::core::LogLevel;
using cardputer_hub::core::LogRecord;
using cardputer_hub::core::NamedKey;
using cardputer_hub::core::PixelPosition;
using cardputer_hub::core::RgbColor;
using cardputer_hub::core::SystemRuntime;
using cardputer_hub::core::TextStyle;

class FakePlatform final : public IPlatformAdapter {
  public:
    explicit FakePlatform(std::vector<std::string>& trace) : trace_(trace) {}

    void begin() override { trace_.push_back("platform.begin"); }
    void update() override { trace_.push_back("platform.update"); }

  private:
    std::vector<std::string>& trace_;
};

class FakeKeyboard final : public IKeyboardAdapter {
  public:
    explicit FakeKeyboard(std::vector<std::string>& trace) : trace_(trace) {}

    void poll(InputEvents& events) override {
        trace_.push_back("keyboard.poll");
        events = nextEvents;
    }

    InputEvents nextEvents;

  private:
    std::vector<std::string>& trace_;
};

struct TextCommand {
    PixelPosition position;
    std::string text;
    TextStyle style;
};

struct RectangleCommand {
    PixelPosition position;
    std::int32_t width;
    std::int32_t height;
    RgbColor color;
};

class FakeDisplay final : public IDisplayAdapter {
  public:
    explicit FakeDisplay(std::vector<std::string>& trace) : trace_(trace) {}

    void clear(RgbColor color) override {
        trace_.push_back("display.clear");
        clears.push_back(color);
    }

    void drawText(PixelPosition position, const char* text, TextStyle style) override {
        trace_.push_back(std::string("display.text:") + text);
        texts.push_back({position, text, style});
    }

    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        rectangles.push_back({position, width, height, color});
    }

    std::vector<RgbColor> clears;
    std::vector<TextCommand> texts;
    std::vector<RectangleCommand> rectangles;

  private:
    std::vector<std::string>& trace_;
};

class FakeLogSink final : public ILogSink {
  public:
    explicit FakeLogSink(std::vector<std::string>& trace) : trace_(trace) {}

    void write(const LogRecord& record) override {
        trace_.push_back(std::string("log:") + record.component + ":" + record.message);
        records.push_back(record);
    }

    std::vector<LogRecord> records;

  private:
    std::vector<std::string>& trace_;
};

struct RuntimeFixture {
    RuntimeFixture()
        : platform(trace), keyboard(trace), display(trace), logSink(trace),
          logger(logSink, LogLevel::Info), runtime(platform, keyboard, display, logger, buildInfo) {
    }

    std::vector<std::string> trace;
    FakePlatform platform;
    FakeKeyboard keyboard;
    FakeDisplay display;
    FakeLogSink logSink;
    Logger logger;
    const BuildInfo buildInfo{"Test Hub", "9.8.7", "abc123", "test"};
    SystemRuntime runtime;
};

void assertColor(RgbColor expected, RgbColor actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.red, actual.red);
    TEST_ASSERT_EQUAL_UINT8(expected.green, actual.green);
    TEST_ASSERT_EQUAL_UINT8(expected.blue, actual.blue);
}

} // namespace

void setUp() {}

void tearDown() {}

void test_startup_draws_branded_splash_with_visible_version() {
    RuntimeFixture fixture;

    fixture.runtime.start();

    const char* expectedTrace[] = {
        "platform.begin",
        "log:firmware.name:Test Hub",
        "log:firmware.version:9.8.7",
        "log:firmware.commit:abc123",
        "log:firmware.build_type:test",
        "display.clear",
        "display.text:SYSTEM STARTUP",
        "display.text:Test Hub",
        "display.text:VERSION",
        "display.text:9.8.7",
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(expectedTrace) / sizeof(expectedTrace[0]), fixture.trace.size());
    for (std::size_t index = 0; index < fixture.trace.size(); ++index) {
        TEST_ASSERT_EQUAL_STRING(expectedTrace[index], fixture.trace[index].c_str());
    }

    TEST_ASSERT_EQUAL_UINT(4, fixture.logSink.records.size());
    TEST_ASSERT_EQUAL_UINT(1, fixture.display.clears.size());
    assertColor({0xF4, 0xF2, 0xEC}, fixture.display.clears[0]);
    TEST_ASSERT_EQUAL_UINT(4, fixture.display.texts.size());
    TEST_ASSERT_EQUAL_STRING("Test Hub", fixture.display.texts[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("9.8.7", fixture.display.texts[3].text.c_str());
    assertColor({0x17, 0x15, 0x0F}, fixture.display.texts[1].style.foreground);
    assertColor({0xF4, 0xF2, 0xEC}, fixture.display.texts[1].style.background);
    TEST_ASSERT_EQUAL_UINT8(2, fixture.display.texts[1].style.scale);
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());
}

void test_splash_progresses_in_segments_and_finishes_after_two_seconds() {
    RuntimeFixture fixture;
    fixture.runtime.start();
    const auto initialRectangleCount = fixture.display.rectangles.size();

    (void)fixture.runtime.update(std::chrono::milliseconds(149));
    TEST_ASSERT_EQUAL_UINT(initialRectangleCount, fixture.display.rectangles.size());
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());

    (void)fixture.runtime.update(std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT(initialRectangleCount + 1, fixture.display.rectangles.size());
    assertColor({0x1B, 0x4F, 0xD0}, fixture.display.rectangles.back().color);
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());

    (void)fixture.runtime.update(std::chrono::milliseconds(1649));
    TEST_ASSERT_EQUAL_UINT(initialRectangleCount + 11, fixture.display.rectangles.size());
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());

    (void)fixture.runtime.update(std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL_UINT(initialRectangleCount + 12, fixture.display.rectangles.size());
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());

    (void)fixture.runtime.update(std::chrono::milliseconds(199));
    TEST_ASSERT_FALSE(fixture.runtime.splashFinished());
    (void)fixture.runtime.update(std::chrono::milliseconds(1));
    TEST_ASSERT_TRUE(fixture.runtime.splashFinished());
}

void test_repeated_startup_is_idempotent() {
    RuntimeFixture fixture;
    fixture.runtime.start();
    const auto actionCount = fixture.trace.size();

    fixture.runtime.start();

    TEST_ASSERT_EQUAL_UINT(actionCount, fixture.trace.size());
}

void test_update_before_start_is_safe_and_returns_no_events() {
    RuntimeFixture fixture;

    const auto& events = fixture.runtime.update();

    TEST_ASSERT_EQUAL_UINT(0, events.size());
    TEST_ASSERT_EQUAL_UINT(0, fixture.trace.size());
}

void test_running_update_refreshes_platform_before_polling_and_returns_events() {
    RuntimeFixture fixture;
    fixture.runtime.start();
    fixture.trace.clear();
    fixture.keyboard.nextEvents = {
        {InputEventType::NamedKey, '\0', NamedKey::Enter, {}},
    };

    const auto& events = fixture.runtime.update();

    TEST_ASSERT_EQUAL_UINT(2, fixture.trace.size());
    TEST_ASSERT_EQUAL_STRING("platform.update", fixture.trace[0].c_str());
    TEST_ASSERT_EQUAL_STRING("keyboard.poll", fixture.trace[1].c_str());
    TEST_ASSERT_EQUAL_UINT(1, events.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::Enter),
                            static_cast<unsigned int>(events[0].namedKey));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_startup_draws_branded_splash_with_visible_version);
    RUN_TEST(test_splash_progresses_in_segments_and_finishes_after_two_seconds);
    RUN_TEST(test_repeated_startup_is_idempotent);
    RUN_TEST(test_update_before_start_is_safe_and_returns_no_events);
    RUN_TEST(test_running_update_refreshes_platform_before_polling_and_returns_events);
    return UNITY_END();
}
