import Foundation

/// Limits shared with firmware (`services/inventory/inventory_record.h`).
public enum InventoryLimits {
    public static let schemaVersion = 2
    public static let maxRecordBytes = 4096
    public static let maxNameLength = 32
    public static let maxDescriptionLength = 900
    public static let maxNameBytes = 128
    public static let idSize = 16
    public static let getHeaderSize = 8
    public static let getChunkSize = CompanionConstants.maxPayloadSize - getHeaderSize
    public static let putHeaderSize = 24
    public static let putChunkSize = CompanionConstants.maxPayloadSize - putHeaderSize
    public static let listHeaderSize = 5
}

/// The random 128-bit locator an inventory tag carries. Not a secret.
public struct InventoryId: Hashable, Comparable, CustomStringConvertible {
    public let bytes: [UInt8]

    public init?(bytes: [UInt8]) {
        guard bytes.count == InventoryLimits.idSize, bytes.contains(where: { $0 != 0 }) else {
            return nil
        }
        self.bytes = bytes
    }

    /// Exactly 32 lowercase hex digits.
    public init?(hex: String) {
        let digits = Array(hex.utf8)
        guard digits.count == 32 else { return nil }
        var bytes: [UInt8] = []
        for index in stride(from: 0, to: 32, by: 2) {
            guard let high = Self.value(digits[index]), let low = Self.value(digits[index + 1])
            else { return nil }
            bytes.append(high << 4 | low)
        }
        self.init(bytes: bytes)
    }

    public var hex: String { bytes.map { String(format: "%02x", $0) }.joined() }
    public var description: String { hex }

    public static func < (lhs: InventoryId, rhs: InventoryId) -> Bool {
        lhs.bytes.lexicographicallyPrecedes(rhs.bytes)
    }

    private static func value(_ digit: UInt8) -> UInt8? {
        switch digit {
        case 0x30...0x39: return digit - 0x30
        case 0x61...0x66: return digit - 0x61 + 10
        default: return nil
        }
    }
}

public enum InventoryText {
    /// Firmware's rule: 1...limit code points, no control or line separator
    /// characters, no leading or trailing space.
    public static func isValid(_ text: String, maxLength: Int) -> Bool {
        let scalars = Array(text.unicodeScalars)
        guard !scalars.isEmpty, scalars.count <= maxLength,
              scalars.first != " ", scalars.last != " " else { return false }
        return scalars.allSatisfy { !isForbidden($0) }
    }

    public static func length(_ text: String) -> Int { text.unicodeScalars.count }

    /// Whitespace and control characters become single spaces; ends are trimmed.
    public static func normalize(_ text: String) -> String {
        var result = ""
        var pendingSpace = false
        for scalar in text.unicodeScalars {
            if isForbidden(scalar) || CharacterSet.whitespacesAndNewlines.contains(scalar) {
                pendingSpace = !result.isEmpty
                continue
            }
            if pendingSpace { result.unicodeScalars.append(" ") }
            pendingSpace = false
            result.unicodeScalars.append(scalar)
        }
        return result
    }

    /// Firmware's description rule: 0...limit code points; "\n" is the only
    /// control character; no leading or trailing space or line break.
    public static func isValidDescription(_ text: String, maxLength: Int) -> Bool {
        let scalars = Array(text.unicodeScalars)
        guard scalars.count <= maxLength else { return false }
        guard let first = scalars.first, let last = scalars.last else { return true }
        let edges: Set<Unicode.Scalar> = [" ", "\n"]
        guard !edges.contains(first), !edges.contains(last) else { return false }
        return scalars.allSatisfy { $0 == "\n" || !isForbidden($0) }
    }

