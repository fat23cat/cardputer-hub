#include <unity.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "../support/companion_session.h"
#include "../support/fake_nfc_reader.h"
#include "../support/memory_file_storage.h"
#include "core/capabilities/capability_registry.h"
#include "services/inventory/inventory_companion_endpoint.h"

using namespace cardputer_hub::connectivity;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;
using namespace std::chrono_literals;

namespace {

constexpr char recordHex[] = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

class FakeTransport final : public ICompanionTransport {
  public:
    CompanionTransportState state() const noexcept override { return transportState; }
    CompanionSendResult send(const CompanionPayload& payload) override {
        sent.push_back(payload);
        return CompanionSendResult::Sent;
    }
    std::optional<CompanionPayload> receive() override {
        if (incoming.empty())
            return std::nullopt;
        const auto payload = incoming.front();
        incoming.pop_front();
        return payload;
    }
    CompanionTransportState transportState = CompanionTransportState::Ready;
    std::vector<CompanionPayload> sent;
    std::deque<CompanionPayload> incoming;
};

class FixedRandom final : public IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = static_cast<std::uint8_t>(index + 1);
    }
};

InventoryId recordId() { return *parseInventoryIdHex(recordHex); }

CompanionInventoryId wireId(const InventoryId& id) {
    CompanionInventoryId wire{};
    std::copy(id.begin(), id.end(), wire.begin());
    return wire;
}

// A record whose description has `lines` lines.
InventoryRecord sampleRecord(std::size_t lines) {
    InventoryRecord record;
    record.id = recordId();
    record.name = "Кладовка";
    for (std::size_t index = 0; index < lines; ++index)
        record.description +=
            (index == 0 ? "" : "\n") + std::string("Коробка номер ") + std::to_string(index + 1);
    record.revision = 2;
    return record;
}

std::size_t lineCount(const std::string& text) {
    return text.empty() ? 0
                        : static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

struct Harness {
    Harness()
        : nfc(reader, capabilities), files(card), storage(files, capabilities),
          inventory(nfc, storage, random), companion(transport, capabilities),
          endpoint(companion, inventory) {
        completeCompanionHandshake(transport, companion);
        epochAfterHandshake = companion.sessionEpoch();
    }

    FakeNfcReader reader;
    CapabilityRegistry capabilities;
    NfcService nfc;
    MemoryFileStorageAdapter card;
    FileStorage files;
    RemovableStorageService storage;
    FixedRandom random;
    InventoryService inventory;
    FakeTransport transport;
    CompanionService companion;
    InventoryCompanionEndpoint endpoint;
    std::uint8_t nextRequest = 100;
    std::uint32_t epochAfterHandshake = 0;

    void storeRecord(const InventoryRecord& record) {
        const auto json = *encodeInventoryRecord(record);
        card.files[InventoryStore::pathFor(record.id)] = bytesOf(json);
    }
    [[nodiscard]] std::string storedJson() const {
        return *card.text(InventoryStore::pathFor(recordId()));
    }

    // Sends one Mac request and returns the Cardputer's answer to it.
    CompanionEnvelope exchange(CompanionEnvelope request) {
        request.session = companion.session();
        request.requestId = nextRequest++;
        const auto before = transport.sent.size();
        transport.incoming.push_back(companionWire(request));
        companion.update(0ms);
        endpoint.update(0ms);
        TEST_ASSERT_EQUAL_UINT(before + 1, transport.sent.size());
        const auto answer = companionDecode(transport.sent.back());
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionKind::Response),
                                static_cast<unsigned>(answer.kind));
        TEST_ASSERT_EQUAL_UINT8(request.requestId, answer.requestId);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(request.operation),
                                static_cast<unsigned>(answer.operation));
        return answer;
    }

    CompanionEnvelope putChunk(const std::string& json, std::uint32_t expected, std::size_t offset,
                               std::size_t size) {
        auto request = makeRequest(1, 1, CompanionOperation::InventoryPut);
        CompanionInventoryChunk chunk{};
        chunk.id = wireId(recordId());
        chunk.revision = expected;
        chunk.total = static_cast<std::uint16_t>(json.size());
        chunk.offset = static_cast<std::uint16_t>(offset);
        chunk.data = reinterpret_cast<const std::uint8_t*>(json.data()) + offset;
        chunk.size = size;
        TEST_ASSERT_TRUE(setInventoryPutRequest(request, chunk));
        return exchange(request);
    }

    // Uploads `json` in full; returns the answer to the last chunk.
    CompanionEnvelope upload(const std::string& json, std::uint32_t expected) {
        CompanionEnvelope answer{};
        for (std::size_t offset = 0; offset < json.size();
             offset += companionInventoryPutChunkSize) {
            const auto size = std::min(companionInventoryPutChunkSize, json.size() - offset);
            answer = putChunk(json, expected, offset, size);
            if (answer.status != CompanionStatus::Ok)
                return answer;
        }
        return answer;
    }
};

