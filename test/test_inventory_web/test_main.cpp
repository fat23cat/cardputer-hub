#include <unity.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "../support/fake_nfc_reader.h"
#include "../support/memory_file_storage.h"
#include "core/capabilities/capability_registry.h"
#include "services/inventory/inventory_web_endpoint.h"

using namespace cardputer_hub::connectivity;
using namespace cardputer_hub::core;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;

namespace {

constexpr char recordHex[] = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

class FixedRandom final : public IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index)
            destination[index] = static_cast<std::uint8_t>(index + 1);
    }
};

InventoryId idFor(std::size_t index) {
    InventoryId id{};
    id[0] = 0x10;
    id[14] = static_cast<std::uint8_t>(index >> 8);
    id[15] = static_cast<std::uint8_t>(index & 0xff);
    return id;
}

InventoryRecord record(const InventoryId& id, std::uint32_t revision, std::string name,
                       std::string description = "") {
    InventoryRecord result;
    result.id = id;
    result.revision = revision;
    result.name = std::move(name);
    result.description = std::move(description);
    return result;
}

struct Harness {
    Harness()
        : nfc(reader, capabilities), files(card), storage(files, capabilities),
          inventory(nfc, storage, random), endpoint(inventory) {}

    FakeNfcReader reader;
    CapabilityRegistry capabilities;
    NfcService nfc;
    MemoryFileStorageAdapter card;
    FileStorage files;
    RemovableStorageService storage;
    FixedRandom random;
    InventoryService inventory;
    InventoryWebEndpoint endpoint;

    void store(const InventoryRecord& value) {
        card.files[InventoryStore::pathFor(value.id)] = bytesOf(*encodeInventoryRecord(value));
    }
    [[nodiscard]] std::optional<InventoryRecord> stored(const InventoryId& id) const {
        const auto text = card.text(InventoryStore::pathFor(id));
        if (!text)
            return std::nullopt;
        return decodeInventoryRecord(*text).record;
    }

    HttpResponse call(HttpMethod method, const std::string& subpath, std::string query = "",
                      std::string body = "") {
        HttpRequest request;
        request.method = method;
        request.path = "/api/inventory" + subpath;
        request.query = std::move(query);
        request.body = std::move(body);
        return endpoint.handle(request, subpath);
    }
};

std::size_t occurrences(const std::string& text, const std::string& part) {
    std::size_t count = 0;
    for (auto position = text.find(part); position != std::string::npos;
         position = text.find(part, position + 1))
        ++count;
    return count;
}

void test_identity() {
    Harness harness;
    TEST_ASSERT_EQUAL_STRING("inventory", std::string(harness.endpoint.id()).c_str());
    TEST_ASSERT_EQUAL_STRING("INVENTORY", std::string(harness.endpoint.title()).c_str());
}

void test_summary_counts_records() {
    Harness harness;
    harness.store(record(idFor(1), 1, "Bike tools"));
    harness.store(record(idFor(2), 1, "Cables"));
    TEST_ASSERT_EQUAL_STRING(R"({"storage":true,"count":2,"limit":256})",
                             harness.endpoint.summaryJson().c_str());
    harness.card.currentState = FileStorageState::NotPresent;
    harness.card.refreshedState = FileStorageState::NotPresent;
    TEST_ASSERT_EQUAL_STRING(R"({"storage":false})", harness.endpoint.summaryJson().c_str());
}

void test_list_returns_records_and_damaged_files() {
    Harness harness;
    const auto first = record(idFor(1), 3, "Bike tools", "Hex keys\nPump");
    harness.store(first);
    harness.card.files[InventoryStore::pathFor(idFor(2))] = bytesOf("{broken");
    const auto response = harness.call(HttpMethod::Get, "/records");
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    const std::string expected = std::string(R"({"records":[)") + *encodeInventoryRecord(first) +
                                 R"(,{"id":")" + inventoryIdHex(idFor(2)) +
                                 R"(","damaged":true}],"next":null,"total":2,"limit":256})";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), response.body.c_str());
}

