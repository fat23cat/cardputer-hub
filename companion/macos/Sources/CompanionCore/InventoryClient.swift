import Foundation

public enum InventoryError: Error, Equatable {
    case disconnected
    case timeout
    case busy
    /// The Cardputer has no mounted microSD card.
    case storageUnavailable
    case notFound
    case conflict(current: UInt32)
    /// The stored record is damaged, or the Cardputer refused the submitted one.
    case rejected
    case storageError
    case malformed
    /// The local draft breaks the shared limits; nothing was sent.
    case invalidRecord

    public var message: String {
        switch self {
        case .disconnected: return "Cardputer disconnected"
        case .timeout: return "Cardputer did not answer"
        case .busy: return "Another transfer is in progress"
        case .storageUnavailable: return "The Cardputer microSD card is not available"
        case .notFound: return "This record is no longer on the Cardputer"
        case .conflict: return "The record changed on the Cardputer"
        case .rejected: return "The Cardputer rejected the record"
        case .storageError: return "The Cardputer could not access its microSD card"
        case .malformed: return "Unexpected answer from the Cardputer"
        case .invalidRecord: return "The record is not valid"
        }
    }
}

/// Inventory domain operations over the live Companion session. Records move as
/// canonical JSON in bounded chunks, one request at a time; the Cardputer
/// validates and commits every write. Nothing is retried or replayed after a
/// failure: callers reload instead.
public final class InventoryClient {
    private let transport: CompanionRequesting
    /// Restarts of a download whose revision changed between chunks.
    public static let maxDownloadRestarts = 2

    public init(transport: CompanionRequesting) {
        self.transport = transport
    }

    public func list(completion: @escaping (Result<[InventoryListEntry], InventoryError>) -> Void) {
        listPage(start: 0, collected: [], completion: completion)
    }

    public func get(_ id: InventoryId,
                    completion: @escaping (Result<InventoryRecord, InventoryError>) -> Void) {
        download(id, offset: 0, revision: nil, data: [], restarts: 0, completion: completion)
    }

    /// Saves `record`, whose `revision` is the one the edit was based on. On
    /// success the result is the committed revision.
    public func put(_ record: InventoryRecord,
                    completion: @escaping (Result<UInt32, InventoryError>) -> Void) {
        guard let json = record.canonicalJSON() else { return completion(.failure(.invalidRecord)) }
        upload(record, json: json, offset: 0, completion: completion)
    }

    /// Deletes a record for good. `revision` is the one the editor loaded, or 0
    /// for a record the Cardputer reports as damaged.
    public func delete(_ id: InventoryId, revision: UInt32,
                       completion: @escaping (Result<Void, InventoryError>) -> Void) {
        transport.sendRequest(.inventoryDelete,
                              payload: InventoryWire.deleteRequest(id: id, expectedRevision: revision)) { result in
            switch Self.payload(result) {
            case .failure(let error):
                completion(.failure(error))
            case .success(let payload):
                completion(payload.isEmpty ? .success(()) : .failure(.malformed))
            }
        }
    }

    private func listPage(start: Int, collected: [InventoryListEntry],
                          completion: @escaping (Result<[InventoryListEntry], InventoryError>) -> Void) {
        transport.sendRequest(.inventoryList, payload: InventoryWire.listRequest(start: start)) { [weak self] result in
            guard let self else { return }
            switch Self.payload(result) {
            case .failure(let error):
                completion(.failure(error))
            case .success(let payload):
                guard let page = InventoryWire.decodeListPage(payload), page.next >= start,
                      page.next - start == page.entries.count else {
                    return completion(.failure(.malformed))
                }
                let entries = collected + page.entries
                if page.next >= page.total || page.entries.isEmpty {
                    completion(.success(entries))
                } else {
                    self.listPage(start: page.next, collected: entries, completion: completion)
                }
            }
        }
    }

    private func download(_ id: InventoryId, offset: Int, revision: UInt32?, data: [UInt8], restarts: Int,
                          completion: @escaping (Result<InventoryRecord, InventoryError>) -> Void) {
        transport.sendRequest(.inventoryGet,
                              payload: InventoryWire.getRequest(id: id, offset: offset)) { [weak self] result in
            guard let self else { return }
            switch Self.payload(result) {
            case .failure(let error):
                completion(.failure(error))
            case .success(let payload):
                guard let chunk = InventoryWire.decodeGetChunk(payload), chunk.offset == offset else {
                    return completion(.failure(.malformed))
                }
                if let revision, chunk.revision != revision {
                    // The record changed mid-download: start again from one snapshot.
                    guard restarts < Self.maxDownloadRestarts else { return completion(.failure(.malformed)) }
                    return self.download(id, offset: 0, revision: nil, data: [], restarts: restarts + 1,
                                         completion: completion)
                }
                let received = data + chunk.data
                if received.count < chunk.total {
                    return self.download(id, offset: received.count, revision: chunk.revision,
                                         data: received, restarts: restarts, completion: completion)
                }
                guard let record = InventoryRecord.decode(received), record.id == id,
                      record.revision == chunk.revision else {
                    return completion(.failure(.malformed))
                }
                completion(.success(record))
            }
        }
    }

    private func upload(_ record: InventoryRecord, json: [UInt8], offset: Int,
                        completion: @escaping (Result<UInt32, InventoryError>) -> Void) {
        let size = min(InventoryLimits.putChunkSize, json.count - offset)
        guard let payload = InventoryWire.putRequest(id: record.id, expectedRevision: record.revision,
                                                     total: json.count, offset: offset,
                                                     data: Array(json[offset..<offset + size])) else {
            return completion(.failure(.invalidRecord))
        }
        transport.sendRequest(.inventoryPut, payload: payload) { [weak self] result in
            guard let self else { return }
            switch Self.payload(result) {
            case .failure(let error):
                completion(.failure(error))
            case .success(let payload):
                guard let ack = InventoryWire.decodePutAck(payload), ack.received == offset + size else {
                    return completion(.failure(.malformed))
                }
                if ack.received < json.count {
                    guard ack.committed == 0 else { return completion(.failure(.malformed)) }
                    return self.upload(record, json: json, offset: ack.received, completion: completion)
                }
                guard ack.committed == record.revision &+ 1 else { return completion(.failure(.malformed)) }
                completion(.success(ack.committed))
            }
        }
    }

    /// The OK payload of an answer, or the error it reports.
    private static func payload(_ result: Result<CompanionEnvelope, CompanionRequestFailure>)
        -> Result<[UInt8], InventoryError> {
        switch result {
        case .failure(.disconnected): return .failure(.disconnected)
        case .failure(.timeout): return .failure(.timeout)
        case .failure(.busy): return .failure(.busy)
        case .failure(.encoding): return .failure(.malformed)
        case .success(let message):
            switch message.status {
            case .ok: return .success(message.payload)
            case .notAvailable: return .failure(.storageUnavailable)
            case .notFound: return .failure(.notFound)
            case .conflict:
                guard let current = InventoryWire.conflictRevision(message.payload) else {
                    return .failure(.malformed)
                }
                return .failure(.conflict(current: current))
            case .rejected: return .failure(.rejected)
            case .storageError: return .failure(.storageError)
            case .unsupported, .malformed: return .failure(.malformed)
            }
        }
    }
}
