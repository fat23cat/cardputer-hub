#include <unity.h>

#include <chrono>
#include <string>
#include <vector>

#include "../support/fake_nfc_reader.h"
#include "../support/memory_file_storage.h"
#include "core/capabilities/capability_registry.h"
#include "services/inventory/inventory_service.h"
#include "services/nfc/nfc_ndef.h"

using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;
using namespace std::chrono_literals;

namespace {

constexpr char existingHex[] = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

class CountingRandom final : public IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override {
        ++draws;
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = static_cast<std::uint8_t>(draws * 16 + index);
    }
    int draws = 0;
};

struct Harness {
    Harness()
        : nfc(reader, capabilities, nullptr, {150ms, 300ms, 2000ms, 3}), files(card),
          storage(files, capabilities), inventory(nfc, storage, random) {
        TEST_ASSERT_TRUE(nfc.start());
    }

    FakeNfcReader reader;
    CapabilityRegistry capabilities;
    NfcService nfc;
    MemoryFileStorageAdapter card;
    FileStorage files;
    RemovableStorageService storage;
    CountingRandom random;
    InventoryService inventory;

    void step(std::chrono::milliseconds elapsed = 150ms) {
        nfc.update(elapsed);
        storage.update(elapsed);
        inventory.update();
    }
    void runUntil(InventoryScreen target, int limit = 400) {
        for (int index = 0; index < limit && inventory.status().screen != target; ++index)
            step();
        assertScreen(target);
    }
    void settle(int updates = 40) {
        for (int index = 0; index < updates; ++index)
            step();
    }
    void assertHint(InventoryAwaitHint expected) const {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected),
                                static_cast<unsigned>(inventory.status().hint));
    }
    void assertScreen(InventoryScreen expected) const {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected),
                                static_cast<unsigned>(inventory.status().screen));
    }
    void storeRecord(const InventoryRecord& record) {
        const auto json = *encodeInventoryRecord(record);
        card.files[InventoryStore::pathFor(record.id)] = bytesOf(json);
    }
    [[nodiscard]] std::size_t recordFiles() const {
        std::size_t count = 0;
        for (const auto& [path, data] : card.files)
            count += path.rfind("inventory/records/", 0) == 0 ? 1 : 0;
        return count;
    }
};

InventoryId existingId() { return *parseInventoryIdHex(existingHex); }

InventoryRecord cyrillicRecord() {
    InventoryRecord record;
    record.id = existingId();
    record.name = "Чемодан";
    record.description = "Зарядка\nТёплый свитер\nПаспорт";
    record.revision = 4;
    return record;
}

FakeCard inventoryTag(const InventoryId& id, std::uint8_t seed) {
    return makeNtag213WithArea(seed, *encodeType2NdefArea(inventoryTagMessage(id), 144));
}

// ---- Registration -----------------------------------------------------------------

void test_inventory_enrollment_writes_verifies_and_persists_once() {
    Harness h;
    h.inventory.open();
    h.runUntil(InventoryScreen::Waiting);
    // The name comes first; no tag is needed while it is typed.
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.startEnrollment("  Blue suitcase ")));
    h.assertScreen(InventoryScreen::AwaitingTag);
    TEST_ASSERT_EQUAL_STRING("Blue suitcase", h.inventory.status().pendingName.c_str());
    h.settle(20);
    h.assertScreen(InventoryScreen::AwaitingTag);
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());

    h.reader.present(makeNtag213(1));
    h.runUntil(InventoryScreen::Writing);
    // Nothing is saved before the tag is written and read back.
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    h.runUntil(InventoryScreen::Known);

    const auto& status = h.inventory.status();
    TEST_ASSERT_TRUE(status.registered);
    TEST_ASSERT_EQUAL_STRING("Blue suitcase", status.record->name.c_str());
    TEST_ASSERT_TRUE(status.record->description.empty());
    TEST_ASSERT_EQUAL_UINT32(1, status.record->revision);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    const auto inspected = inspectType2Window(h.reader.memory().data() + 8, nfcInspectBytes);
    TEST_ASSERT_TRUE(parseInventoryTag(inspected.message).id == status.record->id);
    TEST_ASSERT_EQUAL_INT(1, h.random.draws);
    const auto id = status.record->id;

    // Holding the tag, or tapping it again, never writes or saves again.
    const auto writes = h.reader.pageWrites.size();
    h.settle(200);
    h.reader.removeCard();
    h.runUntil(InventoryScreen::Waiting);
    h.reader.present(inventoryTag(id, 1));
    h.runUntil(InventoryScreen::Known);
    TEST_ASSERT_FALSE(h.inventory.status().registered);
    TEST_ASSERT_EQUAL_UINT(writes, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
}

