#include <unity.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "../support/fake_nfc_reader.h"
#include "core/capabilities/capability_registry.h"
#include "core/logging/logger.h"
#include "services/nfc/nfc_ndef.h"
#include "services/nfc/nfc_service.h"

using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;
using namespace std::chrono_literals;

namespace {

NfcServiceConfig testConfig() { return {150ms, 300ms, 2000ms, 3}; }

class CapturingSink final : public ILogSink {
  public:
    void write(const LogRecord& record) override {
        lines.push_back(std::string(record.component) + ": " + record.message);
    }
    [[nodiscard]] bool anyContains(const std::string& text) const {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
            return line.find(text) != std::string::npos;
        });
    }
    std::vector<std::string> lines;
};

struct Harness {
    explicit Harness(NfcServiceConfig config = testConfig())
        : service(reader, capabilities, &logger, config) {}

    FakeNfcReader reader;
    CapabilityRegistry capabilities;
    CapturingSink sink;
    Logger logger{sink, LogLevel::Debug};
    NfcService service;

    void startScanning() {
        TEST_ASSERT_TRUE(service.start());
        service.startScanning();
    }
    // Advance until `target`, failing the test instead of looping forever.
    void runUntil(NfcServiceState target, std::chrono::milliseconds step = 150ms, int limit = 400) {
        for (int index = 0; index < limit && service.state() != target; ++index)
            service.update(step);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(target),
                                static_cast<unsigned>(service.state()));
    }
    void settle(int updates = 60, std::chrono::milliseconds step = 150ms) {
        for (int index = 0; index < updates; ++index)
            service.update(step);
    }
    [[nodiscard]] bool capabilityAvailable() const {
        return capabilities.isAvailable(nfcReaderCapabilityId);
    }
};

void assertState(NfcServiceState expected, NfcServiceState actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(actual));
}

void assertContent(NfcTagContent expected, NfcTagContent actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(actual));
}

void assertWrite(NfcWriteState expected, NfcWriteState actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(actual));
}

std::vector<std::uint8_t> sampleMessage() {
    return encodeNdefTextMessage("CHINV1:00112233445566778899aabbccddeeff");
}

std::vector<std::uint8_t> areaFor(const std::vector<std::uint8_t>& message) {
    return *encodeType2NdefArea(message, 144);
}

// ---- Reader lifecycle ---------------------------------------------------------

void test_nfc_service_becomes_idle_after_reader_init() {
    Harness h;
    assertState(NfcServiceState::Unavailable, h.service.state());
    TEST_ASSERT_FALSE(h.capabilityAvailable());

    TEST_ASSERT_TRUE(h.service.start());
    assertState(NfcServiceState::Idle, h.service.state());
    TEST_ASSERT_TRUE(h.capabilityAvailable());
    TEST_ASSERT_TRUE(h.service.readerPresent());
    TEST_ASSERT_FALSE(h.service.status().card.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, h.service.status().session);
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
    TEST_ASSERT_EQUAL_INT(1, h.reader.initializeCalls);
}

void test_missing_reader_stays_unavailable_and_is_never_probed_again() {
    Harness h;
    h.reader.initResults = {NfcReaderInitResult::NotPresent};
    TEST_ASSERT_FALSE(h.service.start());
    assertState(NfcServiceState::Unavailable, h.service.state());
    TEST_ASSERT_FALSE(h.capabilityAvailable());
    TEST_ASSERT_FALSE(h.service.readerPresent());

    h.service.startScanning();
    h.settle(200, 1000ms);
    TEST_ASSERT_EQUAL_INT(1, h.reader.initializeCalls);
    TEST_ASSERT_EQUAL_INT(0, h.reader.detectCalls);
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
    assertState(NfcServiceState::Unavailable, h.service.state());
}

