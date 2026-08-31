#pragma once

#include <cstdint>

namespace cardputer_hub::core {

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warning,
    Error,
};

struct LogRecord {
    LogLevel level;
    const char* component;
    const char* message;
};

class ILogSink {
  public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;
};

class Logger {
  public:
    Logger(ILogSink& sink, LogLevel minimumLevel) noexcept;

    void log(const LogRecord& record);
    void debug(const char* component, const char* message);
    void info(const char* component, const char* message);
    void warning(const char* component, const char* message);
    void error(const char* component, const char* message);

  private:
    ILogSink& sink_;
    LogLevel minimumLevel_;
};

} // namespace cardputer_hub::core