void test_registration_requires_a_valid_ascii_name_and_a_card() {
    Harness h;
    h.inventory.open();
    for (const auto* name : {"", "   ", "Чемодан", "a\tb"}) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::InvalidName),
                                static_cast<unsigned>(h.inventory.startEnrollment(name)));
    }
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryCommandResult::InvalidName),
        static_cast<unsigned>(h.inventory.startEnrollment(std::string(33, 'x'))));

    h.card.currentState = FileStorageState::NotPresent;
    h.card.refreshedState = FileStorageState::NotPresent;
    h.settle(30);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEnrollmentCheck::StorageUnavailable),
                            static_cast<unsigned>(h.inventory.checkEnrollment()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::StorageUnavailable),
                            static_cast<unsigned>(h.inventory.startEnrollment("Box")));
    h.reader.present(makeNtag213(1));
    h.settle(40);
    // Without a card the tag is never written.
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_FALSE(h.inventory.status().storageReady);
}

void test_waiting_registration_refuses_tags_that_are_not_blank() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    (void)h.inventory.startEnrollment("Box");
    auto locked = makeNtag213(5);
    locked.memory[10] = 0xFF;
    for (const auto& card :
         {inventoryTag(existingId(), 2),
          makeNtag213WithArea(3, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144)),
          makeClassic1K(4), locked}) {
        h.reader.present(card);
        for (int index = 0;
             index < 40 && h.inventory.status().hint != InventoryAwaitHint::TagNotBlank; ++index)
            h.step();
        h.assertScreen(InventoryScreen::AwaitingTag);
        h.assertHint(InventoryAwaitHint::TagNotBlank);
        h.settle(10);
        TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
        h.reader.removeCard();
        h.runUntil(InventoryScreen::AwaitingTag);
        h.settle(4);
        h.assertHint(InventoryAwaitHint::None);
    }
    // The stored record of the inventory tag was not touched either.
    TEST_ASSERT_EQUAL_UINT32(4, h.inventory.getRecord(existingId()).record->revision);
    h.reader.present(makeNtag213(9));
    h.runUntil(InventoryScreen::Known);
    TEST_ASSERT_EQUAL_STRING("Box", h.inventory.status().record->name.c_str());
}

void test_cancelled_registration_writes_nothing() {
    Harness h;
    h.inventory.open();
    (void)h.inventory.startEnrollment("Box");
    h.inventory.cancelEnrollment();
    h.assertScreen(InventoryScreen::Waiting);
    h.reader.present(makeNtag213(1));
    h.runUntil(InventoryScreen::Blank);
    h.settle(40);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_TRUE(h.inventory.status().pendingName.empty());
}

void test_registration_interrupted_by_removal_claims_nothing() {
    Harness h;
    h.inventory.open();
    (void)h.inventory.startEnrollment("Box");
    h.reader.present(makeNtag213(1));
    h.reader.removeAfterWrites = 4;
    h.runUntil(InventoryScreen::Writing);
    for (int index = 0; index < 100 && h.inventory.status().hint != InventoryAwaitHint::WriteFailed;
         ++index)
        h.step();
    h.assertScreen(InventoryScreen::AwaitingTag);
    h.assertHint(InventoryAwaitHint::WriteFailed);
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());

    // The torn tag reads as blank and receives a new ID when tapped again.
    auto torn = makeNtag213(1);
    torn.memory = h.reader.memory();
    h.reader.present(torn);
    h.runUntil(InventoryScreen::Known);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    TEST_ASSERT_EQUAL_INT(2, h.random.draws);
}

