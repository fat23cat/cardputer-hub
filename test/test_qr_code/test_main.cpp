#include <unity.h>

#include <string>

#include "core/display/qr_code.h"

using cardputer_hub::core::QrCode;

namespace {

// Reads the 15-bit format information next to the top-left finder.
int formatBits(const QrCode& code) {
    int bits = 0;
    const auto add = [&](int x, int y) { bits = (bits << 1) | (code.module(x, y) ? 1 : 0); };
    // Bits 14..9 sit in column 8 going up from row 0 to row 5 in reverse.
    for (int i = 14; i >= 9; --i)
        add(14 - i, 8);
    add(7, 8);
    add(8, 8);
    add(8, 7);
    for (int i = 5; i >= 0; --i)
        add(8, i);
    return bits;
}

bool finderAt(const QrCode& code, int left, int top) {
    for (int dy = 0; dy < 7; ++dy) {
        for (int dx = 0; dx < 7; ++dx) {
            const int ring = std::max(std::abs(dx - 3), std::abs(dy - 3));
            if (code.module(left + dx, top + dy) != (ring != 2))
                return false;
        }
    }
    return true;
}

// Decoded by a reference QR reader (Chromium's BarcodeDetector) to the
// payload below; any change to the encoder must keep producing a readable code.
constexpr const char* goldenWifiRows[] = {
    "11111110000011100000101111111", "10000010011111010010101000001",
    "10111010111010110011001011101", "10111010101011001111101011101",
    "10111010111011101101001011101", "10000010101001011110101000001",
    "11111110101010101010101111111", "00000000101100100010000000000",
    "10111110011000011100001111100", "01110101010011000110101010110",
    "11010010001111110110111101000", "10000000000001000001011010011",
    "11110110011100110110100111100", "10111101100010001001010110110",
    "00110010100100011100011100100", "11111001110010110001110001000",
    "01100011101010010100000101011", "10000101000001101000100011010",
    "10111111010011110110100110000", "10011000010011010011100010001",
    "10110111001110111110111111100", "00000000100010001001100010100",
    "11111110000110011111101010100", "10000010100110110010100011011",
    "10111010101010001100111111000", "10111010101000100111000011011",
    "10111010100001011001110111110", "10000010000101110000110011010",
    "11111110110000111100011011000",
};

void test_wifi_payload_matches_the_verified_symbol() {
    const auto code = *QrCode::encodeText("WIFI:T:WPA;S:HUB-7F3A;P:k7r2mq9pab3x;;");
    for (int y = 0; y < code.size(); ++y)
        for (int x = 0; x < code.size(); ++x)
            TEST_ASSERT_EQUAL(goldenWifiRows[y][x] == '1', code.module(x, y));
}

void test_wifi_payload_fits_version_three() {
    const auto code = QrCode::encodeText("WIFI:T:WPA;S:HUB-7F3A;P:k7r2mq9pab3x;;");
    TEST_ASSERT_TRUE(code.has_value());
    TEST_ASSERT_EQUAL_INT(29, code->size());
}

void test_smallest_version_is_chosen() {
    TEST_ASSERT_EQUAL_INT(21, QrCode::encodeText("HELLO")->size());
    TEST_ASSERT_EQUAL_INT(25, QrCode::encodeText(std::string(20, 'a'))->size());
    TEST_ASSERT_EQUAL_INT(41, QrCode::encodeText(std::string(100, 'a'))->size());
}

void test_too_long_text_is_refused() {
    TEST_ASSERT_FALSE(QrCode::encodeText(std::string(QrCode::maxTextBytes + 1, 'a')).has_value());
    TEST_ASSERT_TRUE(QrCode::encodeText(std::string(QrCode::maxTextBytes, 'a')).has_value());
}

void test_function_patterns_are_drawn() {
    const auto code = *QrCode::encodeText("WIFI:T:WPA;S:HUB-7F3A;P:k7r2mq9pab3x;;");
    const int size = code.size();
    TEST_ASSERT_TRUE(finderAt(code, 0, 0));
    TEST_ASSERT_TRUE(finderAt(code, size - 7, 0));
    TEST_ASSERT_TRUE(finderAt(code, 0, size - 7));
    for (int i = 8; i < size - 8; ++i) {
        TEST_ASSERT_EQUAL(i % 2 == 0, code.module(i, 6));
        TEST_ASSERT_EQUAL(i % 2 == 0, code.module(6, i));
    }
    // The dark module and the alignment pattern centred at (22, 22).
    TEST_ASSERT_TRUE(code.module(8, size - 8));
    TEST_ASSERT_TRUE(code.module(22, 22));
    TEST_ASSERT_FALSE(code.module(21, 22));
    TEST_ASSERT_TRUE(code.module(20, 22));
    // Outside the symbol everything is light (the quiet zone).
    TEST_ASSERT_FALSE(code.module(-1, 0));
    TEST_ASSERT_FALSE(code.module(size, size));
}

void test_format_information_names_level_m_and_its_mask() {
    const auto code = *QrCode::encodeText("WIFI:T:WPA;S:HUB-7F3A;P:k7r2mq9pab3x;;");
    const int bits = formatBits(code) ^ 0x5412;
    // Level M is 00 in the two high data bits.
    TEST_ASSERT_EQUAL_INT(0, (bits >> 13) & 0x3);
    TEST_ASSERT_EQUAL_INT(code.mask(), (bits >> 10) & 0x7);
    // The ten low bits are the BCH remainder of the five data bits.
    int remainder = bits >> 10;
    for (int i = 0; i < 10; ++i)
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    TEST_ASSERT_EQUAL_INT(remainder & 0x3ff, bits & 0x3ff);
}

void test_encoding_is_deterministic() {
    const auto first = *QrCode::encodeText("WIFI:T:WPA;S:HUB-0001;P:abcdefghijkm;;");
    const auto second = *QrCode::encodeText("WIFI:T:WPA;S:HUB-0001;P:abcdefghijkm;;");
    for (int y = 0; y < first.size(); ++y)
        for (int x = 0; x < first.size(); ++x)
            TEST_ASSERT_EQUAL(first.module(x, y), second.module(x, y));
}

void test_wifi_payload_escapes_special_characters() {
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:HUB-0001;P:abcdefghijkm;;",
        cardputer_hub::core::wifiQrPayload("HUB-0001", "abcdefghijkm").c_str());
    TEST_ASSERT_EQUAL_STRING(R"(WIFI:T:WPA;S:a\;b\,c\:d\\e\"f;P:p\;q;;)",
                             cardputer_hub::core::wifiQrPayload(R"(a;b,c:d\e"f)", "p;q").c_str());
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_payload_fits_version_three);
    RUN_TEST(test_wifi_payload_matches_the_verified_symbol);
    RUN_TEST(test_smallest_version_is_chosen);
    RUN_TEST(test_too_long_text_is_refused);
    RUN_TEST(test_function_patterns_are_drawn);
    RUN_TEST(test_format_information_names_level_m_and_its_mask);
    RUN_TEST(test_encoding_is_deterministic);
    RUN_TEST(test_wifi_payload_escapes_special_characters);
    return UNITY_END();
}
