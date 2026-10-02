#include "../support/ui_capture.h"
#include "core/display/text_layout.h"
#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "../support/fake_nfc_reader.h"
#include "../support/memory_file_storage.h"
#include "apps/nfc/nfc_app.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"
#include "services/nfc/nfc_ndef.h"

using namespace cardputer_hub;
using namespace cardputer_hub::apps;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;
using namespace std::chrono_literals;

namespace {

constexpr char recordHex[] = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

const InputEvent enter{InputEventType::NamedKey, 0, NamedKey::Enter, {}};
const InputEvent backspace{InputEventType::NamedKey, 0, NamedKey::Backspace, {}};
const InputEvent fnDelete{InputEventType::NamedKey, 0, NamedKey::Delete, {}};
const InputEvent fnRight{InputEventType::NamedKey, 0, NamedKey::Right, {}};
const InputEvent plainLeft{InputEventType::PrintableCharacter, ',', {}, {}};
const InputEvent plainRight{InputEventType::PrintableCharacter, '/', {}, {}};

InputEvent key(char character) {
    return {InputEventType::PrintableCharacter, character, NamedKey::Count, {}};
}

class Display final : public IDisplayAdapter {
  public:
    void beginTransition(SlideDirection direction) override { transitions.push_back(direction); }
    void clear(RgbColor color) override {
        capture.clear(color);
        ++frames;
        texts.clear();
    }
    void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                       RgbColor color) override {
        capture.rectangle(position, width, height, color);
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
    }
    void drawText(PixelPosition position, const char* value, TextStyle style) override {
        capture.text(position, value, style);
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + core::textWidth(value, style.scale) <= 240);
        TEST_ASSERT_TRUE(position.y + std::ceil(8 * style.scale) <= 135);
        texts.push_back(value);
    }
    [[nodiscard]] bool shows(const std::string& value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    [[nodiscard]] bool showsPart(const std::string& value) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& text) {
            return text.find(value) != std::string::npos;
        });
    }
    test_support::UiCapture capture;
    std::vector<std::string> texts;
    std::vector<SlideDirection> transitions;
    int frames = 0;
};

class FixedRandom final : public IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = static_cast<std::uint8_t>(0x40 + index);
    }
};

struct Fixture {
    Fixture()
        : nfc(reader, capabilities, nullptr, {150ms, 300ms, 2000ms, 3}), files(card),
          storage(files, capabilities), inventory(nfc, storage, random), app(inventory, display) {}

    void begin() {
        TEST_ASSERT_TRUE(nfc.start());
        app.onActivate();
        app.update({}, 0ms);
    }
    void step(std::chrono::milliseconds elapsed = 150ms) {
        nfc.update(elapsed);
        storage.update(elapsed);
        app.update({}, elapsed);
    }
    void press(const InputEvent& event) { app.update({event}, 0ms); }
    void type(const std::string& text) {
        for (const char character : text)
            press(key(character));
    }
    void runUntil(InventoryScreen target, int limit = 400) {
        for (int index = 0; index < limit && inventory.status().screen != target; ++index)
            step();
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(target),
                                static_cast<unsigned>(inventory.status().screen));
    }
    void storeRecord(const InventoryRecord& record) {
        card.files[InventoryStore::pathFor(record.id)] = bytesOf(*encodeInventoryRecord(record));
    }
    void removeCard() {
        card.currentState = FileStorageState::NotPresent;
        card.refreshedState = FileStorageState::NotPresent;
    }

    FakeNfcReader reader;
    CapabilityRegistry capabilities;
    NfcService nfc;
    MemoryFileStorageAdapter card;
    FileStorage files;
    RemovableStorageService storage;
    FixedRandom random;
    InventoryService inventory;
    Display display;
    NfcApp app;
};

InventoryId recordId() { return *parseInventoryIdHex(recordHex); }

FakeCard inventoryTag(std::uint8_t seed) {
    return makeNtag213WithArea(seed, *encodeType2NdefArea(inventoryTagMessage(recordId()), 144));
}

InventoryRecord longDescription() {
    InventoryRecord record;
    record.id = recordId();
    record.name = "Чемодан";
    record.description = "Зарядка для ноутбука и запасной кабель USB-C\nТёплый свитер\nПаспорт\n"
                         "Аптечка\nНаушники\nКнига «Мастер и Маргарита»\nЗонт\nТапочки";
    record.revision = 3;
    return record;
}

// ---- Registration in the shell ----------------------------------------------------