void test_list_pages_follow_next() {
    Harness harness;
    constexpr std::size_t count = 40;
    for (std::size_t index = 0; index < count; ++index)
        harness.store(record(idFor(index), 1, "Box " + std::to_string(index)));

    std::set<std::string> seen;
    std::string query;
    for (int page = 0; page < 10; ++page) {
        const auto response = harness.call(HttpMethod::Get, "/records", query);
        TEST_ASSERT_EQUAL_UINT16(200, response.status);
        TEST_ASSERT_LESS_OR_EQUAL_size_t(InventoryWebEndpoint::maxPageBytes + 128,
                                         response.body.size());
        TEST_ASSERT_LESS_OR_EQUAL_size_t(InventoryWebEndpoint::maxPageRecords,
                                         occurrences(response.body, R"("schema":)"));
        for (std::size_t index = 0; index < count; ++index) {
            const auto hex = inventoryIdHex(idFor(index));
            if (response.body.find(R"({"schema":2,"id":")" + hex) != std::string::npos)
                TEST_ASSERT_TRUE(seen.insert(hex).second);
        }
        const auto next = response.body.find(R"("next":")");
        if (next == std::string::npos) {
            TEST_ASSERT_NOT_EQUAL(std::string::npos, response.body.find(R"("next":null)"));
            break;
        }
        query = "after=" + response.body.substr(next + 8, inventoryIdHexLength);
    }
    TEST_ASSERT_EQUAL_size_t(count, seen.size());
}

void test_list_page_stays_within_byte_budget() {
    Harness harness;
    // Two lines that together use the whole description limit.
    const std::string description((inventoryMaxDescriptionLength - 1) / 2, 'x');
    for (std::size_t index = 0; index < 12; ++index)
        harness.store(record(idFor(index), 1, "Box", description + "\n" + description));
    const auto response = harness.call(HttpMethod::Get, "/records");
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(InventoryWebEndpoint::maxPageBytes + 128,
                                     response.body.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, response.body.find(R"("next":")"));
}

void test_list_rejects_bad_cursor() {
    Harness harness;
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Get, "/records", "after=xyz").status);
}

void test_get_returns_record_or_status() {
    Harness harness;
    const auto value = record(idFor(1), 2, "Кладовка", "Шары\nМишура");
    harness.store(value);
    const auto response = harness.call(HttpMethod::Get, "/records/" + inventoryIdHex(idFor(1)));
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING(encodeInventoryRecord(value)->c_str(), response.body.c_str());

    TEST_ASSERT_EQUAL_UINT16(
        404, harness.call(HttpMethod::Get, "/records/" + std::string(recordHex)).status);
    harness.card.files[InventoryStore::pathFor(idFor(2))] = bytesOf("{broken");
    TEST_ASSERT_EQUAL_UINT16(
        422, harness.call(HttpMethod::Get, "/records/" + inventoryIdHex(idFor(2))).status);
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Get, "/records/nothex").status);
    TEST_ASSERT_EQUAL_UINT16(404, harness.call(HttpMethod::Get, "/other").status);
    TEST_ASSERT_EQUAL_UINT16(405, harness.call(HttpMethod::Post, "/records").status);
}

void test_put_commits_next_revision() {
    Harness harness;
    harness.store(record(idFor(1), 3, "Bike tools"));
    const auto edited = record(idFor(1), 3, "Bike tools", "Hex keys\nChain tool");
    const auto response = harness.call(HttpMethod::Put, "/records/" + inventoryIdHex(idFor(1)), "",
                                       *encodeInventoryRecord(edited));
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"revision":4})", response.body.c_str());
    const auto stored = harness.stored(idFor(1));
    TEST_ASSERT_TRUE(stored.has_value());
    TEST_ASSERT_EQUAL_UINT32(4, stored->revision);
    TEST_ASSERT_EQUAL_STRING("Hex keys\nChain tool", stored->description.c_str());
}

void test_put_accepts_browser_json() {
    Harness harness;
    harness.store(record(idFor(1), 1, "Box"));
    const std::string body = R"({"schema":2,"id":")" + inventoryIdHex(idFor(1)) +
                             R"(","revision":1,"name":"Ёлка \"новая\"","description":"a\nb"})";
    const auto response =
        harness.call(HttpMethod::Put, "/records/" + inventoryIdHex(idFor(1)), "", body);
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING("Ёлка \"новая\"", harness.stored(idFor(1))->name.c_str());
}

void test_put_conflict_reports_current_revision() {
    Harness harness;
    harness.store(record(idFor(1), 4, "Bike tools"));
    const auto stale = record(idFor(1), 3, "Mine");
    const auto response = harness.call(HttpMethod::Put, "/records/" + inventoryIdHex(idFor(1)), "",
                                       *encodeInventoryRecord(stale));
    TEST_ASSERT_EQUAL_UINT16(409, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"revision":4})", response.body.c_str());
    TEST_ASSERT_EQUAL_STRING("Bike tools", harness.stored(idFor(1))->name.c_str());
}

