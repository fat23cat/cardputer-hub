import Foundation

/// This Companion's build id, "YYYY-MM-DD <short commit>" with "+" for a dirty
/// tree. The packaging script writes it into Info.plist; `swift run` has none.
public enum BuildIdentity {
    public static let infoKey = "CardputerBuildId"

    public static func buildId(info: [String: Any]?) -> String {
        guard let id = info?[infoKey] as? String, !id.isEmpty,
              id.utf8.count <= CompanionConstants.maxBuildIdSize,
              id.utf8.allSatisfy({ (0x20...0x7e).contains($0) }) else { return "dev" }
        return id
    }

    public static var current: String { buildId(info: Bundle.main.infoDictionary) }
}

/// Outcome of the latest HELLO.
public enum CompanionCompatibility: Equatable {
    case unknown
    /// Same protocol fingerprint; the Cardputer's build id.
    case matched(firmwareBuildId: String)
    /// The Cardputer was built from another protocol definition.
    case mismatch(firmwareBuildId: String)
    /// No HELLO_ACK: firmware from before plan 043 cannot read the new HELLO.
    case noAnswer
}

/// Which side to update after a mismatch, judged by build dates (the firmware
/// makes the same call for its own screen).
public enum CompanionMismatchAdvice: Equatable {
    case updateCompanion, updateFirmware, rebuildBoth

    public init(companionBuildId: String, firmwareBuildId: String) {
        guard let companion = Self.date(companionBuildId), let firmware = Self.date(firmwareBuildId)
        else { self = .rebuildBoth; return }
        self = companion < firmware ? .updateCompanion
            : companion > firmware ? .updateFirmware : .rebuildBoth
    }

    private static func date(_ buildId: String) -> String? {
        let date = String(buildId.prefix(10))
        let pattern = #"^\d{4}-\d{2}-\d{2}$"#
        return date.range(of: pattern, options: .regularExpression) != nil ? date : nil
    }
}
