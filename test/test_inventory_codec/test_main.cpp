#include <unity.h>

#include <string>
#include <vector>

#include "core/display/display_glyphs.h"
#include "core/display/text_layout.h"
#include "core/text/utf8.h"
#include "hardware/cardputer/assets/system_font_extension.h"
#include "services/inventory/inventory_id.h"
#include "services/inventory/inventory_record.h"
#include "services/nfc/nfc_ndef.h"

using namespace cardputer_hub::core;
using namespace cardputer_hub::services;

namespace {

constexpr char sampleHex[] = "00112233445566778899aabbccddeeff";

InventoryId sampleId() { return *parseInventoryIdHex(sampleHex); }

class SequenceRandom final : public IRandomSource {
  public:
    explicit SequenceRandom(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    void fill(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = bytes_[next_++ % bytes_.size()];
    }

  private:
    std::vector<std::uint8_t> bytes_;
    std::size_t next_ = 0;
};

void assertError(InventoryRecordError expected, std::string_view json) {
    const auto decoded = decodeInventoryRecord(json);
    TEST_ASSERT_FALSE(decoded.record.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(decoded.error));
}

std::string recordJson(const std::string& fields) {
    return "{\"schema\":2,\"id\":\"" + std::string(sampleHex) + "\"," + fields + "}";
}

std::vector<std::uint8_t> window(std::vector<std::uint8_t> dataArea,
                                 std::vector<std::uint8_t> header = {0x04, 0x51, 0x00, 0x00, 0xE1,
                                                                     0x10, 0x12, 0x00}) {
    header.insert(header.end(), dataArea.begin(), dataArea.end());
    header.resize(nfcInspectBytes, 0);
    return header;
}

// ---- Inventory ID and tag payload ---------------------------------------------

void test_inventory_id_is_32_lowercase_hex_and_never_zero() {
    TEST_ASSERT_EQUAL_STRING(sampleHex, inventoryIdHex(sampleId()).c_str());
    TEST_ASSERT_FALSE(parseInventoryIdHex("00112233445566778899AABBCCDDEEFF").has_value());
    TEST_ASSERT_FALSE(parseInventoryIdHex("0011").has_value());
    TEST_ASSERT_FALSE(parseInventoryIdHex("00000000000000000000000000000000").has_value());
    TEST_ASSERT_FALSE(parseInventoryIdHex("00112233445566778899aabbccddeefg").has_value());

    // An all-zero draw is discarded rather than used.
    std::vector<std::uint8_t> bytes(16, 0);
    for (std::uint8_t value = 1; value <= 16; ++value)
        bytes.push_back(value);
    SequenceRandom random(bytes);
    const auto id = generateInventoryId(random);
    TEST_ASSERT_EQUAL_UINT8(1, id[0]);
    TEST_ASSERT_EQUAL_UINT8(16, id[15]);
}

void test_tag_message_round_trips_and_fits_an_ntag213() {
    const auto message = inventoryTagMessage(sampleId());
    TEST_ASSERT_EQUAL_UINT(46, message.size());
    TEST_ASSERT_EQUAL_STRING((std::string("CHINV1:") + sampleHex).c_str(),
                             decodeNdefTextMessage(message)->c_str());
    const auto parsed = parseInventoryTag(message);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::Inventory),
                            static_cast<unsigned>(parsed.kind));
    TEST_ASSERT_TRUE(parsed.id == sampleId());
    const auto area = encodeType2NdefArea(message, 144);
    TEST_ASSERT_TRUE(area.has_value());
    TEST_ASSERT_EQUAL_UINT(52, area->size());
    // The whole area lies inside the inspection window.
    const auto inspected = inspectType2Window(window(*area).data(), nfcInspectBytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Message),
                            static_cast<unsigned>(inspected.content));
    TEST_ASSERT_TRUE(inspected.message == message);
    TEST_ASSERT_FALSE(encodeType2NdefArea(message, 48).has_value());
}

