#include "hardware/cardputer/serial_log_sink.h"

#include <Arduino.h>

namespace cardputer_hub::hardware {
namespace {

const char* levelName(core::LogLevel level) {
    switch (level) {
    case core::LogLevel::Debug:
        return "DEBUG";
    case core::LogLevel::Info:
        return "INFO";
    case core::LogLevel::Warning:
        return "WARNING";
    case core::LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace

void SerialLogSink::write(const core::LogRecord& record) {
    Serial.printf("[%s] %s: %s\n", levelName(record.level), record.component, record.message);
}

} // namespace cardputer_hub::hardware
