#include "core/logging/logger.h"

namespace cardputer_hub::core {

Logger::Logger(ILogSink& sink, LogLevel minimumLevel) noexcept
    : sink_(sink), minimumLevel_(minimumLevel) {}

void Logger::log(const LogRecord& record) {
    if (record.level < minimumLevel_) {
        return;
    }
    sink_.write(record);
}

void Logger::debug(const char* component, const char* message) {
    log({LogLevel::Debug, component, message});
}

void Logger::info(const char* component, const char* message) {
    log({LogLevel::Info, component, message});
}

void Logger::warning(const char* component, const char* message) {
    log({LogLevel::Warning, component, message});
}

void Logger::error(const char* component, const char* message) {
    log({LogLevel::Error, component, message});
}

} // namespace cardputer_hub::core