void test_foreign_and_future_tag_payloads_are_not_inventory_tags() {
    const auto kind = [](const std::vector<std::uint8_t>& message) {
        return static_cast<unsigned>(parseInventoryTag(message).kind);
    };
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::NotInventory),
                            kind(encodeNdefTextMessage("hello")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::UnsupportedVersion),
                            kind(encodeNdefTextMessage(std::string("CHINV2:") + sampleHex)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::UnsupportedVersion),
                            kind(encodeNdefTextMessage("CHINV1:00112233")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::UnsupportedVersion),
                            kind(encodeNdefTextMessage(std::string("CHINV1:") + sampleHex + "00")));
    // A URI record, and two records, are not inventory tags.
    const std::vector<std::uint8_t> uri{0xD1, 0x01, 0x04, 'U', 0x01, 'a', '.', 'b'};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::NotInventory), kind(uri));
    auto twoRecords = inventoryTagMessage(sampleId());
    twoRecords[0] &= static_cast<std::uint8_t>(~0x40U);
    twoRecords.insert(twoRecords.end(), uri.begin(), uri.end());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryTagKind::NotInventory),
                            kind(twoRecords));
}

// ---- Type 2 inspection --------------------------------------------------------

void test_type2_inspection_classifies_blank_formats() {
    for (const auto& area :
         {std::vector<std::uint8_t>{0x03, 0x00, 0xFE}, std::vector<std::uint8_t>{0xFE},
          std::vector<std::uint8_t>{0x00, 0x00, 0x03, 0x00, 0x00, 0xFE}}) {
        const auto inspected = inspectType2Window(window(area).data(), nfcInspectBytes);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Blank),
                                static_cast<unsigned>(inspected.content));
        TEST_ASSERT_FALSE(inspected.incomplete);
        TEST_ASSERT_TRUE(inspected.writable);
        TEST_ASSERT_EQUAL_UINT16(144, inspected.capacity);
    }
}

void test_blank_needs_proof_from_the_whole_data_area() {
    // NULL bytes to the end of a partial window prove nothing: read further.
    const auto partial = window({});
    const auto undecided = inspectType2Window(partial.data(), partial.size());
    TEST_ASSERT_TRUE(undecided.incomplete);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::None),
                            static_cast<unsigned>(undecided.content));
    // The same NULL bytes over the whole data area are a factory-zeroed tag.
    std::vector<std::uint8_t> whole = window({});
    whole.resize(8 + 144, 0);
    const auto zeroed = inspectType2Window(whole.data(), whole.size());
    TEST_ASSERT_FALSE(zeroed.incomplete);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Blank),
                            static_cast<unsigned>(zeroed.content));
    // A message after NULL bytes, or after an empty NDEF TLV, is existing data.
    whole[8 + 100] = 0x03;
    whole[8 + 101] = 0x02;
    whole[8 + 102] = 0xD0;
    whole[8 + 103] = 0x00;
    whole[8 + 104] = 0xFE;
    const auto later = inspectType2Window(whole.data(), whole.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Message),
                            static_cast<unsigned>(later.content));
    whole[8] = 0x03;
    const auto hidden = inspectType2Window(whole.data(), whole.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::OtherData),
                            static_cast<unsigned>(hidden.content));
    TEST_ASSERT_TRUE(hidden.reserved);
}

void test_control_tlvs_are_never_treated_as_blank() {
    // Lock and memory control TLVs describe reserved areas that a write from
    // the first user page would destroy.
    for (const auto& area :
         {std::vector<std::uint8_t>{0x01, 0x03, 0xA0, 0x10, 0x44, 0x03, 0x00, 0xFE},
          std::vector<std::uint8_t>{0x02, 0x03, 0xA0, 0x10, 0x44, 0xFE}}) {
        const auto inspected = inspectType2Window(window(area).data(), nfcInspectBytes);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::OtherData),
                                static_cast<unsigned>(inspected.content));
    }
    // A message after them is still read.
    std::vector<std::uint8_t> area{0x01, 0x03, 0xA0, 0x10, 0x44};
    const auto message = *encodeType2NdefArea(inventoryTagMessage(sampleId()), 144);
    area.insert(area.end(), message.begin(), message.end());
    const auto bytes = window(area);
    const auto inspected = inspectType2Window(bytes.data(), bytes.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Message),
                            static_cast<unsigned>(inspected.content));
}

