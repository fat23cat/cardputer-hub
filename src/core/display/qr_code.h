#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::core {

// A QR Code symbol (ISO/IEC 18004) for short text: byte mode, error
// correction level M, versions 1-6 (21-41 modules), the smallest version that
// fits and the mask with the lowest penalty. Enough for a Wi-Fi join code.
class QrCode {
  public:
    // Version 6 at level M holds 106 bytes.
    static constexpr std::size_t maxTextBytes = 106;

    [[nodiscard]] static std::optional<QrCode> encodeText(std::string_view text);

    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] int mask() const noexcept { return mask_; }
    // True for a dark module. Coordinates outside the symbol are light.
    [[nodiscard]] bool module(int x, int y) const noexcept;

  private:
    QrCode(int version, const std::vector<std::uint8_t>& codewords);

    void setFunction(int x, int y, bool dark);
    void drawFunctionPatterns();
    void drawFormatBits(int mask);
    void drawCodewords(const std::vector<std::uint8_t>& codewords);
    void applyMask(int mask);
    [[nodiscard]] long penalty() const;

    int version_ = 1;
    int size_ = 21;
    int mask_ = 0;
    std::vector<bool> modules_;
    std::vector<bool> function_;
};

// The text a phone camera reads as "join this Wi-Fi network" (WPA/WPA2).
// Backslash, semicolon, comma, colon and quote are escaped.
[[nodiscard]] std::string wifiQrPayload(std::string_view ssid, std::string_view password);

} // namespace cardputer_hub::core
