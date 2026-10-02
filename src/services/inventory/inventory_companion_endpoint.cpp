#include "services/inventory/inventory_companion_endpoint.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace cardputer_hub::services {
namespace {

using connectivity::CompanionEnvelope;
using connectivity::CompanionOperation;
using connectivity::CompanionStatus;

CompanionEnvelope response(const CompanionInboundRequest& request, CompanionStatus status) {
    return connectivity::makeResponse(request.session, request.requestId, request.operation,
                                      status);
}

} // namespace

void InventoryCompanionEndpoint::update(std::chrono::milliseconds elapsed) {
    if (companion_.sessionEpoch() != epoch_) {
        // A transfer never outlives its session.
        epoch_ = companion_.sessionEpoch();
        upload_.reset();
        download_.reset();
    }
    if (upload_) {
        upload_->idle += elapsed;
        if (upload_->idle >= uploadIdleTimeout)
            upload_.reset();
    }
    while (const auto request = companion_.takeInboundRequest())
        handle(*request);
}

void InventoryCompanionEndpoint::handle(const CompanionInboundRequest& request) {
    switch (request.operation) {
    case CompanionOperation::InventoryList:
        handleList(request);
        return;
    case CompanionOperation::InventoryGet:
        handleGet(request);
        return;
    case CompanionOperation::InventoryPut:
        handlePut(request);
        return;
    case CompanionOperation::InventoryDelete:
        handleDelete(request);
        return;
    default:
        reply(request, CompanionStatus::Unsupported);
        return;
    }
}

void InventoryCompanionEndpoint::reply(const CompanionInboundRequest& request,
                                       CompanionStatus status) {
    (void)companion_.respond(request, response(request, status));
}

void InventoryCompanionEndpoint::replyStore(const CompanionInboundRequest& request,
                                            InventoryStoreStatus status, std::uint32_t revision) {
    switch (status) {
    case InventoryStoreStatus::Ok:
        reply(request, CompanionStatus::Ok);
        return;
    case InventoryStoreStatus::NotFound:
        reply(request, CompanionStatus::NotFound);
        return;
    case InventoryStoreStatus::Unavailable:
        reply(request, CompanionStatus::NotAvailable);
        return;
    case InventoryStoreStatus::Conflict: {
        // A damaged record has no revision to report: the base is simply wrong.
        auto message = response(request, CompanionStatus::Conflict);
        if (connectivity::setInventoryConflict(message, revision))
            (void)companion_.respond(request, message);
        else
            reply(request, CompanionStatus::Rejected);
        return;
    }
    case InventoryStoreStatus::Invalid:
    case InventoryStoreStatus::Rejected:
    case InventoryStoreStatus::Exists:
        reply(request, CompanionStatus::Rejected);
        return;
    case InventoryStoreStatus::TooMany:
    case InventoryStoreStatus::StorageError:
        reply(request, CompanionStatus::StorageError);
        return;
    }
}

// One page of the sorted listing, as many entries as fit one message.
void InventoryCompanionEndpoint::handleList(const CompanionInboundRequest& request) {
    std::uint16_t start = 0;
    if (!connectivity::readInventoryListRequest(request.message, start)) {
        reply(request, CompanionStatus::Malformed);
        return;
    }
    const auto listed = inventory_.listRecords();
    if (listed.status != InventoryStoreStatus::Ok) {
        replyStore(request, listed.status, 0);
        return;
    }
    const auto total = static_cast<std::uint16_t>(listed.ids.size());
    std::vector<InventoryRecord> records;
    std::vector<connectivity::CompanionInventoryListEntry> entries;
    std::size_t size = connectivity::companionInventoryListHeaderSize;
    std::size_t index = start;
    records.reserve(8);
    for (; index < listed.ids.size() && records.size() < 255; ++index) {
        const auto found = inventory_.getRecord(listed.ids[index]);
        if (found.status != InventoryStoreStatus::Ok &&
            found.status != InventoryStoreStatus::Invalid) {
            replyStore(request, found.status, 0);
            return;
        }
        const bool valid = found.status == InventoryStoreStatus::Ok;
        const auto nameBytes = valid ? found.record->name.size() : 0;
        if (size + connectivity::inventoryListEntrySize(nameBytes) >
            connectivity::companionMaxPayloadSize)
            break;
        size += connectivity::inventoryListEntrySize(nameBytes);
        records.push_back(valid ? *found.record : InventoryRecord{listed.ids[index], {}, {}, 0});
    }
    entries.reserve(records.size());
    std::transform(records.begin(), records.end(), std::back_inserter(entries),
                   [](const InventoryRecord& record) {
                       return connectivity::CompanionInventoryListEntry{
                           record.id, record.revision != 0, record.revision, record.name};
                   });
    auto message = response(request, CompanionStatus::Ok);
    const auto next = static_cast<std::uint16_t>(std::min<std::size_t>(index, total));
    if (!connectivity::setInventoryListResponse(message, total, next, entries.data(),
                                                entries.size())) {
        reply(request, CompanionStatus::StorageError);
        return;
    }
    (void)companion_.respond(request, message);
}