void test_reader_failure_does_not_break_runtime() {
    Harness h;
    h.reader.initResults = {NfcReaderInitResult::Failed, NfcReaderInitResult::Failed,
                            NfcReaderInitResult::Ready};
    TEST_ASSERT_FALSE(h.service.start());
    assertState(NfcServiceState::Error, h.service.state());
    TEST_ASSERT_FALSE(h.capabilityAvailable());
    TEST_ASSERT_TRUE(h.service.readerPresent());
    TEST_ASSERT_TRUE(h.sink.anyContains("initialization failed"));

    h.service.update(1000ms);
    TEST_ASSERT_EQUAL_INT(1, h.reader.initializeCalls);
    h.service.update(1000ms);
    TEST_ASSERT_EQUAL_INT(2, h.reader.initializeCalls);
    assertState(NfcServiceState::Error, h.service.state());
    TEST_ASSERT_EQUAL_INT(0, h.reader.detectCalls);

    h.service.update(2000ms);
    TEST_ASSERT_EQUAL_INT(3, h.reader.initializeCalls);
    assertState(NfcServiceState::Idle, h.service.state());
    TEST_ASSERT_TRUE(h.capabilityAvailable());
}

void test_scanning_request_survives_a_late_reader_recovery() {
    Harness h;
    h.reader.initResults = {NfcReaderInitResult::Failed, NfcReaderInitResult::Ready};
    TEST_ASSERT_FALSE(h.service.start());
    h.service.startScanning();
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
    h.service.update(2000ms);
    assertState(NfcServiceState::Idle, h.service.state());
    TEST_ASSERT_TRUE(h.reader.fieldEnabled);
}

void test_scanning_controls_the_rf_field() {
    Harness h;
    TEST_ASSERT_TRUE(h.service.start());
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
    h.reader.present(makeNtag213(1));
    h.settle(10);
    // Nothing is polled until a Mini App asks for scanning.
    TEST_ASSERT_EQUAL_INT(0, h.reader.detectCalls);
    assertState(NfcServiceState::Idle, h.service.state());

    h.service.startScanning();
    TEST_ASSERT_TRUE(h.reader.fieldEnabled);
    TEST_ASSERT_TRUE(h.service.status().scanning);
    h.service.stopScanning();
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
    TEST_ASSERT_FALSE(h.service.status().scanning);
}

void test_stop_scanning_clears_the_tag_and_keeps_the_reader_available() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(h.service.status().card.has_value());

    h.service.stopScanning();
    assertState(NfcServiceState::Idle, h.service.state());
    TEST_ASSERT_FALSE(h.service.status().card.has_value());
    assertContent(NfcTagContent::None, h.service.status().tag.content);
    TEST_ASSERT_EQUAL_UINT32(0, h.service.status().session);
    TEST_ASSERT_TRUE(h.capabilityAvailable());
    TEST_ASSERT_FALSE(h.reader.fieldEnabled);
}

// ---- Tag inspection -----------------------------------------------------------

void test_non_ntag_cards_are_unsupported_and_never_read() {
    for (const auto& card : {makeNfcB(1), makeClassic1K(2)}) {
        Harness h;
        h.startScanning();
        h.reader.present(card);
        h.service.update(1ms);
        assertState(NfcServiceState::Ready, h.service.state());
        assertContent(NfcTagContent::Unsupported, h.service.status().tag.content);
        TEST_ASSERT_TRUE(h.service.status().card.has_value());
        TEST_ASSERT_TRUE(h.reader.pageReads.empty());
        TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
        // Unsupported tags are refused for writing without any tag access.
        TEST_ASSERT_FALSE(h.service.writeMessage(1, sampleMessage()));
        assertWrite(NfcWriteState::Refused, h.service.status().write);
        TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    }
}

void test_blank_ntag213_is_inspected_with_one_read_per_update() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.service.update(150ms);
    assertState(NfcServiceState::Reading, h.service.state());
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().session);
    for (std::size_t read = 1; read <= nfcInspectPages / 4; ++read) {
        h.service.update(0ms);
        TEST_ASSERT_EQUAL_UINT(read, h.reader.pageReads.size());
    }
    assertState(NfcServiceState::Ready, h.service.state());
    const std::vector<std::uint16_t> expected{2, 6, 10, 14};
    TEST_ASSERT_TRUE(expected == h.reader.pageReads);
    const auto& tag = h.service.status().tag;
    assertContent(NfcTagContent::Blank, tag.content);
    TEST_ASSERT_TRUE(tag.writable);
    TEST_ASSERT_EQUAL_UINT16(144, tag.capacity);
}