    /// Line breaks of any kind become "\n", other control characters and tabs
    /// become spaces, every line is trimmed, and so are the ends.
    public static func normalizeDescription(_ text: String) -> String {
        let unified = text.replacingOccurrences(of: "\r\n", with: "\n")
        var lines: [String] = []
        var line = ""
        for scalar in unified.unicodeScalars {
            if scalar == "\n" || scalar == "\r" || scalar.value == 0x2028 || scalar.value == 0x2029 ||
                scalar.value == 0x85 {
                lines.append(line)
                line = ""
            } else if isForbidden(scalar) || scalar == "\t" {
                line.unicodeScalars.append(" ")
            } else {
                line.unicodeScalars.append(scalar)
            }
        }
        lines.append(line)
        let trimmed = lines.map { $0.trimmingCharacters(in: .whitespaces) }
        return trimmed.joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func isForbidden(_ scalar: Unicode.Scalar) -> Bool {
        scalar.value < 0x20 || (0x7F...0x9F).contains(scalar.value) ||
            scalar.value == 0x2028 || scalar.value == 0x2029
    }
}

/// A container: a short title and a free-text description with line breaks.
public struct InventoryRecord: Equatable {
    public var id: InventoryId
    public var name: String
    public var description: String
    public var revision: UInt32

    public init(id: InventoryId, name: String, description: String, revision: UInt32) {
        self.id = id
        self.name = name
        self.description = description
        self.revision = revision
    }

    public enum Problem: Equatable {
        case name, description, tooLarge, revision
    }

    public var problems: [Problem] {
        var found: [Problem] = []
        if revision == 0 { found.append(.revision) }
        if !InventoryText.isValid(name, maxLength: InventoryLimits.maxNameLength) { found.append(.name) }
        if !InventoryText.isValidDescription(description, maxLength: InventoryLimits.maxDescriptionLength) {
            found.append(.description)
        }
        if found.isEmpty, encodedSize > InventoryLimits.maxRecordBytes { found.append(.tooLarge) }
        return found
    }

    public var encodedSize: Int { unchecked().count }

    /// The canonical JSON firmware writes: schema, id, revision, name,
    /// description; only quote, backslash and line break are escaped.
    public func canonicalJSON() -> [UInt8]? {
        problems.isEmpty ? unchecked() : nil
    }

    private func unchecked() -> [UInt8] {
        func quoted(_ text: String) -> String {
            var result = "\""
            for character in text.unicodeScalars {
                if character == "\n" {
                    result += "\\n"
                    continue
                }
                if character == "\"" || character == "\\" { result.unicodeScalars.append("\\") }
                result.unicodeScalars.append(character)
            }
            return result + "\""
        }
        let json = "{\"schema\":\(InventoryLimits.schemaVersion),\"id\":\(quoted(id.hex))," +
            "\"revision\":\(revision),\"name\":\(quoted(name))," +
            "\"description\":\(quoted(description))}"
        return Array(json.utf8)
    }

    /// A record as firmware sends it; anything outside the schema is refused.
    public static func decode(_ json: [UInt8]) -> InventoryRecord? {
        guard json.count <= InventoryLimits.maxRecordBytes,
              String(bytes: json, encoding: .utf8) != nil,
              let object = try? JSONSerialization.jsonObject(with: Data(json)) as? [String: Any],
              Set(object.keys) == ["schema", "id", "revision", "name", "description"],
              let schema = object["schema"] as? NSNumber, schema.intValue == InventoryLimits.schemaVersion,
              let hex = object["id"] as? String, let id = InventoryId(hex: hex),
              let revision = object["revision"] as? NSNumber,
              revision.int64Value > 0, revision.int64Value <= Int64(UInt32.max),
              let name = object["name"] as? String,
              let description = object["description"] as? String
        else { return nil }
        let record = InventoryRecord(id: id, name: name, description: description,
                                     revision: UInt32(revision.int64Value))
        return record.problems.isEmpty ? record : nil
    }
}

public struct InventoryListEntry: Equatable {
    public var id: InventoryId
    /// False for a stored file that is not a valid record.
    public var valid: Bool
    public var revision: UInt32
    public var name: String

    public init(id: InventoryId, valid: Bool, revision: UInt32, name: String) {
        self.id = id
        self.valid = valid
        self.revision = revision
        self.name = name
    }
}

/// Payload layouts of INVENTORY_LIST, INVENTORY_GET and INVENTORY_PUT.
public enum InventoryWire {
    public struct ListPage: Equatable {
        public var total: Int
        public var next: Int
        public var entries: [InventoryListEntry]