void test_data_after_a_message_is_not_erasable_content() {
    const auto message = inventoryTagMessage(sampleId());
    for (const auto& trailing : {std::vector<std::uint8_t>{0x01, 0x03, 0xA0, 0x10, 0x44, 0xFE},
                                 std::vector<std::uint8_t>{0x02, 0x03, 0xA0, 0x10, 0x44, 0xFE},
                                 std::vector<std::uint8_t>{0xFD, 0x01, 0x42, 0xFE},
                                 std::vector<std::uint8_t>{0x03, 0x01, 0x42, 0xFE}}) {
        std::vector<std::uint8_t> area{0x03, static_cast<std::uint8_t>(message.size())};
        area.insert(area.end(), message.begin(), message.end());
        area.resize(64, 0); // The hidden TLV starts beyond the first read window.
        area.insert(area.end(), trailing.begin(), trailing.end());
        auto bytes = window({});
        bytes.resize(8); // Keep the header; append beyond the helper's first window.
        bytes.insert(bytes.end(), area.begin(), area.end());
        const auto first = inspectType2Window(bytes.data(), nfcInspectBytes);
        TEST_ASSERT_TRUE(first.incomplete);
        bytes.resize(8 + 144, 0);
        TEST_ASSERT_EQUAL_UINT8(trailing[0], bytes[8 + 64]);
        const auto inspected = inspectType2Window(bytes.data(), bytes.size());
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<unsigned>(NfcTagContent::OtherData),
                                        static_cast<unsigned>(inspected.content),
                                        trailing[0] == 0x01   ? "lock"
                                        : trailing[0] == 0x02 ? "memory"
                                        : trailing[0] == 0x03 ? "second NDEF"
                                                              : "proprietary");
        TEST_ASSERT_TRUE(inspected.reserved);
    }
}

void test_type2_inspection_refuses_what_it_cannot_own() {
    const auto content = [](const std::vector<std::uint8_t>& bytes) {
        return static_cast<unsigned>(inspectType2Window(bytes.data(), bytes.size()).content);
    };
    // Proprietary TLV; a message longer than the first window needs more
    // bytes, and one longer than the whole data area is never truncated.
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::OtherData),
                            content(window({0xFD, 0x02, 0x00, 0x00})));
    const auto partial = window({0x03, 0x80, 0xD1});
    TEST_ASSERT_TRUE(inspectType2Window(partial.data(), partial.size()).incomplete);
    auto whole = window({0x03, 0xA0, 0xD1});
    whole.resize(8 + 144, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::OtherData), content(whole));
    // No capability container.
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::NotFormatted),
                            content(window({0x03, 0x00}, {0, 0, 0, 0, 0, 0, 0, 0})));
    // Static lock bits make a blank tag read-only.
    const auto locked = window({0x03, 0x00, 0xFE}, {0x04, 0x51, 0xF8, 0x00, 0xE1, 0x10, 0x12, 0});
    const auto inspected = inspectType2Window(locked.data(), locked.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Blank),
                            static_cast<unsigned>(inspected.content));
    TEST_ASSERT_FALSE(inspected.writable);
}

// ---- Record JSON ----------------------------------------------------------------

void test_record_round_trips_cyrillic_description_with_line_breaks() {
    InventoryRecord record;
    record.id = sampleId();
    record.name = "Чемодан «Синий»";
    record.description = "Штаны, шорты\nНоски\n\n\"quoted\" \\ path";
    record.revision = 7;
    const auto json = encodeInventoryRecord(record);
    TEST_ASSERT_TRUE(json.has_value());
    // Canonical form: line breaks escaped, no raw control characters.
    TEST_ASSERT_TRUE(json->find('\n') == std::string::npos);
    TEST_ASSERT_TRUE(json->find("\"schema\":2") != std::string::npos);
    TEST_ASSERT_TRUE(json->find("Штаны, шорты\\nНоски") != std::string::npos);
    const auto decoded = decodeInventoryRecord(*json);
    TEST_ASSERT_TRUE(decoded.record.has_value());
    TEST_ASSERT_EQUAL_STRING(record.name.c_str(), decoded.record->name.c_str());
    TEST_ASSERT_EQUAL_STRING(record.description.c_str(), decoded.record->description.c_str());
    TEST_ASSERT_EQUAL_UINT32(7, decoded.record->revision);
    TEST_ASSERT_TRUE(decoded.record->id == record.id);
}

void test_record_accepts_escapes_whitespace_and_any_key_order() {
    const std::string json =
        " {\n \"description\" : \"\\u0416\\u0443\\u043a\\n\\ud83d\\udce6 box\","
        "\"revision\":3, \"name\":\"A\\/B\",\"id\":\"" +
        std::string(sampleHex) + "\",\"schema\":2 }\n";
    const auto decoded = decodeInventoryRecord(json);
    TEST_ASSERT_TRUE(decoded.record.has_value());
    TEST_ASSERT_EQUAL_STRING("Жук\n\xF0\x9F\x93\xA6 box", decoded.record->description.c_str());
    TEST_ASSERT_EQUAL_STRING("A/B", decoded.record->name.c_str());
}

