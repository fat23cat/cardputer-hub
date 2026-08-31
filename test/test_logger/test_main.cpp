#include <unity.h>

#include <vector>

#include "core/logging/logger.h"

namespace {

using cardputer_hub::core::ILogSink;
using cardputer_hub::core::Logger;
using cardputer_hub::core::LogLevel;
using cardputer_hub::core::LogRecord;

class RecordingLogSink final : public ILogSink {
  public:
    void write(const LogRecord& record) override { records.push_back(record); }

    std::vector<LogRecord> records;
};

void assertThreshold(LogLevel minimum, int expectedRecords) {
    RecordingLogSink sink;
    Logger logger(sink, minimum);

    logger.debug("component", "debug");
    logger.info("component", "info");
    logger.warning("component", "warning");
    logger.error("component", "error");

    TEST_ASSERT_EQUAL_INT(expectedRecords, static_cast<int>(sink.records.size()));
}

} // namespace

void setUp() {}

void tearDown() {}

void test_logger_filters_every_threshold_boundary() {
    assertThreshold(LogLevel::Debug, 4);
    assertThreshold(LogLevel::Info, 3);
    assertThreshold(LogLevel::Warning, 2);
    assertThreshold(LogLevel::Error, 1);
}

void test_logger_delivers_accepted_record_unchanged() {
    RecordingLogSink sink;
    Logger logger(sink, LogLevel::Info);
    const char* component = "lifecycle";
    const char* message = "started";
    const LogRecord record{LogLevel::Warning, component, message};

    logger.log(record);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(LogLevel::Warning),
                            static_cast<unsigned int>(sink.records[0].level));
    TEST_ASSERT_EQUAL_PTR(component, sink.records[0].component);
    TEST_ASSERT_EQUAL_PTR(message, sink.records[0].message);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_logger_filters_every_threshold_boundary);
    RUN_TEST(test_logger_delivers_accepted_record_unchanged);
    return UNITY_END();
}
