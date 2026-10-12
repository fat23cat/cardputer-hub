#include "services/inventory/inventory_web_endpoint.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "services/web_access/web_json.h"

namespace cardputer_hub::services {
namespace {

using connectivity::HttpMethod;
using connectivity::HttpResponse;

constexpr std::string_view recordsPath = "/records";

HttpResponse storeFailure(InventoryStoreStatus status, std::uint32_t revision) {
    switch (status) {
    case InventoryStoreStatus::NotFound:
        return webErrorResponse(404, "not-found");
    case InventoryStoreStatus::Invalid:
        return webErrorResponse(422, "damaged");
    case InventoryStoreStatus::Conflict:
        return webJsonResponse(409, R"({"revision":)" + std::to_string(revision) + "}");
    case InventoryStoreStatus::Rejected:
    case InventoryStoreStatus::Exists:
        return webErrorResponse(400, "rejected");
    case InventoryStoreStatus::Unavailable:
        return webErrorResponse(503, "storage");
    case InventoryStoreStatus::TooMany:
    case InventoryStoreStatus::StorageError:
    case InventoryStoreStatus::Ok:
        break;
    }
    return webErrorResponse(500, "storage-error");
}

std::optional<std::uint32_t> parseRevision(std::string_view text) {
    if (text.empty() || text.size() > 10)
        return std::nullopt;
    std::uint64_t value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9')
            return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(digit - '0');
    }
    if (value > UINT32_MAX)
        return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

} // namespace

std::string InventoryWebEndpoint::summaryJson() {
    const auto listed = inventory_.listRecords();
    if (listed.status != InventoryStoreStatus::Ok)
        return R"({"storage":false})";
    return R"({"storage":true,"count":)" + std::to_string(listed.ids.size()) + R"(,"limit":)" +
           std::to_string(inventoryMaxRecords) + "}";
}

HttpResponse InventoryWebEndpoint::handle(const connectivity::HttpRequest& request,
                                          std::string_view subpath) {
    if (subpath == recordsPath) {
        if (request.method != HttpMethod::Get)
            return webErrorResponse(405, "method");
        return list(request);
    }
    if (subpath.substr(0, recordsPath.size() + 1) != "/records/")
        return webErrorResponse(404, "not-found");
    const auto id = parseInventoryIdHex(subpath.substr(recordsPath.size() + 1));
    if (!id)
        return webErrorResponse(400, "id");
    switch (request.method) {
    case HttpMethod::Get:
        return get(*id);
    case HttpMethod::Put:
        return put(*id, request);
    case HttpMethod::Delete:
        return remove(*id, request);
    case HttpMethod::Post:
    case HttpMethod::Other:
        break;
    }
    return webErrorResponse(405, "method");
}

HttpResponse InventoryWebEndpoint::list(const connectivity::HttpRequest& request) {
    std::optional<InventoryId> after;
    if (const auto cursor = connectivity::httpQueryParameter(request.query, "after")) {
        after = parseInventoryIdHex(*cursor);
        if (!after)
            return webErrorResponse(400, "cursor");
    }
    const auto listed = inventory_.listRecords();
    if (listed.status != InventoryStoreStatus::Ok)
        return storeFailure(listed.status, 0);

    auto position =
        after ? std::upper_bound(listed.ids.begin(), listed.ids.end(), *after) : listed.ids.begin();
    std::string records;
    std::size_t count = 0;
    std::optional<InventoryId> last;
    for (; position != listed.ids.end() && count < maxPageRecords; ++position) {
        const auto found = inventory_.getRecord(*position);
        std::string item;
        if (found.status == InventoryStoreStatus::Ok) {
            item = found.json;
        } else if (found.status == InventoryStoreStatus::Invalid) {
            item = R"({"id":")" + inventoryIdHex(*position) + R"(","damaged":true})";
        } else if (found.status == InventoryStoreStatus::NotFound) {
            // Removed since the directory was listed.
            last = *position;
            continue;
        } else {
            return storeFailure(found.status, 0);
        }
        if (count != 0 && records.size() + item.size() + 1 > maxPageBytes)
            break;
        if (count != 0)
            records.push_back(',');
        records += item;
        ++count;
        last = *position;
    }
    std::string body = R"({"records":[)" + records + R"(],"next":)";
    if (position != listed.ids.end() && last)
        body += "\"" + inventoryIdHex(*last) + "\"";
    else
        body += "null";
    body += R"(,"total":)" + std::to_string(listed.ids.size()) + R"(,"limit":)" +
            std::to_string(inventoryMaxRecords) + "}";
    return webJsonResponse(200, std::move(body));
}

HttpResponse InventoryWebEndpoint::get(const InventoryId& id) {
    auto found = inventory_.getRecord(id);
    if (found.status != InventoryStoreStatus::Ok)
        return storeFailure(found.status, 0);
    return webJsonResponse(200, std::move(found.json));
}

HttpResponse InventoryWebEndpoint::put(const InventoryId& id,
                                       const connectivity::HttpRequest& request) {
    // The upload is the record at the revision the edit started from.
    const auto decoded = decodeInventoryRecord(request.body);
    if (!decoded.record || decoded.record->id != id)
        return webErrorResponse(400, "rejected");
    const auto result = inventory_.putRecord(*decoded.record, decoded.record->revision);
    if (result.status != InventoryStoreStatus::Ok)
        return storeFailure(result.status, result.revision);
    return webJsonResponse(200, R"({"revision":)" + std::to_string(result.revision) + "}");
}

HttpResponse InventoryWebEndpoint::remove(const InventoryId& id,
                                          const connectivity::HttpRequest& request) {
    const auto text = connectivity::httpQueryParameter(request.query, "revision");
    const auto revision = text ? parseRevision(*text) : std::nullopt;
    if (!revision)
        return webErrorResponse(400, "revision");
    const auto result = inventory_.deleteRecord(id, *revision);
    if (result.status != InventoryStoreStatus::Ok)
        return storeFailure(result.status, result.revision);
    return webEmptyResponse(204);
}

} // namespace cardputer_hub::services