void test_an_interrupted_verification_is_completed_by_the_next_tap() {
    Harness h;
    h.inventory.open();
    (void)h.inventory.startEnrollment("Box");
    h.reader.present(makeNtag213(1));
    // Every page is written, then the tag leaves before it is read back.
    h.reader.removeAfterWrites = 14;
    for (int index = 0; index < 200 && h.inventory.status().hint != InventoryAwaitHint::WriteFailed;
         ++index)
        h.step();
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    const auto writes = h.reader.pageWrites.size();
    auto written = makeNtag213(1);
    written.memory = h.reader.memory();
    h.reader.present(written);
    h.runUntil(InventoryScreen::Known);
    // The tag already carried the attempt's complete ID: no second write.
    TEST_ASSERT_EQUAL_UINT(writes, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_INT(1, h.random.draws);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
}

void test_a_failed_write_is_not_retried_until_the_tag_is_presented_again() {
    Harness h;
    auto card = makeNtag213(1);
    card.lockedPages = {8};
    h.inventory.open();
    (void)h.inventory.startEnrollment("Box");
    h.reader.present(card);
    for (int index = 0; index < 200 && h.inventory.status().hint != InventoryAwaitHint::WriteFailed;
         ++index)
        h.step();
    const auto writes = h.reader.pageWrites.size();
    h.settle(100);
    h.assertScreen(InventoryScreen::AwaitingTag);
    TEST_ASSERT_EQUAL_UINT(writes, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
}

void test_inventory_tag_written_save_failed_can_recover() {
    Harness h;
    h.inventory.open();
    (void)h.inventory.startEnrollment("Tools");
    h.reader.present(makeNtag213(1));
    // The card disappears after the tag verified but before the record is saved.
    for (int index = 0; index < 400 && h.nfc.status().write != NfcWriteState::Verifying; ++index)
        h.step(0ms);
    h.card.mutationsBeforeLoss = 0;
    h.runUntil(InventoryScreen::SaveFailed);
    const auto id = *h.inventory.status().id;
    h.card.reinsert();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::NotFound),
                            static_cast<unsigned>(h.inventory.getRecord(id).status));
    // The notice stays while the tag is removed; Enter saves the record again.
    h.reader.removeCard();
    h.settle(10);
    h.assertScreen(InventoryScreen::SaveFailed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Done),
                            static_cast<unsigned>(h.inventory.retrySave()));
    h.assertScreen(InventoryScreen::RecordCreated);
    TEST_ASSERT_EQUAL_STRING("Tools", h.inventory.status().record->name.c_str());
    h.inventory.acknowledge();
    h.assertScreen(InventoryScreen::Waiting);
    TEST_ASSERT_EQUAL_INT(1, h.random.draws);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
}

void test_missing_record_is_created_without_holding_the_tag() {
    Harness h;
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::MissingRecord);
    const auto id = *h.inventory.status().id;
    // The tag leaves while the name is typed.
    h.reader.removeCard();
    h.runUntil(InventoryScreen::Waiting);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Done),
                            static_cast<unsigned>(h.inventory.createRecord(id, "Garage")));
    h.assertScreen(InventoryScreen::RecordCreated);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::NotAllowed),
                            static_cast<unsigned>(h.inventory.createRecord(id, "Again")));
    // A new tap replaces the notice and opens the record.
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    TEST_ASSERT_EQUAL_STRING("Garage", h.inventory.status().record->name.c_str());
}

// ---- Lookup -------------------------------------------------------------------------

void test_inventory_lookup_displays_utf8_description_without_companion() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    const auto& record = *h.inventory.status().record;
    TEST_ASSERT_EQUAL_STRING("Чемодан", record.name.c_str());
    TEST_ASSERT_EQUAL_STRING("Зарядка\nТёплый свитер\nПаспорт", record.description.c_str());
    TEST_ASSERT_EQUAL_UINT32(4, record.revision);

    const auto mutations = h.card.mutations;
    h.settle(200);
    TEST_ASSERT_EQUAL_INT(mutations, h.card.mutations);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    h.reader.removeCard();
    h.runUntil(InventoryScreen::Waiting);
    TEST_ASSERT_FALSE(h.inventory.status().record.has_value());
}