void assertStatus(CompanionStatus expected, const CompanionEnvelope& answer) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(answer.status));
}

std::uint32_t read32At(const CompanionEnvelope& message, std::size_t offset) {
    const auto* bytes = message.payload.data() + offset;
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void test_list_pages_names_and_marks_invalid_records() {
    Harness h;
    h.storeRecord(sampleRecord(1));
    h.card.files["inventory/records/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.json"] = bytesOf("{");
    auto request = makeRequest(1, 1, CompanionOperation::InventoryList);
    TEST_ASSERT_TRUE(setInventoryListRequest(request, 0));
    const auto answer = h.exchange(request);
    assertStatus(CompanionStatus::Ok, answer);
    const auto* bytes = answer.payload.data();
    TEST_ASSERT_EQUAL_UINT8(2, bytes[0]); // total
    TEST_ASSERT_EQUAL_UINT8(2, bytes[2]); // next
    TEST_ASSERT_EQUAL_UINT8(2, bytes[4]); // count
    // Sorted by ID: the valid record first, then the damaged file.
    TEST_ASSERT_EQUAL_UINT8(0x0f, bytes[5]);
    TEST_ASSERT_EQUAL_UINT8(1, bytes[21]);
    TEST_ASSERT_EQUAL_UINT32(2, read32At(answer, 22));
    TEST_ASSERT_EQUAL_UINT8(std::string("Кладовка").size(), bytes[26]);
    const std::size_t second = 5 + inventoryListEntrySize(bytes[26]);
    TEST_ASSERT_EQUAL_UINT8(0xaa, bytes[second]);
    TEST_ASSERT_EQUAL_UINT8(0, bytes[second + 16]);

    TEST_ASSERT_TRUE(setInventoryListRequest(request, 2));
    const auto end = h.exchange(request);
    assertStatus(CompanionStatus::Ok, end);
    TEST_ASSERT_EQUAL_UINT8(0, end.payload[4]);
}

void test_list_without_a_card_is_not_available() {
    Harness h;
    h.card.currentState = FileStorageState::NotPresent;
    h.card.refreshedState = FileStorageState::NotPresent;
    auto request = makeRequest(1, 1, CompanionOperation::InventoryList);
    TEST_ASSERT_TRUE(setInventoryListRequest(request, 0));
    assertStatus(CompanionStatus::NotAvailable, h.exchange(request));
}

void test_list_reports_record_read_error_instead_of_damage() {
    Harness h;
    h.storeRecord(sampleRecord(1));
    h.card.readBackendErrorPath = InventoryStore::pathFor(recordId());
    auto request = makeRequest(1, 1, CompanionOperation::InventoryList);
    TEST_ASSERT_TRUE(setInventoryListRequest(request, 0));
    assertStatus(CompanionStatus::StorageError, h.exchange(request));
    h.card.readBackendErrorPath.reset();
    assertStatus(CompanionStatus::Ok, h.exchange(request));
}

void test_get_serves_one_revision_in_bounded_chunks() {
    Harness h;
    const auto record = sampleRecord(30);
    h.storeRecord(record);
    const auto expected = *encodeInventoryRecord(record);
    TEST_ASSERT_TRUE(expected.size() > 2 * companionInventoryGetChunkSize);

    std::string received;
    while (true) {
        auto request = makeRequest(1, 1, CompanionOperation::InventoryGet);
        TEST_ASSERT_TRUE(setInventoryGetRequest(request, wireId(recordId()),
                                                static_cast<std::uint16_t>(received.size())));
        const auto answer = h.exchange(request);
        assertStatus(CompanionStatus::Ok, answer);
        CompanionInventoryChunk chunk{};
        TEST_ASSERT_TRUE(readInventoryGetResponse(answer, chunk));
        TEST_ASSERT_EQUAL_UINT32(2, chunk.revision);
        TEST_ASSERT_EQUAL_UINT16(expected.size(), chunk.total);
        TEST_ASSERT_EQUAL_UINT16(received.size(), chunk.offset);
        received.append(reinterpret_cast<const char*>(chunk.data), chunk.size);
        if (received.size() == chunk.total)
            break;
    }
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), received.c_str());

    auto missing = makeRequest(1, 1, CompanionOperation::InventoryGet);
    TEST_ASSERT_TRUE(setInventoryGetRequest(
        missing, wireId(*parseInventoryIdHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")), 0));
    assertStatus(CompanionStatus::NotFound, h.exchange(missing));
}

void test_inventory_put_revision_round_trip_in_chunks() {
    Harness h;
    h.storeRecord(sampleRecord(1));
    auto edited = sampleRecord(25);
    edited.name = "Кладовка у входа";
    const auto json = *encodeInventoryRecord(edited);
    TEST_ASSERT_TRUE(json.size() > companionInventoryPutChunkSize);
    const auto before = h.storedJson();

    // Every chunk before the last is acknowledged without committing anything.
    const auto first = h.putChunk(json, 2, 0, companionInventoryPutChunkSize);
    assertStatus(CompanionStatus::Ok, first);
    TEST_ASSERT_EQUAL_UINT32(0, read32At(first, 2));
    TEST_ASSERT_EQUAL_STRING(before.c_str(), h.storedJson().c_str());
    TEST_ASSERT_TRUE(h.endpoint.uploadInProgress());

    CompanionEnvelope last{};
    for (std::size_t offset = companionInventoryPutChunkSize; offset < json.size();
         offset += companionInventoryPutChunkSize)
        last = h.putChunk(json, 2, offset,
                          std::min(companionInventoryPutChunkSize, json.size() - offset));
    assertStatus(CompanionStatus::Ok, last);
    TEST_ASSERT_EQUAL_UINT32(3, read32At(last, 2));
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());
    const auto stored = decodeInventoryRecord(h.storedJson());
    TEST_ASSERT_EQUAL_STRING("Кладовка у входа", stored.record->name.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, stored.record->revision);
    TEST_ASSERT_EQUAL_UINT(25, lineCount(stored.record->description));
}

void test_inventory_transfer_cancel_and_revision_conflict() {
    Harness h;
    h.storeRecord(sampleRecord(1));
    auto edited = sampleRecord(25);
    const auto json = *encodeInventoryRecord(edited);

    // A stale base revision is refused before the record is sent.
    const auto stale = h.putChunk(json, 1, 0, companionInventoryPutChunkSize);
    assertStatus(CompanionStatus::Conflict, stale);
    TEST_ASSERT_EQUAL_UINT32(2, read32At(stale, 0));
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());

    // The record advances while an upload is in flight: the commit is refused.
    assertStatus(CompanionStatus::Ok, h.putChunk(json, 2, 0, companionInventoryPutChunkSize));
    auto other = sampleRecord(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(InventoryStoreStatus::Ok),
                            static_cast<unsigned>(h.inventory.putRecord(other, 2).status));
    CompanionEnvelope last{};
    for (std::size_t offset = companionInventoryPutChunkSize; offset < json.size();
         offset += companionInventoryPutChunkSize)
        last = h.putChunk(json, 2, offset,
                          std::min(companionInventoryPutChunkSize, json.size() - offset));
    assertStatus(CompanionStatus::Conflict, last);
    TEST_ASSERT_EQUAL_UINT32(3, read32At(last, 0));
    TEST_ASSERT_EQUAL_UINT(2, lineCount(decodeInventoryRecord(h.storedJson()).record->description));

    // The session drops mid-upload: the partial upload is discarded and the
    // next session cannot continue it.
    const auto before = h.storedJson();
    assertStatus(CompanionStatus::Ok, h.putChunk(json, 3, 0, companionInventoryPutChunkSize));
    h.transport.transportState = CompanionTransportState::Unavailable;
    h.companion.update(0ms);
    h.endpoint.update(0ms);
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());
    h.transport.transportState = CompanionTransportState::Ready;
    h.companion.update(0ms);
    completeCompanionHandshake(h.transport, h.companion);
    h.endpoint.update(0ms);
    const auto resumed =
        h.putChunk(json, 3, companionInventoryPutChunkSize, companionInventoryPutChunkSize);
    assertStatus(CompanionStatus::Rejected, resumed);
    TEST_ASSERT_EQUAL_STRING(before.c_str(), h.storedJson().c_str());
}

