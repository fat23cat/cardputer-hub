import Foundation

public enum CompanionKind: UInt8 {
    case hello = 1
    case helloAck = 2
    case request = 3
    case response = 4
    case event = 5
}

public enum CompanionOperation: UInt8 {
    case none = 0
    case ping = 1
    case capabilities = 2
    case appActive = 3
    case appActivate = 4
    case appActiveChanged = 5
    case systemMetrics = 6
}

public enum CompanionStatus: UInt8 {
    case ok = 0
    case notAvailable = 1
    case notFound = 2
    case unsupported = 3
    case malformed = 4
}

public enum CompanionCapability: UInt8 {
    case appActive = 1
    case appActivate = 2
    case appActiveEvents = 3
    case systemMetrics = 4
}

public struct CompanionConstants {
    public static let protocolVersion: UInt8 = 1
    public static let latestProtocolVersion: UInt8 = 2
    public static let maxMessageSize = 256
    public static let envelopeSize = 8
    public static let maxPayloadSize = maxMessageSize - envelopeSize
    public static let maxBundleIdSize = 128
    public static let pingTokenSize = 4
    public static let serviceUUID = "07B23AB1-3938-418A-8E16-0AEC1CAA517F"
    public static let hostToDeviceUUID = "792B8431-054D-4758-B198-3EE728EA6FE1"
    public static let deviceToHostUUID = "BE3869D8-8F00-4D4B-A4CA-954A2D5B3EE1"
    public static let hidServiceUUID = "1812"
}

public struct CompanionEnvelope: Equatable {
    public var version: UInt8 = CompanionConstants.protocolVersion
    public var kind: CompanionKind = .request
    public var session: UInt16 = 0
    public var requestId: UInt8 = 0
    public var operation: CompanionOperation = .none
    public var status: CompanionStatus = .ok
    public var payload: [UInt8] = []

    public init() {}
}

public enum CompanionCodec {
    public static func encode(_ message: CompanionEnvelope) -> [UInt8]? {
        guard (1...CompanionConstants.latestProtocolVersion).contains(message.version),
              message.payload.count <= CompanionConstants.maxPayloadSize,
              isValid(message)
        else { return nil }
        var bytes: [UInt8] = [
            message.version,
            message.kind.rawValue,
            UInt8(message.session & 0xFF),
            UInt8((message.session >> 8) & 0xFF),
            message.requestId,
            message.operation.rawValue,
            message.status.rawValue,
            UInt8(message.payload.count),
        ]
        bytes.append(contentsOf: message.payload)
        return bytes
    }

    public static func decode(_ data: [UInt8]) -> CompanionEnvelope? {
        guard data.count >= CompanionConstants.envelopeSize,
              data.count <= CompanionConstants.maxMessageSize
        else { return nil }
        let payloadSize = Int(data[7])
        guard data.count == CompanionConstants.envelopeSize + payloadSize,
              (1...CompanionConstants.latestProtocolVersion).contains(data[0]),
              let kind = CompanionKind(rawValue: data[1]),
              let operation = CompanionOperation(rawValue: data[5]),
              let status = CompanionStatus(rawValue: data[6])
        else { return nil }
        var message = CompanionEnvelope()
        message.version = data[0]
        message.kind = kind
        message.session = UInt16(data[2]) | (UInt16(data[3]) << 8)
        message.requestId = data[4]
        message.operation = operation
        message.status = status
        if payloadSize > 0 {
            message.payload = Array(data[CompanionConstants.envelopeSize..<(CompanionConstants.envelopeSize + payloadSize)])
        }
        return isValid(message) ? message : nil
    }

    public static func isValid(_ message: CompanionEnvelope) -> Bool {
        operationAllowed(message.kind, message.operation) &&
            headerSemanticsValid(message) &&
            payloadValid(message)
    }

