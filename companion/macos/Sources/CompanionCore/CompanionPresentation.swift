import Foundation

public enum CompanionConnectionPresentationState: Equatable {
    case disconnected
    case connecting
    case connected
    case error
}

public enum CompanionPresentation {
    public static func connection(session: UInt16, phase: CompanionAttachPhase,
                                  error: Bool, waitingToConnect: Bool = false)
        -> CompanionConnectionPresentationState {
        if session != 0 { return .connected }
        if error { return .error }
        switch phase {
        case .idle: return .disconnected
        case .cancelling: return waitingToConnect ? .connecting : .disconnected
        case .connecting, .discoveringServices, .discoveringCharacteristics,
             .subscribing, .handshaking: return .connecting
        }
    }

    public static func lastSeen(_ date: Date?, now: Date) -> String? {
        guard let date else { return nil }
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 2 { return "Just now" }
        if seconds < 60 { return "\(seconds)s ago" }
        if seconds < 3_600 { return "\(seconds / 60)m ago" }
        return "\(seconds / 3_600)h ago"
    }

    public static func statusMetadata(connection: CompanionConnectionPresentationState,
                                      protocolVersion: UInt8?, lastMessageAt: Date?, now: Date) -> String? {
        guard connection == .connected, let protocolVersion else { return nil }
        guard let seen = lastSeen(lastMessageAt, now: now) else { return "Protocol v\(protocolVersion)" }
        return "Protocol v\(protocolVersion) · \(seen == "Just now" ? "just now" : seen)"
    }

    public static func sessionDuration(_ date: Date?, now: Date) -> String? {
        guard let date else { return nil }
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 3_600 { return "\(seconds / 60)m \(seconds % 60)s" }
        return "\(seconds / 3_600)h \((seconds % 3_600) / 60)m"
    }
}

public struct CompanionCapabilitySummary: Equatable {
    public let appControl: Bool
    public let appEvents: Bool
    public let systemMetrics: Bool

    public init(_ capabilities: [CompanionCapability]) {
        appControl = capabilities.contains(.appActive) && capabilities.contains(.appActivate)
        appEvents = capabilities.contains(.appActiveEvents)
        systemMetrics = capabilities.contains(.systemMetrics)
    }
}
