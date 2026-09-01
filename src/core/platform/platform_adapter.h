#pragma once

namespace cardputer_hub::core {

class IPlatformAdapter {
  public:
    virtual ~IPlatformAdapter() = default;
    virtual void begin() = 0;
    virtual void update() = 0;
};

} // namespace cardputer_hub::core