void test_inventory_discards_stale_tag_result() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.step();
    h.step(0ms);
    h.reader.removeCard();
    h.reader.present(makeNtag213(3));
    for (int index = 0; index < 400 && h.inventory.status().screen != InventoryScreen::Blank;
         ++index) {
        h.step();
        TEST_ASSERT_FALSE(h.inventory.status().screen == InventoryScreen::Known);
    }
    h.assertScreen(InventoryScreen::Blank);
    TEST_ASSERT_FALSE(h.inventory.status().record.has_value());
}

void test_inventory_storage_loss_preserves_record_and_reader() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    const auto before = h.card.files;
    h.card.currentState = FileStorageState::NotPresent;
    h.card.refreshedState = FileStorageState::NotPresent;
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::StorageUnavailable);
    TEST_ASSERT_TRUE(h.capabilities.isAvailable(nfcReaderCapabilityId));
    TEST_ASSERT_FALSE(h.capabilities.isAvailable(removableFileStorageCapabilityId));
    TEST_ASSERT_FALSE(h.inventory.canErase());

    h.card.refreshedState = FileStorageState::Ready;
    h.inventory.retryStorage();
    h.assertScreen(InventoryScreen::Known);
    h.step();
    TEST_ASSERT_TRUE(h.capabilities.isAvailable(removableFileStorageCapabilityId));
    TEST_ASSERT_TRUE(before == h.card.files);

    auto edited = cyrillicRecord();
    edited.description += "\nНовый предмет";
    h.card.mutationsBeforeLoss = 0;
    const auto put = h.inventory.putRecord(edited, 4);
    TEST_ASSERT_TRUE(put.status != InventoryStoreStatus::Ok);
    h.card.reinsert();
    const auto reread = h.inventory.getRecord(existingId());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(reread.status));
    TEST_ASSERT_EQUAL_UINT32(4, reread.record->revision);
}

void test_inventory_never_overwrites_foreign_ndef_or_classic() {
    struct Case {
        FakeCard card;
        InventoryScreen screen;
        bool erasable;
    };
    std::vector<Case> cases;
    cases.push_back({makeNtag213WithArea(1, *encodeType2NdefArea(
                                                encodeNdefTextMessage("https://example.org"), 144)),
                     InventoryScreen::OtherData, true});
    cases.push_back(
        {makeNtag213WithArea(
             2, *encodeType2NdefArea(encodeNdefTextMessage("CHINV2:0011223344556677"), 144)),
         InventoryScreen::OtherData, true});
    auto locked = makeNtag213(3);
    locked.memory[10] = 0xFF;
    cases.push_back({locked, InventoryScreen::ReadOnly, false});
    cases.push_back({makeNtag213WithArea(6, {0x01, 0x03, 0xA0, 0x10, 0x44, 0x03, 0x00, 0xFE}),
                     InventoryScreen::OtherData, false});
    cases.push_back({makeClassic1K(4), InventoryScreen::Unsupported, false});
    cases.push_back({makeNfcB(5), InventoryScreen::Unsupported, false});
    for (auto& item : cases) {
        Harness h;
        h.inventory.open();
        h.reader.present(item.card);
        h.runUntil(item.screen);
        TEST_ASSERT_EQUAL(item.erasable, h.inventory.canErase());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(item.erasable
                                                          ? InventoryCommandResult::Started
                                                          : InventoryCommandResult::NotAllowed),
                                static_cast<unsigned>(h.inventory.requestErase()));
        if (item.erasable)
            h.inventory.cancelErase();
        h.settle(40);
        TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
        h.reader.removeCard();
        h.reader.present(makeNtag213(9));
        h.runUntil(InventoryScreen::Blank);
    }
}