// An NTAG215 whose data area holds NULL bytes past the first window and then
// an existing NDEF message.
FakeCard ntag215WithLateMessage() {
    auto card = makeNtag213(3);
    card.info.type = NfcCardType::Ntag215;
    card.info.userBytes = 504;
    card.memory.assign(135 * 4, 0);
    card.memory[12] = 0xE1;
    card.memory[13] = 0x10;
    card.memory[14] = 0x3F;
    const auto area = areaFor(sampleMessage());
    std::copy(area.begin(), area.end(), card.memory.begin() + 16 + 200);
    return card;
}

void test_blank_is_decided_only_after_reading_the_whole_data_area() {
    Harness h;
    h.startScanning();
    h.reader.present(ntag215WithLateMessage());
    h.runUntil(NfcServiceState::Ready);
    // The message beyond the first window is found, never overwritten.
    assertContent(NfcTagContent::Message, h.service.status().tag.content);
    TEST_ASSERT_FALSE(h.service.writeMessage(1, sampleMessage()));
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());

    // An all-zero NTAG213 is blank only after its whole data area was read,
    // including the last user pages.
    Harness zeroed;
    auto card = makeNtag213(4);
    std::fill(card.memory.begin() + 16, card.memory.end(), 0);
    zeroed.startScanning();
    zeroed.reader.present(card);
    zeroed.runUntil(NfcServiceState::Ready);
    assertContent(NfcTagContent::Blank, zeroed.service.status().tag.content);
    TEST_ASSERT_EQUAL_UINT16(36, zeroed.reader.pageReads.back());
    for (const auto page : zeroed.reader.pageReads)
        TEST_ASSERT_TRUE(page + 3 <= 39);
}

void test_message_tag_publishes_the_whole_message() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(1, areaFor(sampleMessage())));
    h.runUntil(NfcServiceState::Ready);
    const auto& tag = h.service.status().tag;
    assertContent(NfcTagContent::Message, tag.content);
    TEST_ASSERT_TRUE(sampleMessage() == tag.message);
}

void test_held_tag_is_read_once_and_never_written() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(1, areaFor(sampleMessage())));
    h.runUntil(NfcServiceState::Ready);
    const auto generation = h.service.status().generation;
    const auto reads = h.reader.pageReads.size();
    h.settle(500);
    assertState(NfcServiceState::Ready, h.service.state());
    TEST_ASSERT_EQUAL_INT(1, h.reader.detectCalls);
    TEST_ASSERT_EQUAL_UINT(reads, h.reader.pageReads.size());
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());
    TEST_ASSERT_EQUAL_UINT32(generation, h.service.status().generation);
    TEST_ASSERT_TRUE(h.reader.presenceCalls > 10);
}

void test_read_failure_is_retried_once_then_reported() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.reader.failReads = 1;
    h.runUntil(NfcServiceState::Ready);
    assertContent(NfcTagContent::Blank, h.service.status().tag.content);

    Harness failing;
    failing.startScanning();
    failing.reader.present(makeNtag213(2));
    failing.reader.failReads = 2;
    failing.runUntil(NfcServiceState::Ready);
    assertContent(NfcTagContent::ReadFailed, failing.service.status().tag.content);
    TEST_ASSERT_FALSE(failing.service.status().tag.writable);
}

// ---- Tag writing --------------------------------------------------------------