void test_registry_requires_the_nfc_reader_capability() {
    AppRegistry apps;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppRegistrationResult::Registered),
                            static_cast<unsigned>(apps.registerApp(
                                {nfcAppId, "NFC", "nfc", "nfc", {nfcReaderCapabilityId}})));
    const auto* descriptor = apps.find(nfcAppId);
    TEST_ASSERT_NOT_NULL(descriptor);
    TEST_ASSERT_EQUAL_UINT(1, descriptor->requiredCapabilities.size());
    TEST_ASSERT_EQUAL_STRING("NFC_READER", descriptor->requiredCapabilities[0].c_str());
}

// ---- Waiting ------------------------------------------------------------------------

void test_waiting_screen_is_drawn_once_and_scans_only_while_open() {
    Fixture f;
    f.begin();
    TEST_ASSERT_TRUE(f.nfc.status().scanning);
    TEST_ASSERT_TRUE(f.reader.fieldEnabled);
    f.display.capture.save("nfc_waiting");
    TEST_ASSERT_TRUE(f.display.shows("NFC"));
    TEST_ASSERT_TRUE(f.display.shows("TAP A TAG"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  NEW"));
    TEST_ASSERT_FALSE(f.display.shows("NO SD"));
    const auto frames = f.display.frames;
    for (int index = 0; index < 50; ++index)
        f.step(20ms);
    TEST_ASSERT_EQUAL_INT(frames, f.display.frames);

    f.app.onDeactivate();
    TEST_ASSERT_FALSE(f.nfc.status().scanning);
    TEST_ASSERT_FALSE(f.reader.fieldEnabled);
}

void test_missing_card_is_shown_in_the_header() {
    Fixture f;
    f.removeCard();
    f.begin();
    TEST_ASSERT_TRUE(f.display.shows("NO SD"));
    TEST_ASSERT_TRUE(f.display.shows("TAP A TAG"));
}

// ---- Registration -------------------------------------------------------------------

void test_new_container_is_named_first_then_written_to_any_blank_tag() {
    Fixture f;
    f.begin();
    f.press(enter);
    f.display.capture.save("nfc_name_entry");
    TEST_ASSERT_TRUE(f.display.shows("NEW CONTAINER"));
    TEST_ASSERT_TRUE(f.display.shows("0/32"));
    TEST_ASSERT_TRUE(f.display.shows("ESC  CANCEL"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER  SAVE"));
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("NEW CONTAINER"));
    f.type("Blue bagx");
    f.press(backspace);
    TEST_ASSERT_TRUE(f.display.shows("Blue bag_"));
    TEST_ASSERT_TRUE(f.display.shows("8/32"));
    TEST_ASSERT_TRUE(f.display.shows("24 LEFT"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  SAVE"));

    f.press(enter);
    f.display.capture.save("nfc_awaiting_tag");
    TEST_ASSERT_TRUE(f.display.shows("TAP TAG TO WRITE"));
    TEST_ASSERT_TRUE(f.display.shows("Blue bag"));
    TEST_ASSERT_TRUE(f.display.shows("ANY BLANK NTAG STICKER"));
    TEST_ASSERT_TRUE(f.display.shows("ESC  CANCEL"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());

    f.reader.present(makeNtag213(1));
    f.runUntil(InventoryScreen::Known);
    f.app.update({}, 0ms);
    f.display.capture.save("nfc_registered");
    TEST_ASSERT_TRUE(f.display.shows("Blue bag"));
    TEST_ASSERT_TRUE(f.display.shows("SAVED"));
    TEST_ASSERT_TRUE(f.display.shows("NO DESCRIPTION YET"));
    TEST_ASSERT_TRUE(f.display.shows("WRITE IT IN MAC COMPANION"));
}

void test_the_tag_may_leave_while_the_name_is_typed() {
    Fixture f;
    f.storeRecord(longDescription());
    f.begin();
    f.reader.present(makeNtag213(1));
    f.runUntil(InventoryScreen::Blank);
    TEST_ASSERT_TRUE(f.display.shows("ENTER  REGISTER"));
    f.press(enter);
    f.type("Box");
    f.reader.removeCard();
    f.runUntil(InventoryScreen::Waiting);
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("Box_"));
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("TAP TAG TO WRITE"));

    // A tag that is not blank is refused without being touched.
    f.reader.present(inventoryTag(2));
    for (int index = 0; index < 40 && !f.display.shows("THIS TAG IS NOT BLANK"); ++index)
        f.step();
    TEST_ASSERT_TRUE(f.display.shows("THIS TAG IS NOT BLANK"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());

    // Escape ends the registration and keeps the app open.
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("Чемодан"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());
}

void test_name_entry_is_bounded_and_escape_cancels_it() {
    Fixture f;
    f.begin();
    TEST_ASSERT_FALSE(f.app.handleBack());
    f.press(enter);
    f.type(std::string(40, 'x'));
    TEST_ASSERT_TRUE(f.display.shows("32/32"));
    TEST_ASSERT_TRUE(f.display.shows("0 LEFT"));
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("TAP A TAG"));
}

void test_registration_without_a_card_explains_and_writes_nothing() {
    Fixture f;
    f.removeCard();
    f.begin();
    f.reader.present(makeNtag213(1));
    f.runUntil(InventoryScreen::Blank);
    f.press(enter);
    TEST_ASSERT_FALSE(f.display.shows("NEW CONTAINER"));
    TEST_ASSERT_TRUE(f.display.shows("INSERT MICROSD TO SAVE"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());
}

void test_escape_does_not_abandon_a_tag_write() {
    Fixture f;
    f.begin();
    f.press(enter);
    f.type("Box");
    f.press(enter);
    f.reader.present(makeNtag213(1));
    f.runUntil(InventoryScreen::Writing);
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.runUntil(InventoryScreen::Known);
}

// ---- Known containers ------------------------------------------------------------

void test_known_container_pages_the_cyrillic_description_and_clears_on_removal() {
    Fixture f;
    f.storeRecord(longDescription());
    f.begin();
    f.reader.present(inventoryTag(2));
    f.runUntil(InventoryScreen::Known);
    f.app.update({}, 0ms);
    f.display.capture.save("nfc_known_page1");
    TEST_ASSERT_TRUE(f.display.shows("Чемодан"));
    TEST_ASSERT_TRUE(f.display.shows("1/2"));
    TEST_ASSERT_TRUE(f.display.shows("Зарядка для ноутбука и запасной"));
    TEST_ASSERT_TRUE(f.display.shows("кабель USB-C"));
    TEST_ASSERT_TRUE(f.display.shows("Тёплый свитер"));
    TEST_ASSERT_TRUE(f.display.shows("FN+DEL  ERASE"));
    TEST_ASSERT_FALSE(f.display.shows("Тапочки"));

    f.press(plainLeft);
    TEST_ASSERT_TRUE(f.display.transitions.empty());
    f.press(fnRight);
    f.display.capture.save("nfc_known_page2");
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_TRUE(f.display.shows("Тапочки"));
    f.press(plainRight);
    TEST_ASSERT_EQUAL_UINT(1, f.display.transitions.size());

    f.reader.removeCard();
    f.runUntil(InventoryScreen::Waiting);
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("TAP A TAG"));
    TEST_ASSERT_FALSE(f.display.showsPart("Тапочки"));
    TEST_ASSERT_FALSE(f.display.shows("Чемодан"));
}

void test_erase_asks_first_then_empties_the_tag_and_deletes_the_record() {
    Fixture f;
    f.storeRecord(longDescription());
    f.begin();
    f.reader.present(inventoryTag(2));
    f.runUntil(InventoryScreen::Known);
    f.press(fnDelete);
    f.display.capture.save("nfc_erase_confirm");
    TEST_ASSERT_TRUE(f.display.shows("ERASE THIS TAG?"));
    TEST_ASSERT_TRUE(f.display.shows("ITS RECORD IS DELETED"));
    TEST_ASSERT_TRUE(f.display.shows("ESC  CANCEL"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  ERASE"));
    TEST_ASSERT_TRUE(f.app.handleBack());
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("Чемодан"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());

    f.press(fnDelete);
    f.press(enter);
    f.runUntil(InventoryScreen::Erased);
    f.app.update({}, 0ms);
    TEST_ASSERT_TRUE(f.display.shows("TAG ERASED"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  OK"));
    TEST_ASSERT_FALSE(f.card.text(InventoryStore::pathFor(recordId())).has_value());
    f.press(enter);
    f.runUntil(InventoryScreen::Blank);
}

void test_erase_confirmation_hides_enter_when_a_different_tag_is_present() {
    Fixture f;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    const auto other =
        makeNtag213WithArea(5, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    const auto changed =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("world"), 144));
    f.begin();
    f.reader.present(foreign);
    f.runUntil(InventoryScreen::OtherData);
    f.press(fnDelete);
    f.reader.removeCard();
    for (int index = 0; index < 5; ++index)
        f.step();
    f.reader.present(other);
    f.step();
    TEST_ASSERT_TRUE(f.display.shows("CHECKING TAG; WAIT"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER  ERASE"));
    for (int index = 0;
         index < 50 && f.inventory.status().eraseHint != InventoryEraseHint::DifferentTag; ++index)
        f.step();
    TEST_ASSERT_TRUE(f.display.shows("THIS IS A DIFFERENT TAG"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER  ERASE"));
    f.press(enter);
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryScreen::EraseConfirm),
                            static_cast<unsigned>(f.inventory.status().screen));
    f.reader.removeCard();
    for (int index = 0; index < 5; ++index)
        f.step();
    f.reader.present(changed);
    for (int index = 0;
         index < 50 && f.inventory.status().eraseHint != InventoryEraseHint::TagChanged; ++index)
        f.step();
    TEST_ASSERT_TRUE(f.display.shows("TAG DATA CHANGED"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER  ERASE"));
}

void test_missing_record_is_created_for_the_existing_id_without_the_tag() {
    Fixture f;
    f.begin();
    f.reader.present(inventoryTag(2));
    f.runUntil(InventoryScreen::MissingRecord);
    TEST_ASSERT_TRUE(f.display.shows("NO RECORD FOR THIS TAG"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  CREATE"));
    TEST_ASSERT_TRUE(f.display.shows("FN+DEL  ERASE"));
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("CREATE RECORD"));
    f.reader.removeCard();
    f.runUntil(InventoryScreen::Waiting);
    f.type("Garage");
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("RECORD SAVED"));
    TEST_ASSERT_TRUE(f.display.shows("Garage"));
    TEST_ASSERT_TRUE(f.reader.pageWrites.empty());
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("TAP A TAG"));
}

void test_storage_unavailable_offers_a_retry() {
    Fixture f;
    f.storeRecord(longDescription());
    f.removeCard();
    f.begin();
    f.reader.present(inventoryTag(2));
    f.runUntil(InventoryScreen::StorageUnavailable);
    TEST_ASSERT_TRUE(f.display.shows("MICROSD UNAVAILABLE"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER  RETRY"));
    f.card.refreshedState = FileStorageState::Ready;
    f.press(enter);
    TEST_ASSERT_TRUE(f.display.shows("Чемодан"));
}

void test_nonblank_and_unsupported_tags_show_only_available_actions() {
    struct Case {
        FakeCard card;
        const char* title;
        bool erasable;
    };
    auto locked = makeNtag213(3);
    locked.memory[10] = 0xFF;
    const std::vector<Case> cases{
        {makeNtag213WithArea(1, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144)),
         "TAG HOLDS OTHER DATA", true},
        {locked, "TAG IS LOCKED", false},
        {makeClassic1K(4), "UNSUPPORTED TAG", false},
    };
    for (const auto& item : cases) {
        Fixture f;
        f.begin();
        f.reader.present(item.card);
        for (int index = 0; index < 40; ++index)
            f.step();
        TEST_ASSERT_TRUE(f.display.shows(item.title));
        TEST_ASSERT_FALSE(f.display.showsPart("ENTER"));
        TEST_ASSERT_EQUAL(item.erasable, f.display.showsPart("ERASE"));
        f.press(enter);
        f.press(fnDelete);
        if (item.erasable) {
            TEST_ASSERT_TRUE(f.display.shows("ERASE THIS TAG?"));
            TEST_ASSERT_TRUE(f.display.shows("ITS OTHER DATA IS LOST"));
            TEST_ASSERT_TRUE(f.app.handleBack());
            f.app.update({}, 0ms);
        }
        TEST_ASSERT_TRUE(f.display.shows(item.title));
        TEST_ASSERT_TRUE(f.reader.pageWrites.empty());
    }
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_requires_the_nfc_reader_capability);
    RUN_TEST(test_waiting_screen_is_drawn_once_and_scans_only_while_open);
    RUN_TEST(test_missing_card_is_shown_in_the_header);
    RUN_TEST(test_new_container_is_named_first_then_written_to_any_blank_tag);
    RUN_TEST(test_the_tag_may_leave_while_the_name_is_typed);
    RUN_TEST(test_name_entry_is_bounded_and_escape_cancels_it);
    RUN_TEST(test_registration_without_a_card_explains_and_writes_nothing);
    RUN_TEST(test_escape_does_not_abandon_a_tag_write);
    RUN_TEST(test_known_container_pages_the_cyrillic_description_and_clears_on_removal);
    RUN_TEST(test_erase_asks_first_then_empties_the_tag_and_deletes_the_record);
    RUN_TEST(test_erase_confirmation_hides_enter_when_a_different_tag_is_present);
    RUN_TEST(test_missing_record_is_created_for_the_existing_id_without_the_tag);
    RUN_TEST(test_storage_unavailable_offers_a_retry);
    RUN_TEST(test_nonblank_and_unsupported_tags_show_only_available_actions);
    return UNITY_END();
}
