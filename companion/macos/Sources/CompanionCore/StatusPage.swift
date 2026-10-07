import Foundation

/// SERVICE_STATUS levels in severity order; 0 (unknown) is never sent.
public enum ServiceStatusLevel: UInt8, Equatable, Sendable {
    case operational = 1
    case maintenance = 2
    case minor = 3
    case major = 4
    case critical = 5

    /// Statuspage `status.indicator`.
    public init?(indicator: String) {
        switch indicator {
        case "none": self = .operational
        case "minor": self = .minor
        case "major": self = .major
        case "critical": self = .critical
        case "maintenance": self = .maintenance
        default: return nil
        }
    }
}

/// One status page as the Cardputer shows it: a level and a short description.
public struct StatusPageReport: Equatable, Sendable {
    public static let maxDescriptionBytes = 48

    public let level: ServiceStatusLevel
    public let description: String

    /// The description is cut to 48 bytes at a character boundary.
    public init(level: ServiceStatusLevel, description: String) {
        self.level = level
        var cut = ""
        for character in description {
            guard cut.utf8.count + String(character).utf8.count <= Self.maxDescriptionBytes else { break }
            cut.append(character)
        }
        self.description = cut
    }

    /// Reads an Atlassian Statuspage `/api/v2/status.json` body.
    public static func parse(_ body: Data) -> StatusPageReport? {
        guard let root = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
              let status = root["status"] as? [String: Any],
              let indicator = status["indicator"] as? String,
              let level = ServiceStatusLevel(indicator: indicator) else { return nil }
        return StatusPageReport(level: level, description: status["description"] as? String ?? "")
    }

    /// OK response: level (1), description length (1), UTF-8 description.
    public func encode() -> [UInt8] {
        let text = Array(description.utf8)
        return [level.rawValue, UInt8(text.count)] + text
    }

    public static func decode(_ payload: [UInt8]) -> StatusPageReport? {
        guard payload.count >= 2, let level = ServiceStatusLevel(rawValue: payload[0]),
              payload[1] <= maxDescriptionBytes, payload.count == 2 + Int(payload[1]),
              let text = String(bytes: payload[2...], encoding: .utf8) else { return nil }
        return StatusPageReport(level: level, description: text)
    }
}

public enum ServiceStatusWire {
    public static let maxURLBytes = 200

    /// Request: URL length (1), then an https URL of printable ASCII.
    public static func requestURL(_ payload: [UInt8]) -> URL? {
        guard payload.count >= 2, payload.count == 1 + Int(payload[0]),
              Int(payload[0]) <= maxURLBytes,
              payload.dropFirst().allSatisfy({ $0 > 0x20 && $0 < 0x7F }),
              let text = String(bytes: payload.dropFirst(), encoding: .ascii),
              text.hasPrefix("https://"), text.count > "https://".count,
              let url = URL(string: text), url.scheme == "https", url.host != nil
        else { return nil }
        return url
    }

    public static func request(_ url: String) -> [UInt8]? {
        let bytes = Array(url.utf8)
        guard bytes.count <= maxURLBytes else { return nil }
        let payload = [UInt8(bytes.count)] + bytes
        return requestURL(payload) != nil ? payload : nil
    }
}

/// Fetches one status page. The completion runs on the main queue, with nil when
/// the page could not be fetched or read.
public protocol StatusPageFetching: AnyObject {
    func fetch(_ url: URL, completion: @escaping (StatusPageReport?) -> Void)
}