void test_corrupt_record_is_reported_and_left_untouched() {
    Harness h;
    const auto path = InventoryStore::pathFor(existingId());
    h.card.files[path] = bytesOf("{\"schema\":2,\"id\":\"truncated");
    const auto before = h.card.files;
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::InvalidRecord);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::NotAllowed),
                            static_cast<unsigned>(h.inventory.createRecord(existingId(), "Box")));
    auto other = cyrillicRecord();
    other.id = *parseInventoryIdHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    h.card.files[path] = bytesOf(*encodeInventoryRecord(other));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Invalid),
                            static_cast<unsigned>(h.inventory.getRecord(existingId()).status));
    h.card.files = before;
    const auto put = h.inventory.putRecord(cyrillicRecord(), 4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Invalid),
                            static_cast<unsigned>(put.status));
    TEST_ASSERT_TRUE(before == h.card.files);
}

// ---- Erasing ------------------------------------------------------------------------

void test_erase_empties_the_tag_then_deletes_the_record() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    TEST_ASSERT_TRUE(h.inventory.canErase());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.requestErase()));
    h.assertScreen(InventoryScreen::EraseConfirm);
    TEST_ASSERT_EQUAL_UINT(0, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    h.assertScreen(InventoryScreen::Erasing);
    // The record stays until the tag is verified blank.
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    h.runUntil(InventoryScreen::Erased);
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    const auto inspected = inspectType2Window(h.reader.memory().data() + 8, nfcInspectBytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcTagContent::Blank),
                            static_cast<unsigned>(inspected.content));
    h.settle(10);
    h.assertScreen(InventoryScreen::Erased);
    h.inventory.acknowledge();
    h.runUntil(InventoryScreen::Blank);
}

void test_failed_erase_deletes_nothing() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    h.reader.failWrites = 5;
    (void)h.inventory.requestErase();
    (void)h.inventory.confirmErase();
    h.runUntil(InventoryScreen::EraseFailed);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());

    // Without a card the tag is not erased at all: its record could not go.
    Harness noCard;
    noCard.storeRecord(cyrillicRecord());
    noCard.inventory.open();
    noCard.reader.present(inventoryTag(existingId(), 2));
    noCard.runUntil(InventoryScreen::Known);
    noCard.card.currentState = FileStorageState::NotPresent;
    noCard.card.refreshedState = FileStorageState::NotPresent;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(noCard.inventory.requestErase()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::StorageUnavailable),
                            static_cast<unsigned>(noCard.inventory.confirmErase()));
    noCard.settle(20);
    TEST_ASSERT_TRUE(noCard.reader.pageWrites.empty());
}

void test_erase_readback_failure_reconciles_the_blank_tag() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    (void)h.inventory.requestErase();
    (void)h.inventory.confirmErase();
    h.step(); // The one-page erase succeeds, but its readback has not started.
    TEST_ASSERT_EQUAL_UINT(1, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    h.reader.failReads = 2;
    h.runUntil(InventoryScreen::EraseFailed);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    h.runUntil(InventoryScreen::Erased);
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    TEST_ASSERT_EQUAL_UINT(1, h.reader.pageWrites.size());
}

void test_uncertain_erase_keeps_record_after_session_loss_even_for_a_cloned_uid() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    (void)h.inventory.requestErase();
    (void)h.inventory.confirmErase();
    h.step();
    h.reader.failReads = 2;
    h.runUntil(InventoryScreen::EraseFailed);
    h.reader.removeCard();
    const auto erasedMemory = h.reader.memory();
    // A fresh blank sticker can advertise the same UID as the erased one.
    // A new activation must not authorize deletion of the old record.
    h.reader.present(makeNtag213(2));
    h.runUntil(InventoryScreen::Blank);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    h.reader.removeCard();
    auto original = inventoryTag(existingId(), 2);
    original.memory = erasedMemory;
    h.reader.present(original);
    h.runUntil(InventoryScreen::Blank);
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
}