void InventoryCompanionEndpoint::handleGet(const CompanionInboundRequest& request) {
    InventoryId id{};
    std::uint16_t offset = 0;
    if (!connectivity::readInventoryGetRequest(request.message, id, offset)) {
        reply(request, CompanionStatus::Malformed);
        return;
    }
    // A download starts from a fresh read; later chunks come from that snapshot.
    if (offset == 0 || !download_ || download_->id != id) {
        download_.reset();
        auto found = inventory_.getRecord(id);
        if (found.status != InventoryStoreStatus::Ok) {
            replyStore(request, found.status, 0);
            return;
        }
        download_ = Download{id, found.record->revision, std::move(found.json)};
    }
    const auto& json = download_->json;
    if (offset >= json.size()) {
        reply(request, CompanionStatus::Malformed);
        return;
    }
    connectivity::CompanionInventoryChunk chunk{};
    chunk.id = id;
    chunk.revision = download_->revision;
    chunk.total = static_cast<std::uint16_t>(json.size());
    chunk.offset = offset;
    chunk.data = reinterpret_cast<const std::uint8_t*>(json.data()) + offset;
    chunk.size = std::min(json.size() - offset, connectivity::companionInventoryGetChunkSize);
    auto message = response(request, CompanionStatus::Ok);
    if (!connectivity::setInventoryGetResponse(message, chunk)) {
        reply(request, CompanionStatus::StorageError);
        return;
    }
    if (offset + chunk.size >= json.size())
        download_.reset();
    (void)companion_.respond(request, message);
}

void InventoryCompanionEndpoint::handlePut(const CompanionInboundRequest& request) {
    connectivity::CompanionInventoryChunk chunk{};
    if (!connectivity::readInventoryPutRequest(request.message, chunk)) {
        reply(request, CompanionStatus::Malformed);
        return;
    }
    if (chunk.offset == 0) {
        // A new upload replaces any unfinished one. A stale base is refused
        // before the rest of the record is sent.
        upload_.reset();
        const auto current = inventory_.getRecord(chunk.id);
        if (current.status != InventoryStoreStatus::Ok) {
            replyStore(request, current.status, 0);
            return;
        }
        if (current.record->revision != chunk.revision) {
            replyStore(request, InventoryStoreStatus::Conflict, current.record->revision);
            return;
        }
        upload_ = Upload{chunk.id, chunk.revision, chunk.total, {}, {}};
        upload_->data.reserve(chunk.total);
    } else if (!upload_ || upload_->id != chunk.id || upload_->expectedRevision != chunk.revision ||
               upload_->total != chunk.total || upload_->data.size() != chunk.offset) {
        upload_.reset();
        reply(request, CompanionStatus::Rejected);
        return;
    }
    upload_->data.append(reinterpret_cast<const char*>(chunk.data), chunk.size);
    upload_->idle = {};
    const auto received = static_cast<std::uint16_t>(upload_->data.size());
    std::uint32_t committed = 0;
    if (received == upload_->total) {
        const auto upload = std::move(*upload_);
        upload_.reset();
        const auto decoded = decodeInventoryRecord(upload.data);
        if (!decoded.record || decoded.record->id != upload.id ||
            decoded.record->revision != upload.expectedRevision) {
            reply(request, CompanionStatus::Rejected);
            return;
        }
        const auto put = inventory_.putRecord(*decoded.record, upload.expectedRevision);
        if (put.status != InventoryStoreStatus::Ok) {
            replyStore(request, put.status, put.revision);
            return;
        }
        committed = put.revision;
    }
    auto message = response(request, CompanionStatus::Ok);
    (void)connectivity::setInventoryPutResponse(message, received, committed);
    (void)companion_.respond(request, message);
}

void InventoryCompanionEndpoint::handleDelete(const CompanionInboundRequest& request) {
    InventoryId id{};
    std::uint32_t expectedRevision = 0;
    if (!connectivity::readInventoryDeleteRequest(request.message, id, expectedRevision)) {
        reply(request, CompanionStatus::Malformed);
        return;
    }
    if (download_ && download_->id == id)
        download_.reset();
    if (upload_ && upload_->id == id)
        upload_.reset();
    const auto removed = inventory_.deleteRecord(id, expectedRevision);
    replyStore(request, removed.status, removed.revision);
}

} // namespace cardputer_hub::services