void test_inventory_rejects_invalid_utf8_json_and_bounds() {
    const std::string valid = "\"revision\":1,\"name\":\"Box\",\"description\":\"\"";
    TEST_ASSERT_TRUE(decodeInventoryRecord(recordJson(valid)).record.has_value());

    assertError(InventoryRecordError::InvalidUtf8,
                recordJson("\"revision\":1,\"name\":\"B\xC3\x28x\",\"description\":\"\""));
    assertError(InventoryRecordError::InvalidUtf8,
                recordJson("\"revision\":1,\"name\":\"Box\",\"description\":\"\xED\xA0\x80\""));
    assertError(InventoryRecordError::InvalidUtf8,
                recordJson("\"revision\":1,\"name\":\"\\udc00\",\"description\":\"\""));
    assertError(InventoryRecordError::Malformed, recordJson(valid) + "x");
    assertError(InventoryRecordError::Malformed, "{\"schema\":2,");
    assertError(InventoryRecordError::Malformed,
                recordJson("\"revision\":01,\"name\":\"Box\",\"description\":\"\""));
    assertError(InventoryRecordError::Malformed,
                recordJson("\"revision\":4294967296,\"name\":\"Box\",\"description\":\"\""));
    assertError(InventoryRecordError::Malformed,
                recordJson("\"revision\":1,\"name\":\"Box\",\"description\":[]"));
    assertError(InventoryRecordError::DuplicateField, recordJson(valid + ",\"name\":\"Other\""));
    // Schema 1's item list is not part of schema 2.
    assertError(InventoryRecordError::UnknownField, recordJson(valid + ",\"items\":[]"));
    assertError(InventoryRecordError::MissingField, recordJson("\"revision\":1,\"name\":\"Box\""));
    assertError(InventoryRecordError::WrongSchema,
                "{\"schema\":1,\"id\":\"" + std::string(sampleHex) + "\"," + valid + "}");
    assertError(InventoryRecordError::BadId,
                "{\"schema\":2,\"id\":\"00112233445566778899AABBCCDDEEFF\"," + valid + "}");
    assertError(InventoryRecordError::BadRevision,
                recordJson("\"revision\":0,\"name\":\"Box\",\"description\":\"\""));
    assertError(InventoryRecordError::BadName,
                recordJson("\"revision\":1,\"name\":\"\",\"description\":\"\""));
    assertError(InventoryRecordError::BadName,
                recordJson("\"revision\":1,\"name\":\"Line\\nbreak\",\"description\":\"\""));
    assertError(InventoryRecordError::BadName,
                recordJson("\"revision\":1,\"name\":\"" + std::string(33, 'x') +
                           "\",\"description\":\"\""));
    for (const auto* description :
         {"\\nLeading break", "Trailing space ", "Tab\\there", "Carriage\\rreturn"}) {
        assertError(InventoryRecordError::BadDescription,
                    recordJson(std::string("\"revision\":1,\"name\":\"Box\",\"description\":\"") +
                               description + "\""));
    }
    std::string longest;
    for (int index = 0; index < 900; ++index)
        longest += "ж";
    TEST_ASSERT_TRUE(decodeInventoryRecord(recordJson("\"revision\":1,\"name\":\"Box\","
                                                      "\"description\":\"" +
                                                      longest + "\""))
                         .record.has_value());
    assertError(InventoryRecordError::BadDescription,
                recordJson("\"revision\":1,\"name\":\"Box\",\"description\":\"" + longest + "ж\""));
    assertError(InventoryRecordError::TooLarge, std::string(4097, ' '));
}

void test_the_longest_description_fits_the_record_bound() {
    InventoryRecord record;
    record.id = sampleId();
    record.name = std::string(32, 'x');
    // 900 four-byte code points, the worst case for the byte bound.
    for (int index = 0; index < 900; ++index)
        record.description += "\xF0\x9F\x93\xA6";
    const auto json = encodeInventoryRecord(record);
    TEST_ASSERT_TRUE(json.has_value());
    TEST_ASSERT_TRUE(json->size() <= inventoryMaxRecordBytes);
}

// ---- UTF-8 text on the display --------------------------------------------------