void test_two_uncertain_erases_keep_both_records_after_new_sessions() {
    Harness h;
    const auto first = cyrillicRecord();
    auto second = first;
    second.id = *parseInventoryIdHex("abcdefabcdefabcdefabcdefabcdefab");
    second.name = "Second";
    h.storeRecord(first);
    h.storeRecord(second);
    h.inventory.open();

    h.reader.present(inventoryTag(first.id, 2));
    h.runUntil(InventoryScreen::Known);
    (void)h.inventory.requestErase();
    (void)h.inventory.confirmErase();
    h.step();
    h.reader.failReads = 2;
    h.runUntil(InventoryScreen::EraseFailed);
    h.reader.removeCard();
    const auto firstBlank = h.reader.memory();

    h.reader.present(inventoryTag(second.id, 3));
    h.runUntil(InventoryScreen::Known);
    (void)h.inventory.requestErase();
    (void)h.inventory.confirmErase();
    h.step();
    h.reader.failReads = 2;
    h.runUntil(InventoryScreen::EraseFailed);
    h.reader.removeCard();
    const auto secondBlank = h.reader.memory();
    TEST_ASSERT_EQUAL_UINT(2, h.recordFiles());

    auto firstAgain = inventoryTag(first.id, 2);
    firstAgain.memory = firstBlank;
    h.reader.present(firstAgain);
    h.runUntil(InventoryScreen::Blank);
    TEST_ASSERT_EQUAL_UINT(2, h.recordFiles());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(h.inventory.getRecord(first.id).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(h.inventory.getRecord(second.id).status));

    h.reader.removeCard();
    h.runUntil(InventoryScreen::Waiting);
    auto secondAgain = inventoryTag(second.id, 3);
    secondAgain.memory = secondBlank;
    h.reader.present(secondAgain);
    h.runUntil(InventoryScreen::Blank);
    TEST_ASSERT_EQUAL_UINT(2, h.recordFiles());
}

void test_erase_of_a_missing_or_damaged_record_tag() {
    Harness missing;
    missing.inventory.open();
    missing.reader.present(inventoryTag(existingId(), 2));
    missing.runUntil(InventoryScreen::MissingRecord);
    TEST_ASSERT_TRUE(missing.inventory.canErase());
    (void)missing.inventory.requestErase();
    (void)missing.inventory.confirmErase();
    missing.runUntil(InventoryScreen::Erased);

    Harness damaged;
    damaged.card.files[InventoryStore::pathFor(existingId())] = bytesOf("{");
    damaged.inventory.open();
    damaged.reader.present(inventoryTag(existingId(), 2));
    damaged.runUntil(InventoryScreen::InvalidRecord);
    (void)damaged.inventory.requestErase();
    (void)damaged.inventory.confirmErase();
    damaged.runUntil(InventoryScreen::Erased);
    TEST_ASSERT_EQUAL_UINT(0, damaged.recordFiles());
}

void test_foreign_tag_removed_and_returned_before_erase_confirmation() {
    Harness h;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    h.inventory.open();
    h.reader.present(foreign);
    h.runUntil(InventoryScreen::OtherData);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.requestErase()));
    h.reader.removeCard();
    h.settle(5);
    h.assertScreen(InventoryScreen::EraseConfirm);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEraseHint::TagRemoved),
                            static_cast<unsigned>(h.inventory.status().eraseHint));

    h.reader.present(foreign);
    for (int index = 0; index < 50 && h.inventory.status().eraseHint != InventoryEraseHint::None;
         ++index)
        h.step();
    h.assertScreen(InventoryScreen::EraseConfirm);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEraseHint::None),
                            static_cast<unsigned>(h.inventory.status().eraseHint));
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    (void)h.inventory.confirmErase();
    h.runUntil(InventoryScreen::Erased);
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(NfcTagContent::Blank),
        static_cast<unsigned>(
            inspectType2Window(h.reader.memory().data() + 8, nfcInspectBytes).content));
}

void test_different_tag_cannot_arm_erase_without_a_new_confirmation() {
    Harness h;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    const auto other =
        makeNtag213WithArea(5, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    h.inventory.open();
    h.reader.present(foreign);
    h.runUntil(InventoryScreen::OtherData);
    (void)h.inventory.requestErase();
    h.reader.removeCard();
    h.settle(5);
    h.reader.present(other);
    for (int index = 0;
         index < 50 && h.inventory.status().eraseHint != InventoryEraseHint::DifferentTag; ++index)
        h.step();
    h.assertScreen(InventoryScreen::EraseConfirm);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEraseHint::DifferentTag),
                            static_cast<unsigned>(h.inventory.status().eraseHint));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::NotAllowed),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    h.reader.removeCard();
    h.settle(5);
    h.reader.present(foreign);
    for (int index = 0; index < 50 && h.inventory.status().eraseHint != InventoryEraseHint::None;
         ++index)
        h.step();
    h.assertScreen(InventoryScreen::EraseConfirm);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    h.settle(10);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    h.runUntil(InventoryScreen::Erased);
}