void test_put_rejects_invalid_or_mismatched_record() {
    Harness harness;
    harness.store(record(idFor(1), 1, "Box"));
    const auto path = "/records/" + inventoryIdHex(idFor(1));
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Put, path, "", "{}").status);
    TEST_ASSERT_EQUAL_UINT16(400, harness
                                      .call(HttpMethod::Put, path, "",
                                            *encodeInventoryRecord(record(idFor(2), 1, "Other id")))
                                      .status);
    const std::string blankName = R"({"schema":2,"id":")" + inventoryIdHex(idFor(1)) +
                                  R"(","revision":1,"name":" ","description":""})";
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Put, path, "", blankName).status);
    TEST_ASSERT_EQUAL_UINT16(404,
                             harness
                                 .call(HttpMethod::Put, "/records/" + inventoryIdHex(idFor(9)), "",
                                       *encodeInventoryRecord(record(idFor(9), 1, "Missing")))
                                 .status);
    TEST_ASSERT_EQUAL_UINT32(1, harness.stored(idFor(1))->revision);
}

void test_delete_checks_revision() {
    Harness harness;
    harness.store(record(idFor(1), 2, "Box"));
    const auto path = "/records/" + inventoryIdHex(idFor(1));
    const auto conflict = harness.call(HttpMethod::Delete, path, "revision=1");
    TEST_ASSERT_EQUAL_UINT16(409, conflict.status);
    TEST_ASSERT_EQUAL_STRING(R"({"revision":2})", conflict.body.c_str());
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Delete, path).status);
    TEST_ASSERT_EQUAL_UINT16(400, harness.call(HttpMethod::Delete, path, "revision=x").status);
    TEST_ASSERT_TRUE(harness.stored(idFor(1)).has_value());
    TEST_ASSERT_EQUAL_UINT16(204, harness.call(HttpMethod::Delete, path, "revision=2").status);
    TEST_ASSERT_FALSE(harness.stored(idFor(1)).has_value());
    TEST_ASSERT_EQUAL_UINT16(404, harness.call(HttpMethod::Delete, path, "revision=2").status);
}

void test_delete_removes_damaged_file() {
    Harness harness;
    harness.card.files[InventoryStore::pathFor(idFor(1))] = bytesOf("{broken");
    TEST_ASSERT_EQUAL_UINT16(
        204, harness.call(HttpMethod::Delete, "/records/" + inventoryIdHex(idFor(1)), "revision=0")
                 .status);
    TEST_ASSERT_EQUAL_size_t(0, harness.card.files.count(InventoryStore::pathFor(idFor(1))));
}

void test_storage_unavailable_is_503() {
    Harness harness;
    harness.store(record(idFor(1), 1, "Box"));
    harness.card.currentState = FileStorageState::NotPresent;
    harness.card.refreshedState = FileStorageState::NotPresent;
    const auto path = "/records/" + inventoryIdHex(idFor(1));
    const auto list = harness.call(HttpMethod::Get, "/records");
    TEST_ASSERT_EQUAL_UINT16(503, list.status);
    TEST_ASSERT_EQUAL_STRING(R"({"error":"storage"})", list.body.c_str());
    TEST_ASSERT_EQUAL_UINT16(503, harness.call(HttpMethod::Get, path).status);
    TEST_ASSERT_EQUAL_UINT16(
        503,
        harness.call(HttpMethod::Put, path, "", *encodeInventoryRecord(record(idFor(1), 1, "X")))
            .status);
    TEST_ASSERT_EQUAL_UINT16(503, harness.call(HttpMethod::Delete, path, "revision=1").status);
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_summary_counts_records);
    RUN_TEST(test_list_returns_records_and_damaged_files);
    RUN_TEST(test_list_pages_follow_next);
    RUN_TEST(test_list_page_stays_within_byte_budget);
    RUN_TEST(test_list_rejects_bad_cursor);
    RUN_TEST(test_get_returns_record_or_status);
    RUN_TEST(test_put_commits_next_revision);
    RUN_TEST(test_put_accepts_browser_json);
    RUN_TEST(test_put_conflict_reports_current_revision);
    RUN_TEST(test_put_rejects_invalid_or_mismatched_record);
    RUN_TEST(test_delete_checks_revision);
    RUN_TEST(test_delete_removes_damaged_file);
    RUN_TEST(test_storage_unavailable_is_503);
    return UNITY_END();
}