void test_utf8_decoding_is_strict() {
    TEST_ASSERT_TRUE(isValidUtf8("Ёжик"));
    TEST_ASSERT_FALSE(isValidUtf8("\xC0\xAF"));         // overlong
    TEST_ASSERT_FALSE(isValidUtf8("\xED\xB0\x80"));     // surrogate
    TEST_ASSERT_FALSE(isValidUtf8("\xF4\x90\x80\x80")); // above U+10FFFF
    TEST_ASSERT_FALSE(isValidUtf8("\xD0"));             // truncated
    TEST_ASSERT_EQUAL_UINT(4, utf8Length("Ёжик"));
    TEST_ASSERT_EQUAL_STRING("Ёж", utf8Prefix("Ёжик", 2).c_str());
}

void test_display_glyphs_cover_russian_and_typography() {
    const auto glyphs = displayGlyphs("АяЁё «A»—№\xF0\x9F\x93\xA6\xFF");
    const std::vector<std::uint8_t> expected{0x80, 0xBF, 0xC0, 0xC1, ' ',  0xC2,
                                             'A',  0xC3, '-',  0xC4, 0x7F, 0x7F};
    TEST_ASSERT_EQUAL_UINT(expected.size(), glyphs.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        TEST_ASSERT_EQUAL_UINT8(expected[index], static_cast<std::uint8_t>(glyphs[index]));
    TEST_ASSERT_EQUAL_STRING("PLAIN ASCII", displayGlyphs("PLAIN ASCII").c_str());
}

void test_every_display_glyph_has_a_font_glyph() {
    namespace assets = cardputer_hub::hardware::assets;
    std::vector<bool> defined(256, false);
    for (const auto& glyph : assets::systemFontExtension) {
        TEST_ASSERT_FALSE(defined[glyph.code]);
        defined[glyph.code] = true;
        if (glyph.latin != 0) {
            TEST_ASSERT_TRUE(glyph.latin >= 'A' && glyph.latin <= 'z');
            continue;
        }
        for (const auto* row : glyph.rows) {
            TEST_ASSERT_NOT_NULL(row);
            TEST_ASSERT_EQUAL_UINT(5, std::char_traits<char>::length(row));
            for (int column = 0; column < 5; ++column)
                TEST_ASSERT_TRUE(row[column] == '#' || row[column] == '.');
        }
    }
    TEST_ASSERT_TRUE(defined[display_glyph::unsupported]);
    for (unsigned code = 0x80; code <= display_glyph::lastDefined; ++code)
        TEST_ASSERT_TRUE(defined[code]);
    for (unsigned code = display_glyph::lastDefined + 1; code < 256; ++code)
        TEST_ASSERT_FALSE(defined[code]);
}

void test_layout_measures_and_wraps_by_code_point() {
    TEST_ASSERT_EQUAL_INT(systemTextWidth("ABCD"), systemTextWidth("Ёжик"));
    TEST_ASSERT_EQUAL_STRING("Тёплый...", fitSystemText("Тёплый свитер", 9 * 7.2f + 1).c_str());

    const auto lines = wrapText("Зарядка для ноутбука и мышь", 12);
    TEST_ASSERT_EQUAL_UINT(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("Зарядка для", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("ноутбука и", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("мышь", lines[2].c_str());
    for (const auto& line : wrapText("Электрокардиограф переносной", 8))
        TEST_ASSERT_TRUE(utf8Length(line) <= 8);
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_inventory_id_is_32_lowercase_hex_and_never_zero);
    RUN_TEST(test_tag_message_round_trips_and_fits_an_ntag213);
    RUN_TEST(test_foreign_and_future_tag_payloads_are_not_inventory_tags);
    RUN_TEST(test_type2_inspection_classifies_blank_formats);
    RUN_TEST(test_blank_needs_proof_from_the_whole_data_area);
    RUN_TEST(test_control_tlvs_are_never_treated_as_blank);
    RUN_TEST(test_data_after_a_message_is_not_erasable_content);
    RUN_TEST(test_type2_inspection_refuses_what_it_cannot_own);
    RUN_TEST(test_record_round_trips_cyrillic_description_with_line_breaks);
    RUN_TEST(test_record_accepts_escapes_whitespace_and_any_key_order);
    RUN_TEST(test_inventory_rejects_invalid_utf8_json_and_bounds);
    RUN_TEST(test_the_longest_description_fits_the_record_bound);
    RUN_TEST(test_utf8_decoding_is_strict);
    RUN_TEST(test_display_glyphs_cover_russian_and_typography);
    RUN_TEST(test_every_display_glyph_has_a_font_glyph);
    RUN_TEST(test_layout_measures_and_wraps_by_code_point);
    return UNITY_END();
}
