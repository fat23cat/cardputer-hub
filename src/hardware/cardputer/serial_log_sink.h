#pragma once

#include "core/logging/logger.h"

namespace cardputer_hub::hardware {

class SerialLogSink final : public core::ILogSink {
  public:
    void write(const core::LogRecord& record) override;
};

} // namespace cardputer_hub::hardware