void test_write_uses_empty_length_first_and_verifies_by_reading_back() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    const auto message = sampleMessage();
    const auto area = areaFor(message);

    TEST_ASSERT_TRUE(h.service.writeMessage(1, message));
    assertState(NfcServiceState::Writing, h.service.state());
    assertWrite(NfcWriteState::Writing, h.service.status().write);
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().writeSession);
    h.reader.pageReads.clear();
    h.runUntil(NfcServiceState::Ready, 0ms);

    const auto& writes = h.reader.pageWrites;
    TEST_ASSERT_EQUAL_UINT(area.size() / 4 + 1, writes.size());
    TEST_ASSERT_EQUAL_UINT16(4, writes.front().page);
    TEST_ASSERT_EQUAL_UINT8(0x03, writes.front().data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, writes.front().data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, writes.front().data[2]);
    TEST_ASSERT_EQUAL_UINT16(4, writes.back().page);
    TEST_ASSERT_EQUAL_UINT8(area[1], writes.back().data[1]);
    for (const auto& write : writes)
        TEST_ASSERT_TRUE(write.page >= 4 && write.page < 40);
    // Read back from the first user page.
    TEST_ASSERT_EQUAL_UINT16(4, h.reader.pageReads.front());
    TEST_ASSERT_TRUE(std::equal(area.begin(), area.end(), h.reader.memory().begin() + 16));

    assertWrite(NfcWriteState::Succeeded, h.service.status().write);
    assertContent(NfcTagContent::Message, h.service.status().tag.content);
    TEST_ASSERT_TRUE(message == h.service.status().tag.message);
}

void test_write_is_refused_for_anything_but_a_blank_writable_tag() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(1, areaFor(sampleMessage())));
    h.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_FALSE(h.service.writeMessage(1, sampleMessage()));
    TEST_ASSERT_FALSE(h.service.writeMessage(7, sampleMessage()));
    assertWrite(NfcWriteState::Refused, h.service.status().write);
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());

    // A capability container that denies writes makes a blank tag read-only.
    Harness locked;
    auto card = makeNtag213(2);
    card.memory[15] = 0x0F;
    locked.startScanning();
    locked.reader.present(card);
    locked.runUntil(NfcServiceState::Ready);
    assertContent(NfcTagContent::Blank, locked.service.status().tag.content);
    TEST_ASSERT_FALSE(locked.service.status().tag.writable);
    TEST_ASSERT_FALSE(locked.service.writeMessage(1, sampleMessage()));
    TEST_ASSERT_TRUE(locked.reader.pageWrites.empty());
}

void test_removal_during_write_interrupts_and_leaves_no_truncated_message() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    h.reader.removeAfterWrites = 3;
    TEST_ASSERT_TRUE(h.service.writeMessage(1, sampleMessage()));
    h.runUntil(NfcServiceState::Idle, 0ms);

    assertWrite(NfcWriteState::Interrupted, h.service.status().write);
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().writeSession);
    TEST_ASSERT_EQUAL_UINT32(0, h.service.status().session);
    // The torn tag still reads as blank: an empty NDEF TLV, then a Terminator.
    const auto& memory = h.reader.memory();
    const auto inspection = inspectType2Window(memory.data() + 8, 8 + 144);
    assertContent(NfcTagContent::Blank, inspection.content);
}

void test_locked_page_fails_the_write_and_the_tag_is_inspected_again() {
    Harness h;
    auto card = makeNtag213(1);
    card.lockedPages = {6};
    h.startScanning();
    h.reader.present(card);
    h.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(h.service.writeMessage(1, sampleMessage()));
    h.reader.pageReads.clear();
    h.runUntil(NfcServiceState::Ready, 0ms);
    assertWrite(NfcWriteState::Failed, h.service.status().write);
    // Re-inspected from page 2, and still blank: the length was never set.
    TEST_ASSERT_EQUAL_UINT16(2, h.reader.pageReads.front());
    assertContent(NfcTagContent::Blank, h.service.status().tag.content);
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().session);
}

void test_read_back_mismatch_fails_verification() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    h.reader.corruptPage = 7;
    TEST_ASSERT_TRUE(h.service.writeMessage(1, sampleMessage()));
    h.runUntil(NfcServiceState::Ready, 0ms);
    assertWrite(NfcWriteState::Failed, h.service.status().write);
}