void test_tag_being_read_cannot_arm_erase_without_a_new_confirmation() {
    Harness h;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    h.inventory.open();
    h.reader.present(foreign);
    h.runUntil(InventoryScreen::OtherData);
    (void)h.inventory.requestErase();
    h.reader.removeCard();
    h.settle(5);
    h.reader.present(foreign);
    h.step();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(NfcServiceState::Reading),
                            static_cast<unsigned>(h.nfc.status().state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::NotAllowed),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    for (int index = 0; index < 50 && h.nfc.status().state != NfcServiceState::Ready; ++index)
        h.step();
    h.assertScreen(InventoryScreen::EraseConfirm);
    h.settle(10);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    h.runUntil(InventoryScreen::Erased);
}

void test_same_uid_with_changed_data_is_not_called_a_different_tag() {
    Harness h;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    const auto changed =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("world"), 144));
    h.inventory.open();
    h.reader.present(foreign);
    h.runUntil(InventoryScreen::OtherData);
    (void)h.inventory.requestErase();
    h.reader.removeCard();
    h.settle(5);
    h.reader.present(changed);
    for (int index = 0;
         index < 50 && h.inventory.status().eraseHint != InventoryEraseHint::TagChanged; ++index)
        h.step();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEraseHint::TagChanged),
                            static_cast<unsigned>(h.inventory.status().eraseHint));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::NotAllowed),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
}

void test_confirmed_erase_checks_returning_tag_before_writing() {
    Harness h;
    const auto foreign =
        makeNtag213WithArea(4, *encodeType2NdefArea(encodeNdefTextMessage("hello"), 144));
    h.inventory.open();
    h.reader.present(foreign);
    h.runUntil(InventoryScreen::OtherData);
    (void)h.inventory.requestErase();
    h.reader.removeCard();
    h.settle(5);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryCommandResult::Started),
                            static_cast<unsigned>(h.inventory.confirmErase()));
    h.assertScreen(InventoryScreen::EraseAwaitingTag);
    h.reader.present(foreign);
    h.step();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryEraseHint::CheckingTag),
                            static_cast<unsigned>(h.inventory.status().eraseHint));
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    h.runUntil(InventoryScreen::Erased);
}

// ---- Companion edits ---------------------------------------------------------------

void test_inventory_put_revision_round_trip() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);

    auto edited = cyrillicRecord();
    edited.name = "Синий чемодан";
    edited.description = "Ноутбук";
    const auto put = h.inventory.putRecord(edited, 4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(put.status));
    TEST_ASSERT_EQUAL_UINT32(5, put.revision);
    TEST_ASSERT_EQUAL_STRING("Синий чемодан", h.inventory.status().record->name.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, h.inventory.status().record->revision);

    const auto stale = h.inventory.putRecord(cyrillicRecord(), 4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Conflict),
                            static_cast<unsigned>(stale.status));
    TEST_ASSERT_EQUAL_UINT32(5, stale.revision);
    TEST_ASSERT_EQUAL_STRING("Ноутбук",
                             h.inventory.getRecord(existingId()).record->description.c_str());
    auto invalid = cyrillicRecord();
    invalid.name.clear();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Rejected),
                            static_cast<unsigned>(h.inventory.putRecord(invalid, 5).status));
}

