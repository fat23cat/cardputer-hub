#include "core/lifecycle/build_info.h"
#include "core/lifecycle/system_runtime.h"
#include "core/logging/logger.h"
#include "hardware/cardputer/cardputer_display_adapter.h"
#include "hardware/cardputer/cardputer_keyboard_adapter.h"
#include "hardware/cardputer/cardputer_platform.h"
#include "hardware/cardputer/serial_log_sink.h"

namespace {

cardputer_hub::hardware::CardputerPlatform platform;
cardputer_hub::hardware::CardputerKeyboardAdapter keyboard;
cardputer_hub::hardware::CardputerDisplayAdapter display;
cardputer_hub::hardware::SerialLogSink logSink;
cardputer_hub::core::Logger logger(logSink, cardputer_hub::core::LogLevel::Info);
cardputer_hub::core::SystemRuntime runtime(platform, keyboard, display, logger,
                                           cardputer_hub::core::firmwareBuildInfo());

} // namespace

void setup() { runtime.start(); }

void loop() { (void)runtime.update(); }
