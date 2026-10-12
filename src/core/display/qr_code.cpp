#include "core/display/qr_code.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace cardputer_hub::core {
namespace {

constexpr int minimumVersion = 1;
constexpr int maximumVersion = 6;
// Level M, indexed by version.
constexpr std::array<int, maximumVersion + 1> eccPerBlock = {0, 10, 16, 26, 18, 24, 16};
constexpr std::array<int, maximumVersion + 1> blocks = {0, 1, 1, 1, 2, 2, 4};
// Format information: level M is 0b00.
constexpr int levelBits = 0;

int rawDataModules(int version) {
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int alignments = version / 7 + 2;
        result -= (25 * alignments - 10) * alignments - 55;
    }
    return result;
}

int dataCodewords(int version) {
    return rawDataModules(version) / 8 - eccPerBlock[version] * blocks[version];
}

std::uint8_t multiply(std::uint8_t x, std::uint8_t y) {
    int z = 0;
    for (int bit = 7; bit >= 0; --bit) {
        z = (z << 1) ^ ((z >> 7) * 0x11d);
        z ^= ((y >> bit) & 1) * x;
    }
    return static_cast<std::uint8_t>(z);
}

std::vector<std::uint8_t> divisor(int degree) {
    std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
    result.back() = 1;
    std::uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (std::size_t j = 0; j < result.size(); ++j) {
            result[j] = multiply(result[j], root);
            if (j + 1 < result.size())
                result[j] ^= result[j + 1];
        }
        root = multiply(root, 0x02);
    }
    return result;
}

std::vector<std::uint8_t> remainder(const std::vector<std::uint8_t>& data,
                                    const std::vector<std::uint8_t>& generator) {
    std::vector<std::uint8_t> result(generator.size(), 0);
    for (const auto byte : data) {
        const std::uint8_t factor = byte ^ result.front();
        result.erase(result.begin());
        result.push_back(0);
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] ^= multiply(generator[i], factor);
    }
    return result;
}

// Data codewords followed by the error correction, interleaved by block.
std::vector<std::uint8_t> withErrorCorrection(const std::vector<std::uint8_t>& data, int version) {
    const int blockCount = blocks[version];
    const int ecc = eccPerBlock[version];
    const int raw = rawDataModules(version) / 8;
    const int shortBlocks = blockCount - raw % blockCount;
    const int shortLength = raw / blockCount;
    const auto generator = divisor(ecc);

    std::vector<std::vector<std::uint8_t>> dataBlocks;
    std::vector<std::vector<std::uint8_t>> eccBlocks;
    std::size_t position = 0;
    for (int block = 0; block < blockCount; ++block) {
        const auto length =
            static_cast<std::size_t>(shortLength - ecc + (block < shortBlocks ? 0 : 1));
        std::vector<std::uint8_t> part(data.begin() + static_cast<std::ptrdiff_t>(position),
                                       data.begin() +
                                           static_cast<std::ptrdiff_t>(position + length));
        position += length;
        eccBlocks.push_back(remainder(part, generator));
        dataBlocks.push_back(std::move(part));
    }
    std::vector<std::uint8_t> result;
    const auto longest = dataBlocks.back().size();
    for (std::size_t i = 0; i < longest; ++i)
        for (const auto& block : dataBlocks)
            if (i < block.size())
                result.push_back(block[i]);
    for (std::size_t i = 0; i < static_cast<std::size_t>(ecc); ++i)
        for (const auto& block : eccBlocks)
            result.push_back(block[i]);
    return result;
}