void test_companion_delete_checks_the_revision() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    h.inventory.open();
    h.reader.present(inventoryTag(existingId(), 2));
    h.runUntil(InventoryScreen::Known);
    const auto stale = h.inventory.deleteRecord(existingId(), 3);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Conflict),
                            static_cast<unsigned>(stale.status));
    TEST_ASSERT_EQUAL_UINT32(4, stale.revision);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryStoreStatus::Conflict),
        static_cast<unsigned>(h.inventory.deleteRecord(existingId(), 0).status));
    TEST_ASSERT_EQUAL_UINT(1, h.recordFiles());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryStoreStatus::Ok),
        static_cast<unsigned>(h.inventory.deleteRecord(existingId(), 4).status));
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
    // The box on the reader now has no record.
    h.assertScreen(InventoryScreen::MissingRecord);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryStoreStatus::NotFound),
        static_cast<unsigned>(h.inventory.deleteRecord(existingId(), 4).status));

    // A damaged file is deleted only with revision 0.
    h.card.files[InventoryStore::pathFor(existingId())] = bytesOf("{");
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryStoreStatus::Conflict),
        static_cast<unsigned>(h.inventory.deleteRecord(existingId(), 4).status));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(InventoryStoreStatus::Ok),
        static_cast<unsigned>(h.inventory.deleteRecord(existingId(), 0).status));
    TEST_ASSERT_EQUAL_UINT(0, h.recordFiles());
}

void test_listing_names_each_record_once() {
    Harness h;
    h.storeRecord(cyrillicRecord());
    auto other = cyrillicRecord();
    other.id = *parseInventoryIdHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    h.storeRecord(other);
    h.card.files[InventoryStore::pathFor(existingId()) + ".new"] = bytesOf("x");
    h.card.files["inventory/records/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.json.bak"] = bytesOf("x");
    h.card.files["inventory/records/notes.txt"] = bytesOf("x");
    const auto listed = h.inventory.listRecords();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(listed.status));
    TEST_ASSERT_EQUAL_UINT(3, listed.ids.size());
    TEST_ASSERT_TRUE(listed.ids[0] == existingId());
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_inventory_enrollment_writes_verifies_and_persists_once);
    RUN_TEST(test_registration_requires_a_valid_ascii_name_and_a_card);
    RUN_TEST(test_waiting_registration_refuses_tags_that_are_not_blank);
    RUN_TEST(test_cancelled_registration_writes_nothing);
    RUN_TEST(test_registration_interrupted_by_removal_claims_nothing);
    RUN_TEST(test_an_interrupted_verification_is_completed_by_the_next_tap);
    RUN_TEST(test_a_failed_write_is_not_retried_until_the_tag_is_presented_again);
    RUN_TEST(test_inventory_tag_written_save_failed_can_recover);
    RUN_TEST(test_missing_record_is_created_without_holding_the_tag);
    RUN_TEST(test_inventory_lookup_displays_utf8_description_without_companion);
    RUN_TEST(test_inventory_discards_stale_tag_result);
    RUN_TEST(test_inventory_storage_loss_preserves_record_and_reader);
    RUN_TEST(test_inventory_never_overwrites_foreign_ndef_or_classic);
    RUN_TEST(test_corrupt_record_is_reported_and_left_untouched);
    RUN_TEST(test_erase_empties_the_tag_then_deletes_the_record);
    RUN_TEST(test_failed_erase_deletes_nothing);
    RUN_TEST(test_erase_readback_failure_reconciles_the_blank_tag);
    RUN_TEST(test_uncertain_erase_keeps_record_after_session_loss_even_for_a_cloned_uid);
    RUN_TEST(test_two_uncertain_erases_keep_both_records_after_new_sessions);
    RUN_TEST(test_erase_of_a_missing_or_damaged_record_tag);
    RUN_TEST(test_foreign_tag_removed_and_returned_before_erase_confirmation);
    RUN_TEST(test_different_tag_cannot_arm_erase_without_a_new_confirmation);
    RUN_TEST(test_tag_being_read_cannot_arm_erase_without_a_new_confirmation);
    RUN_TEST(test_same_uid_with_changed_data_is_not_called_a_different_tag);
    RUN_TEST(test_confirmed_erase_checks_returning_tag_before_writing);
    RUN_TEST(test_inventory_put_revision_round_trip);
    RUN_TEST(test_companion_delete_checks_the_revision);
    RUN_TEST(test_listing_names_each_record_once);
    return UNITY_END();
}