void test_out_of_order_invalid_and_idle_uploads_change_nothing() {
    Harness h;
    h.storeRecord(sampleRecord(1));
    const auto before = h.storedJson();
    const auto json = *encodeInventoryRecord(sampleRecord(25));

    assertStatus(CompanionStatus::Ok, h.putChunk(json, 2, 0, companionInventoryPutChunkSize));
    assertStatus(CompanionStatus::Rejected,
                 h.putChunk(json, 2, 2 * companionInventoryPutChunkSize, 10));
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());

    // Complete JSON that is not a valid record for this ID is refused.
    auto wrongId = sampleRecord(1);
    wrongId.id = *parseInventoryIdHex("cccccccccccccccccccccccccccccccc");
    std::string mismatched = *encodeInventoryRecord(wrongId);
    assertStatus(CompanionStatus::Rejected, h.upload(mismatched, 2));
    std::string broken = before.substr(0, before.size() - 1);
    assertStatus(CompanionStatus::Rejected, h.upload(broken, 2));

    assertStatus(CompanionStatus::Ok, h.putChunk(json, 2, 0, companionInventoryPutChunkSize));
    h.endpoint.update(InventoryCompanionEndpoint::uploadIdleTimeout);
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), h.storedJson().c_str());
}

void test_delete_checks_the_revision_and_drops_transfers() {
    Harness h;
    h.storeRecord(sampleRecord(25));
    const auto json = *encodeInventoryRecord(sampleRecord(25));
    auto request = makeRequest(1, 1, CompanionOperation::InventoryDelete);
    TEST_ASSERT_TRUE(setInventoryDeleteRequest(request, wireId(recordId()), 1));
    const auto stale = h.exchange(request);
    assertStatus(CompanionStatus::Conflict, stale);
    TEST_ASSERT_EQUAL_UINT32(2, read32At(stale, 0));
    TEST_ASSERT_TRUE(h.card.text(InventoryStore::pathFor(recordId())).has_value());

    // A delete ends an upload of the same record.
    assertStatus(CompanionStatus::Ok, h.putChunk(json, 2, 0, companionInventoryPutChunkSize));
    TEST_ASSERT_TRUE(setInventoryDeleteRequest(request, wireId(recordId()), 2));
    const auto removed = h.exchange(request);
    assertStatus(CompanionStatus::Ok, removed);
    TEST_ASSERT_EQUAL_UINT8(0, removed.payloadSize);
    TEST_ASSERT_FALSE(h.endpoint.uploadInProgress());
    TEST_ASSERT_FALSE(h.card.text(InventoryStore::pathFor(recordId())).has_value());
    assertStatus(CompanionStatus::NotFound, h.exchange(request));

    // A damaged file goes only with revision 0; a non-zero base is refused.
    h.card.files[InventoryStore::pathFor(recordId())] = bytesOf("{");
    assertStatus(CompanionStatus::Rejected, h.exchange(request));
    TEST_ASSERT_TRUE(setInventoryDeleteRequest(request, wireId(recordId()), 0));
    assertStatus(CompanionStatus::Ok, h.exchange(request));
    TEST_ASSERT_TRUE(h.card.files.empty());
}