void test_transient_write_failure_is_retried_once() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    h.reader.failWrites = 1;
    TEST_ASSERT_TRUE(h.service.writeMessage(1, sampleMessage()));
    h.runUntil(NfcServiceState::Ready, 0ms);
    assertWrite(NfcWriteState::Succeeded, h.service.status().write);
}

void test_erase_empties_only_the_inspected_message_and_verifies() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(1, areaFor(sampleMessage())));
    h.runUntil(NfcServiceState::Ready);
    // Only the content the caller inspected is erased.
    const auto raw = h.service.status().tag.raw;
    auto changed = raw;
    changed.back() ^= 0x01;
    TEST_ASSERT_FALSE(h.service.eraseTag(1, changed));
    TEST_ASSERT_FALSE(h.service.eraseTag(2, raw));
    TEST_ASSERT_TRUE(h.reader.pageWrites.empty());

    TEST_ASSERT_TRUE(h.service.eraseTag(1, raw));
    h.runUntil(NfcServiceState::Ready, 0ms);
    TEST_ASSERT_EQUAL_UINT(1, h.reader.pageWrites.size());
    TEST_ASSERT_EQUAL_UINT16(4, h.reader.pageWrites[0].page);
    const NfcType2Page blank{0x03, 0x00, 0xFE, 0x00};
    TEST_ASSERT_TRUE(blank == h.reader.pageWrites[0].data);
    assertWrite(NfcWriteState::Succeeded, h.service.status().write);
    assertContent(NfcTagContent::Blank, h.service.status().tag.content);
    TEST_ASSERT_TRUE(h.service.status().tag.message.empty());
}

void test_erase_is_refused_for_blank_foreign_or_locked_tags() {
    Harness blank;
    blank.startScanning();
    blank.reader.present(makeNtag213(1));
    blank.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_FALSE(blank.service.eraseTag(1, blank.service.status().tag.raw));

    Harness locked;
    auto card = makeNtag213WithArea(2, areaFor(sampleMessage()));
    card.memory[15] = 0x0F;
    locked.startScanning();
    locked.reader.present(card);
    locked.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_FALSE(locked.service.eraseTag(1, locked.service.status().tag.raw));
    TEST_ASSERT_TRUE(locked.reader.pageWrites.empty());

    // Other NDEF data is erasable; control TLVs (reserved areas) are not.
    Harness foreign;
    foreign.startScanning();
    foreign.reader.present(makeNtag213WithArea(3, areaFor(encodeNdefTextMessage("hello"))));
    foreign.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(foreign.service.eraseTag(1, foreign.service.status().tag.raw));
    foreign.runUntil(NfcServiceState::Ready, 0ms);
    assertContent(NfcTagContent::Blank, foreign.service.status().tag.content);

    Harness reserved;
    reserved.startScanning();
    reserved.reader.present(
        makeNtag213WithArea(4, {0x01, 0x03, 0xA0, 0x10, 0x44, 0x03, 0x00, 0xFE}));
    reserved.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(reserved.service.status().tag.reserved);
    TEST_ASSERT_FALSE(reserved.service.eraseTag(1, reserved.service.status().tag.raw));
    TEST_ASSERT_TRUE(reserved.reader.pageWrites.empty());
}

// ---- Removal, replacement and stale results -----------------------------------

void test_tag_removal_clears_the_session() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    const auto generation = h.service.status().generation;

    h.reader.removeCard();
    h.runUntil(NfcServiceState::Idle);
    const auto& status = h.service.status();
    TEST_ASSERT_FALSE(status.card.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, status.session);
    assertContent(NfcTagContent::None, status.tag.content);
    TEST_ASSERT_TRUE(status.generation != generation);
    TEST_ASSERT_TRUE(h.capabilityAvailable());
}

