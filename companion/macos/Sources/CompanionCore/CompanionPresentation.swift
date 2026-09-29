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
                                      firmwareBuildId: String?, lastMessageAt: Date?, now: Date) -> String? {
        guard connection == .connected, let firmwareBuildId else { return nil }
        let build = "Cardputer \(firmwareBuildId)"
        guard let seen = lastSeen(lastMessageAt, now: now) else { return build }
        return "\(build) · \(seen == "Just now" ? "just now" : seen)"
    }

    /// One line for the menu when the Cardputer and this Companion cannot talk.
    public static func compatibilityNotice(_ compatibility: CompanionCompatibility,
                                           companionBuildId: String) -> String? {
        switch compatibility {
        case .unknown, .matched:
            return nil
        case .noAnswer:
            return "No answer — Cardputer firmware may be older than this Companion"
        case .mismatch(let firmware):
            switch CompanionMismatchAdvice(companionBuildId: companionBuildId, firmwareBuildId: firmware) {
            case .updateCompanion: return "Update this Companion — Cardputer runs \(firmware)"
            case .updateFirmware: return "Update Cardputer firmware (\(firmware))"
            case .rebuildBoth: return "Rebuild firmware and Companion from one commit"
            }
        }
    }

    public static func sessionDuration(_ date: Date?, now: Date) -> String? {
        guard let date else { return nil }
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 3_600 { return "\(seconds / 60)m \(seconds % 60)s" }
        return "\(seconds / 3_600)h \((seconds % 3_600) / 60)m"
    }
}