void test_inventory_requests_need_a_ready_session() {
    Harness h;
    // A request from an earlier session is ignored, not answered.
    auto request = makeRequest(static_cast<std::uint16_t>(h.companion.session() + 1), 9,
                               CompanionOperation::InventoryList);
    TEST_ASSERT_TRUE(setInventoryListRequest(request, 0));
    const auto sent = h.transport.sent.size();
    h.transport.incoming.push_back(companionWire(request));
    h.companion.update(0ms);
    h.endpoint.update(0ms);
    TEST_ASSERT_EQUAL_UINT(sent, h.transport.sent.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CompanionServiceState::Ready),
                            static_cast<unsigned>(h.companion.state()));
    TEST_ASSERT_EQUAL_UINT32(h.epochAfterHandshake, h.companion.sessionEpoch());
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_list_pages_names_and_marks_invalid_records);
    RUN_TEST(test_list_without_a_card_is_not_available);
    RUN_TEST(test_list_reports_record_read_error_instead_of_damage);
    RUN_TEST(test_get_serves_one_revision_in_bounded_chunks);
    RUN_TEST(test_inventory_put_revision_round_trip_in_chunks);
    RUN_TEST(test_inventory_transfer_cancel_and_revision_conflict);
    RUN_TEST(test_out_of_order_invalid_and_idle_uploads_change_nothing);
    RUN_TEST(test_delete_checks_the_revision_and_drops_transfers);
    RUN_TEST(test_inventory_requests_need_a_ready_session);
    return UNITY_END();
}