void test_tag_removed_during_a_read_never_reaches_ready() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.service.update(150ms);
    h.service.update(0ms);
    assertState(NfcServiceState::Reading, h.service.state());

    h.reader.removeCard();
    bool reachedReady = false;
    for (int index = 0; index < 400 && h.service.state() != NfcServiceState::Idle; ++index) {
        h.service.update(150ms);
        reachedReady = reachedReady || h.service.state() == NfcServiceState::Ready;
    }
    TEST_ASSERT_FALSE(reachedReady);
    assertState(NfcServiceState::Idle, h.service.state());
}

void test_replacement_tag_never_receives_the_previous_tags_data() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(0xA1, areaFor(sampleMessage())));
    h.service.update(150ms);
    h.service.update(0ms);
    assertState(NfcServiceState::Reading, h.service.state());

    // A is removed and a blank B is presented at once. The reader answers the
    // next read for A under B's activation.
    h.reader.removeCard();
    h.reader.present(makeNtag213(0xB2));
    for (int index = 0; index < 400 && !(h.service.state() == NfcServiceState::Ready &&
                                         h.service.status().session == 2);
         ++index)
        h.service.update(150ms);
    assertState(NfcServiceState::Ready, h.service.state());
    TEST_ASSERT_EQUAL_UINT8(0xB2, h.service.status().card->uid.back());
    assertContent(NfcTagContent::Blank, h.service.status().tag.content);
}

void test_late_result_from_an_earlier_tag_is_discarded() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.service.update(150ms);
    h.reader.staleActivation = 0;
    h.reader.staleOperations = 1;
    h.runUntil(NfcServiceState::Ready, 0ms);
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().session);
    assertContent(NfcTagContent::Blank, h.service.status().tag.content);
    // The discarded completion was retried.
    TEST_ASSERT_EQUAL_UINT(5, h.reader.pageReads.size());
}

void test_persistent_stale_results_reset_the_reader() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.service.update(150ms);
    h.reader.staleActivation = 0;
    h.reader.staleOperations = 1000;
    for (int index = 0; index < 10 && h.reader.shutdownCalls == 0; ++index)
        h.service.update(0ms);

    TEST_ASSERT_EQUAL_INT(1, h.reader.shutdownCalls);
    assertState(NfcServiceState::Error, h.service.state());
    TEST_ASSERT_FALSE(h.capabilityAvailable());

    h.reader.staleOperations = 0;
    h.runUntil(NfcServiceState::Ready, 500ms, 100);
    TEST_ASSERT_TRUE(h.capabilityAvailable());
    TEST_ASSERT_EQUAL_UINT32(2, h.service.status().session);
}

// ---- Reader loss --------------------------------------------------------------

void test_reader_loss_clears_the_session_and_removes_the_capability() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(h.capabilityAvailable());

    h.reader.readerGone = true;
    h.runUntil(NfcServiceState::Error);
    TEST_ASSERT_FALSE(h.capabilityAvailable());
    TEST_ASSERT_EQUAL_INT(1, h.reader.shutdownCalls);
    TEST_ASSERT_FALSE(h.service.status().card.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, h.service.status().session);
    TEST_ASSERT_TRUE(h.sink.anyContains("reader lost"));

    h.runUntil(NfcServiceState::Ready, 500ms, 200);
    TEST_ASSERT_TRUE(h.capabilityAvailable());
    TEST_ASSERT_EQUAL_UINT32(2, h.service.status().session);
    TEST_ASSERT_TRUE(h.reader.fieldEnabled);
}

void test_reader_loss_while_idle_is_detected_by_polling() {
    Harness h;
    h.startScanning();
    h.reader.readerGone = true;
    h.runUntil(NfcServiceState::Error);
    TEST_ASSERT_FALSE(h.capabilityAvailable());
}

void test_reader_loss_during_a_write_interrupts_it() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213(1));
    h.runUntil(NfcServiceState::Ready);
    TEST_ASSERT_TRUE(h.service.writeMessage(1, sampleMessage()));
    h.service.update(0ms);
    h.reader.readerGone = true;
    h.service.update(0ms);
    assertState(NfcServiceState::Error, h.service.state());
    assertWrite(NfcWriteState::Interrupted, h.service.status().write);
    TEST_ASSERT_EQUAL_UINT32(1, h.service.status().writeSession);
}

