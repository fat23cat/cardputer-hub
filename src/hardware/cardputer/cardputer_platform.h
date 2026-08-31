#pragma once

#include "core/platform/platform_adapter.h"

namespace cardputer_hub::hardware {

class CardputerPlatform final : public core::IPlatformAdapter {
  public:
    void begin() override;
    void update() override;
};

} // namespace cardputer_hub::hardware