        public init(total: Int, next: Int, entries: [InventoryListEntry]) {
            self.total = total
            self.next = next
            self.entries = entries
        }
    }

    public struct Chunk: Equatable {
        public var revision: UInt32
        public var total: Int
        public var offset: Int
        public var data: [UInt8]

        public init(revision: UInt32, total: Int, offset: Int, data: [UInt8]) {
            self.revision = revision
            self.total = total
            self.offset = offset
            self.data = data
        }
    }

    public static func listRequest(start: Int) -> [UInt8] { u16(start) }

    public static func decodeListRequest(_ payload: [UInt8]) -> Int? {
        payload.count == 2 ? read16(payload, 0) : nil
    }

    public static func encodeListPage(_ page: ListPage) -> [UInt8]? {
        guard page.next <= page.total, page.entries.count <= min(page.total, 255) else { return nil }
        var payload = u16(page.total) + u16(page.next) + [UInt8(page.entries.count)]
        for entry in page.entries {
            let name = Array(entry.name.utf8)
            guard name.count <= InventoryLimits.maxNameBytes, entry.valid == !name.isEmpty,
                  entry.valid == (entry.revision != 0) else { return nil }
            payload += entry.id.bytes + [entry.valid ? 1 : 0] + u32(entry.revision) +
                [UInt8(name.count)] + name
        }
        return payload.count <= CompanionConstants.maxPayloadSize ? payload : nil
    }

    public static func decodeListPage(_ payload: [UInt8]) -> ListPage? {
        guard payload.count >= InventoryLimits.listHeaderSize else { return nil }
        let total = read16(payload, 0)
        let next = read16(payload, 2)
        let count = Int(payload[4])
        var position = InventoryLimits.listHeaderSize
        var entries: [InventoryListEntry] = []
        for _ in 0..<count {
            guard position + InventoryLimits.idSize + 6 <= payload.count,
                  let id = InventoryId(bytes: Array(payload[position..<position + 16])) else { return nil }
            let valid = payload[position + 16]
            let revision = read32(payload, position + 17)
            let length = Int(payload[position + 21])
            position += InventoryLimits.idSize + 6
            guard valid <= 1, length <= InventoryLimits.maxNameBytes, position + length <= payload.count,
                  (valid == 1) == (length > 0), (valid == 1) == (revision != 0),
                  let name = String(bytes: payload[position..<position + length], encoding: .utf8)
            else { return nil }
            position += length
            entries.append(InventoryListEntry(id: id, valid: valid == 1, revision: revision, name: name))
        }
        guard position == payload.count, next <= total, count <= total else { return nil }
        return ListPage(total: total, next: next, entries: entries)
    }

    public static func getRequest(id: InventoryId, offset: Int) -> [UInt8] { id.bytes + u16(offset) }

    public static func decodeGetRequest(_ payload: [UInt8]) -> (InventoryId, Int)? {
        guard payload.count == InventoryLimits.idSize + 2,
              let id = InventoryId(bytes: Array(payload[0..<16])) else { return nil }
        let offset = read16(payload, 16)
        return offset < InventoryLimits.maxRecordBytes ? (id, offset) : nil
    }

    public static func encodeGetChunk(_ chunk: Chunk) -> [UInt8]? {
        guard chunk.revision != 0,
              boundsValid(chunk.total, chunk.offset, chunk.data.count, InventoryLimits.getChunkSize)
        else { return nil }
        return u32(chunk.revision) + u16(chunk.total) + u16(chunk.offset) + chunk.data
    }

    public static func decodeGetChunk(_ payload: [UInt8]) -> Chunk? {
        guard payload.count > InventoryLimits.getHeaderSize else { return nil }
        let chunk = Chunk(revision: read32(payload, 0), total: read16(payload, 4),
                          offset: read16(payload, 6),
                          data: Array(payload[InventoryLimits.getHeaderSize...]))
        guard chunk.revision != 0,
              boundsValid(chunk.total, chunk.offset, chunk.data.count, InventoryLimits.getChunkSize)
        else { return nil }
        return chunk
    }