void test_a_vanished_reader_is_probed_again_without_blocking_updates() {
    Harness h;
    h.reader.initResults = {NfcReaderInitResult::Ready, NfcReaderInitResult::NotPresent,
                            NfcReaderInitResult::NotPresent, NfcReaderInitResult::Ready};
    h.startScanning();
    h.reader.readerGone = true;
    h.runUntil(NfcServiceState::Error);
    h.service.update(2000ms);
    assertState(NfcServiceState::Unavailable, h.service.state());
    h.service.update(2000ms);
    TEST_ASSERT_EQUAL_INT(3, h.reader.initializeCalls);
    h.service.update(2000ms);
    assertState(NfcServiceState::Idle, h.service.state());
    TEST_ASSERT_TRUE(h.capabilityAvailable());
}

void test_operational_log_carries_no_tag_identity_or_data() {
    Harness h;
    h.startScanning();
    h.reader.present(makeNtag213WithArea(0x5C, areaFor(sampleMessage())));
    h.runUntil(NfcServiceState::Ready);
    for (const auto& line : h.sink.lines) {
        TEST_ASSERT_TRUE(line.find("5C") == std::string::npos);
        TEST_ASSERT_TRUE(line.find("CHINV") == std::string::npos);
    }
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nfc_service_becomes_idle_after_reader_init);
    RUN_TEST(test_missing_reader_stays_unavailable_and_is_never_probed_again);
    RUN_TEST(test_reader_failure_does_not_break_runtime);
    RUN_TEST(test_scanning_request_survives_a_late_reader_recovery);
    RUN_TEST(test_scanning_controls_the_rf_field);
    RUN_TEST(test_stop_scanning_clears_the_tag_and_keeps_the_reader_available);
    RUN_TEST(test_non_ntag_cards_are_unsupported_and_never_read);
    RUN_TEST(test_blank_ntag213_is_inspected_with_one_read_per_update);
    RUN_TEST(test_blank_is_decided_only_after_reading_the_whole_data_area);
    RUN_TEST(test_message_tag_publishes_the_whole_message);
    RUN_TEST(test_held_tag_is_read_once_and_never_written);
    RUN_TEST(test_read_failure_is_retried_once_then_reported);
    RUN_TEST(test_write_uses_empty_length_first_and_verifies_by_reading_back);
    RUN_TEST(test_write_is_refused_for_anything_but_a_blank_writable_tag);
    RUN_TEST(test_removal_during_write_interrupts_and_leaves_no_truncated_message);
    RUN_TEST(test_locked_page_fails_the_write_and_the_tag_is_inspected_again);
    RUN_TEST(test_read_back_mismatch_fails_verification);
    RUN_TEST(test_transient_write_failure_is_retried_once);
    RUN_TEST(test_erase_empties_only_the_inspected_message_and_verifies);
    RUN_TEST(test_erase_is_refused_for_blank_foreign_or_locked_tags);
    RUN_TEST(test_tag_removal_clears_the_session);
    RUN_TEST(test_tag_removed_during_a_read_never_reaches_ready);
    RUN_TEST(test_replacement_tag_never_receives_the_previous_tags_data);
    RUN_TEST(test_late_result_from_an_earlier_tag_is_discarded);
    RUN_TEST(test_persistent_stale_results_reset_the_reader);
    RUN_TEST(test_reader_loss_clears_the_session_and_removes_the_capability);
    RUN_TEST(test_reader_loss_while_idle_is_detected_by_polling);
    RUN_TEST(test_reader_loss_during_a_write_interrupts_it);
    RUN_TEST(test_a_vanished_reader_is_probed_again_without_blocking_updates);
    RUN_TEST(test_operational_log_carries_no_tag_identity_or_data);
    return UNITY_END();
}