    public static func hello(versions: [UInt8] = [CompanionConstants.protocolVersion]) -> CompanionEnvelope {
        var message = CompanionEnvelope()
        message.kind = .hello
        message.payload = [UInt8(versions.count)] + versions
        return message
    }

    public static func pingResponse(session: UInt16, requestId: UInt8, token: [UInt8]) -> CompanionEnvelope {
        var message = CompanionEnvelope()
        message.kind = .response
        message.session = session
        message.requestId = requestId
        message.operation = .ping
        message.payload = token
        return message
    }

    public static func capabilitiesResponse(session: UInt16, requestId: UInt8,
                                            version: UInt8 = CompanionConstants.protocolVersion) -> CompanionEnvelope {
        var message = CompanionEnvelope()
        message.version = version
        message.kind = .response
        message.session = session
        message.requestId = requestId
        message.operation = .capabilities
        message.payload = [3, CompanionCapability.appActive.rawValue,
                           CompanionCapability.appActivate.rawValue,
                           CompanionCapability.appActiveEvents.rawValue]
        if version >= CompanionConstants.latestProtocolVersion {
            message.payload[0] = 4
            message.payload.append(CompanionCapability.systemMetrics.rawValue)
        }
        return message
    }

    public static func bundlePayload(_ identifier: String) -> [UInt8]? {
        let utf8 = Array(identifier.utf8)
        guard !utf8.isEmpty, utf8.count <= CompanionConstants.maxBundleIdSize else { return nil }
        return [UInt8(utf8.count)] + utf8
    }

    public static func readBundle(_ payload: [UInt8]) -> String? {
        guard payload.count >= 2,
              payload[0] > 0,
              payload[0] <= CompanionConstants.maxBundleIdSize,
              payload.count == Int(payload[0]) + 1
        else { return nil }
        return String(bytes: payload.dropFirst(), encoding: .utf8)
    }

    private static func operationAllowed(_ kind: CompanionKind, _ operation: CompanionOperation) -> Bool {
        switch kind {
        case .hello, .helloAck:
            return operation == .none
        case .request, .response:
            return operation == .ping || operation == .capabilities ||
                operation == .appActive || operation == .appActivate || operation == .systemMetrics
        case .event:
            return operation == .appActiveChanged
        }
    }

    private static func headerSemanticsValid(_ message: CompanionEnvelope) -> Bool {
        switch message.kind {
        case .hello:
            return message.session == 0 && message.requestId == 0 &&
                message.operation == .none && message.status == .ok
        case .helloAck:
            return message.session != 0 && message.requestId == 0 &&
                message.operation == .none && message.status == .ok
        case .request:
            return message.session != 0 && message.requestId != 0 && message.status == .ok
        case .response:
            return message.session != 0 && message.requestId != 0
        case .event:
            return message.session != 0 && message.requestId == 0 && message.status == .ok
        }
    }

    private static func payloadValid(_ message: CompanionEnvelope) -> Bool {
        switch message.operation {
        case .none:
            if message.kind == .hello {
                return message.payload.count >= 2 &&
                    message.payload[0] > 0 &&
                    message.payload[0] <= 4 &&
                    message.payload.count == Int(message.payload[0]) + 1
            }
            return message.kind == .helloAck && message.version == 1 &&
                message.payload.count == 1 &&
                (1...CompanionConstants.latestProtocolVersion).contains(message.payload[0])
        case .ping:
            return message.payload.count == CompanionConstants.pingTokenSize
        case .capabilities:
            if message.kind == .request { return message.payload.isEmpty }
            if message.status != .ok { return message.payload.isEmpty }
            guard let count = message.payload.first, count > 0, count <= 8,
                  message.payload.count == Int(count) + 1
            else { return false }
            return message.payload.dropFirst().allSatisfy {
                guard let capability = CompanionCapability(rawValue: $0) else { return false }
                return capability != .systemMetrics || message.version >= 2
            }
        case .appActive:
            if message.kind == .request { return message.payload.isEmpty }
            if message.status == .ok { return readBundle(message.payload) != nil }
            return message.payload.isEmpty
        case .appActivate:
            if message.kind == .request { return readBundle(message.payload) != nil }
            return message.payload.isEmpty
        case .appActiveChanged:
            return message.payload.isEmpty || readBundle(message.payload) != nil
        case .systemMetrics:
            guard message.version >= 2 else { return false }
            if message.kind == .request || message.status != .ok { return message.payload.isEmpty }
            return SystemMetricsSample.decode(message.payload) != nil
        }
    }
}