bool maskBit(int mask, int x, int y) {
    switch (mask) {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    case 3:
        return (x + y) % 3 == 0;
    case 4:
        return (x / 3 + y / 2) % 2 == 0;
    case 5:
        return x * y % 2 + x * y % 3 == 0;
    case 6:
        return (x * y % 2 + x * y % 3) % 2 == 0;
    default:
        return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

} // namespace

std::optional<QrCode> QrCode::encodeText(std::string_view text) {
    if (text.size() > maxTextBytes)
        return std::nullopt;
    int version = minimumVersion;
    // Mode (4 bits), count (8 bits for versions 1-9), then the bytes.
    const auto neededBits = static_cast<int>(12 + 8 * text.size());
    while (version <= maximumVersion && dataCodewords(version) * 8 < neededBits)
        ++version;
    if (version > maximumVersion)
        return std::nullopt;

    const int capacityBits = dataCodewords(version) * 8;
    std::vector<bool> bits;
    const auto append = [&bits](unsigned value, int length) {
        for (int bit = length - 1; bit >= 0; --bit)
            bits.push_back(((value >> bit) & 1U) != 0);
    };
    append(0x4, 4);
    append(static_cast<unsigned>(text.size()), 8);
    for (const char character : text)
        append(static_cast<unsigned char>(character), 8);
    append(0, std::min(4, capacityBits - static_cast<int>(bits.size())));
    append(0, (8 - static_cast<int>(bits.size() % 8)) % 8);
    for (unsigned pad = 0xec; static_cast<int>(bits.size()) < capacityBits; pad ^= 0xec ^ 0x11)
        append(pad, 8);

    std::vector<std::uint8_t> data(bits.size() / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            data[i / 8] = static_cast<std::uint8_t>(data[i / 8] | (0x80 >> (i % 8)));
    return QrCode(version, withErrorCorrection(data, version));
}

QrCode::QrCode(int version, const std::vector<std::uint8_t>& codewords)
    : version_(version), size_(version * 4 + 17),
      modules_(static_cast<std::size_t>(size_ * size_), false),
      function_(static_cast<std::size_t>(size_ * size_), false) {
    drawFunctionPatterns();
    drawCodewords(codewords);

    long best = std::numeric_limits<long>::max();
    for (int candidate = 0; candidate < 8; ++candidate) {
        applyMask(candidate);
        drawFormatBits(candidate);
        const long score = penalty();
        if (score < best) {
            best = score;
            mask_ = candidate;
        }
        applyMask(candidate); // XOR again undoes it
    }
    applyMask(mask_);
    drawFormatBits(mask_);
}

bool QrCode::module(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= size_ || y >= size_)
        return false;
    return modules_[static_cast<std::size_t>(y * size_ + x)];
}

void QrCode::setFunction(int x, int y, bool dark) {
    const auto index = static_cast<std::size_t>(y * size_ + x);
    modules_[index] = dark;
    function_[index] = true;
}

void QrCode::drawFunctionPatterns() {
    for (int i = 0; i < size_; ++i) {
        setFunction(6, i, i % 2 == 0);
        setFunction(i, 6, i % 2 == 0);
    }
    for (const auto& [centerX, centerY] :
         {std::pair{3, 3}, std::pair{size_ - 4, 3}, std::pair{3, size_ - 4}}) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = centerX + dx;
                const int y = centerY + dy;
                const int distance = std::max(std::abs(dx), std::abs(dy));
                if (x >= 0 && y >= 0 && x < size_ && y < size_)
                    setFunction(x, y, distance != 2 && distance != 4);
            }
        }
    }
    // Versions 2-6 have one alignment pattern, near the bottom-right corner.
    if (version_ >= 2) {
        const int center = size_ - 7;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                setFunction(center + dx, center + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    // Reserve the format areas; the real bits are drawn with the mask.
    drawFormatBits(0);
}

void QrCode::drawFormatBits(int mask) {
    const int data = (levelBits << 3) | mask;
    int remainder = data;
    for (int i = 0; i < 10; ++i)
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    const int bits = ((data << 10) | (remainder & 0x3ff)) ^ 0x5412;
    const auto bit = [bits](int index) { return ((bits >> index) & 1) != 0; };

    for (int i = 0; i <= 5; ++i)
        setFunction(8, i, bit(i));
    setFunction(8, 7, bit(6));
    setFunction(8, 8, bit(7));
    setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; ++i)
        setFunction(14 - i, 8, bit(i));

    for (int i = 0; i < 8; ++i)
        setFunction(size_ - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i)
        setFunction(8, size_ - 15 + i, bit(i));
    setFunction(8, size_ - 8, true);
}

void QrCode::drawCodewords(const std::vector<std::uint8_t>& codewords) {
    std::size_t index = 0;
    const std::size_t total = codewords.size() * 8;
    for (int right = size_ - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vertical = 0; vertical < size_; ++vertical) {
            for (int column = 0; column < 2; ++column) {
                const int x = right - column;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? size_ - 1 - vertical : vertical;
                const auto cell = static_cast<std::size_t>(y * size_ + x);
                if (function_[cell] || index >= total)
                    continue;
                modules_[cell] = ((codewords[index / 8] >> (7 - index % 8)) & 1) != 0;
                ++index;
            }
        }
    }
}

void QrCode::applyMask(int mask) {
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) {
            const auto cell = static_cast<std::size_t>(y * size_ + x);
            if (!function_[cell] && maskBit(mask, x, y))
                modules_[cell] = !modules_[cell];
        }
    }
}

long QrCode::penalty() const {
    long result = 0;
    // Runs of five or more equal modules in a row or column.
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < size_; ++a) {
            int run = 0;
            bool color = false;
            for (int b = 0; b < size_; ++b) {
                const bool dark = pass == 0 ? module(b, a) : module(a, b);
                if (b == 0 || dark != color) {
                    color = dark;
                    run = 1;
                } else if (++run == 5) {
                    result += 3;
                } else if (run > 5) {
                    ++result;
                }
            }
        }
    }
    // 2x2 blocks of one color.
    for (int y = 0; y + 1 < size_; ++y)
        for (int x = 0; x + 1 < size_; ++x) {
            const bool dark = module(x, y);
            if (dark == module(x + 1, y) && dark == module(x, y + 1) &&
                dark == module(x + 1, y + 1))
                result += 3;
        }
    // Finder-like 1:1:3:1:1 patterns with four light modules on one side.
    static constexpr std::array<bool, 11> patternA = {true, false, true,  true,  true, false,
                                                      true, false, false, false, false};
    static constexpr std::array<bool, 11> patternB = {false, false, false, false, true, false,
                                                      true,  true,  true,  false, true};
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < size_; ++a) {
            for (int b = 0; b + 11 <= size_; ++b) {
                bool matchA = true;
                bool matchB = true;
                for (int i = 0; i < 11; ++i) {
                    const bool dark = pass == 0 ? module(b + i, a) : module(a, b + i);
                    matchA = matchA && dark == patternA[static_cast<std::size_t>(i)];
                    matchB = matchB && dark == patternB[static_cast<std::size_t>(i)];
                }
                if (matchA || matchB)
                    result += 40;
            }
        }
    }
    // Balance of dark and light modules.
    int dark = 0;
    for (const bool value : modules_)
        dark += value ? 1 : 0;
    const int total = size_ * size_;
    const int deviation = std::abs(dark * 20 - total * 10);
    result += static_cast<long>((deviation + total - 1) / total - 1) * 10;
    return result;
}

std::string wifiQrPayload(std::string_view ssid, std::string_view password) {
    const auto escaped = [](std::string_view text) {
        std::string result;
        for (const char character : text) {
            if (character == '\\' || character == ';' || character == ',' || character == ':' ||
                character == '"')
                result.push_back('\\');
            result.push_back(character);
        }
        return result;
    };
    return "WIFI:T:WPA;S:" + escaped(ssid) + ";P:" + escaped(password) + ";;";
}

} // namespace cardputer_hub::core
