#pragma once

namespace cardputer_hub::hardware {

class CardputerPlatform {
  public:
    void begin();
    void printBootMessage(const char* name, const char* version, const char* commit,
                          const char* buildType);
    void update();
};

} // namespace cardputer_hub::hardware