public enum CompanionFramer {
    public static let headerSize = 3
    public static let defaultPayload = 17
    public static let maxChunks = 16

    public static func encode(_ message: [UInt8], messageId: UInt8, maxPayload: Int = defaultPayload) -> [[UInt8]]? {
        guard !message.isEmpty, message.count <= CompanionConstants.maxMessageSize, maxPayload > 0 else { return nil }
        let count = (message.count + maxPayload - 1) / maxPayload
        guard count > 0, count <= maxChunks else { return nil }
        var chunks: [[UInt8]] = []
        var offset = 0
        for index in 0..<count {
            let remaining = message.count - offset
            let payload = min(remaining, maxPayload)
            var chunk: [UInt8] = [messageId, UInt8(index), UInt8(count)]
            chunk.append(contentsOf: message[offset..<(offset + payload)])
            chunks.append(chunk)
            offset += payload
        }
        return chunks
    }

    public static func decode(_ chunks: [[UInt8]]) -> [UInt8]? {
        guard let first = chunks.first, first.count >= headerSize else { return nil }
        let messageId = first[0]
        let count = Int(first[2])
        guard count > 0, count <= maxChunks, chunks.count == count else { return nil }
        var parts = Array(repeating: [UInt8](), count: count)
        var seen = Set<Int>()
        for chunk in chunks {
            guard chunk.count > headerSize, chunk[0] == messageId, chunk[2] == UInt8(count) else { return nil }
            let index = Int(chunk[1])
            guard index < count, !seen.contains(index) else { return nil }
            seen.insert(index)
            parts[index] = Array(chunk[headerSize...])
        }
        guard seen.count == count else { return nil }
        return parts.flatMap { $0 }
    }
}

public final class CompanionReassembler {
    public static let timeout: TimeInterval = 2

    private var parts: [Int: [UInt8]] = [:]
    private var messageId: UInt8?
    private var count: Int?
    private var elapsed: TimeInterval = 0

    public init() {}

    public func reset() {
        parts.removeAll()
        messageId = nil
        count = nil
        elapsed = 0
    }

    public func update(_ delta: TimeInterval) {
        guard messageId != nil else { return }
        elapsed += max(0, delta)
        if elapsed >= Self.timeout {
            reset()
        }
    }

    public func ingest(_ chunk: [UInt8]) -> [UInt8]? {
        guard chunk.count > CompanionFramer.headerSize else { return nil }
        let id = chunk[0]
        let index = Int(chunk[1])
        let chunkCount = Int(chunk[2])
        guard id != 0, chunkCount > 0, chunkCount <= CompanionFramer.maxChunks, index < chunkCount else {
            reset()
            return nil
        }
        if let messageId, messageId != id {
            reset()
        }
        if self.messageId == nil {
            self.messageId = id
            self.count = chunkCount
            elapsed = 0
        } else if count != chunkCount {
            reset()
            return nil
        }
        if parts[index] != nil {
            reset()
            return nil
        }
        parts[index] = Array(chunk[CompanionFramer.headerSize...])
        guard parts.count == chunkCount else { return nil }
        var assembled: [UInt8] = []
        for partIndex in 0..<chunkCount {
            guard let part = parts[partIndex] else { return nil }
            assembled.append(contentsOf: part)
        }
        reset()
        return assembled
    }
}