    public static func putRequest(id: InventoryId, expectedRevision: UInt32, total: Int,
                                  offset: Int, data: [UInt8]) -> [UInt8]? {
        guard expectedRevision != 0,
              boundsValid(total, offset, data.count, InventoryLimits.putChunkSize) else { return nil }
        return id.bytes + u32(expectedRevision) + u16(total) + u16(offset) + data
    }

    public static func decodePutRequest(_ payload: [UInt8]) -> (InventoryId, Chunk)? {
        guard payload.count > InventoryLimits.putHeaderSize,
              let id = InventoryId(bytes: Array(payload[0..<16])) else { return nil }
        let chunk = Chunk(revision: read32(payload, 16), total: read16(payload, 20),
                          offset: read16(payload, 22),
                          data: Array(payload[InventoryLimits.putHeaderSize...]))
        guard chunk.revision != 0,
              boundsValid(chunk.total, chunk.offset, chunk.data.count, InventoryLimits.putChunkSize)
        else { return nil }
        return (id, chunk)
    }

    /// OK answer to a PUT chunk: bytes received and the committed revision
    /// (0 until the last chunk is committed).
    public static func decodePutAck(_ payload: [UInt8]) -> (received: Int, committed: UInt32)? {
        payload.count == 6 ? (read16(payload, 0), read32(payload, 2)) : nil
    }

    public static func putAck(received: Int, committed: UInt32) -> [UInt8] {
        u16(received) + u32(committed)
    }

    public static func deleteRequest(id: InventoryId, expectedRevision: UInt32) -> [UInt8] {
        id.bytes + u32(expectedRevision)
    }

    public static func decodeDeleteRequest(_ payload: [UInt8]) -> (InventoryId, UInt32)? {
        guard payload.count == InventoryLimits.idSize + 4,
              let id = InventoryId(bytes: Array(payload[0..<16])) else { return nil }
        return (id, read32(payload, 16))
    }

    public static func conflictRevision(_ payload: [UInt8]) -> UInt32? {
        guard payload.count == 4 else { return nil }
        let revision = read32(payload, 0)
        return revision != 0 ? revision : nil
    }

    /// Payload rules the envelope codec applies to inventory operations.
    static func payloadValid(_ message: CompanionEnvelope) -> Bool {
        if message.kind == .request {
            switch message.operation {
            case .inventoryList: return decodeListRequest(message.payload) != nil
            case .inventoryGet: return decodeGetRequest(message.payload) != nil
            case .inventoryDelete: return decodeDeleteRequest(message.payload) != nil
            default: return decodePutRequest(message.payload) != nil
            }
        }
        if message.status == .conflict {
            return (message.operation == .inventoryPut || message.operation == .inventoryDelete) &&
                conflictRevision(message.payload) != nil
        }
        if message.status != .ok { return message.payload.isEmpty }
        switch message.operation {
        case .inventoryList: return decodeListPage(message.payload) != nil
        case .inventoryGet: return decodeGetChunk(message.payload) != nil
        case .inventoryDelete: return message.payload.isEmpty
        default: return decodePutAck(message.payload) != nil
        }
    }

    private static func boundsValid(_ total: Int, _ offset: Int, _ size: Int, _ maxChunk: Int) -> Bool {
        total > 0 && total <= InventoryLimits.maxRecordBytes && size > 0 && size <= maxChunk &&
            offset >= 0 && offset < total && offset + size <= total
    }

    private static func u16(_ value: Int) -> [UInt8] {
        [UInt8(value & 0xFF), UInt8((value >> 8) & 0xFF)]
    }

    private static func u32(_ value: UInt32) -> [UInt8] {
        (0..<4).map { UInt8((value >> (8 * UInt32($0))) & 0xFF) }
    }

    private static func read16(_ bytes: [UInt8], _ offset: Int) -> Int {
        Int(bytes[offset]) | Int(bytes[offset + 1]) << 8
    }

    private static func read32(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        (0..<4).reduce(UInt32(0)) { $0 | UInt32(bytes[offset + $1]) << (8 * UInt32($1)) }
    }
}
