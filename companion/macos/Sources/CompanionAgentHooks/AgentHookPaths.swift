import Foundation

/// Shared locations of the hook helper and Companion's private socket.
public enum AgentHookPaths {
    public static var supportDirectory: URL {
        home.appendingPathComponent("Library/Application Support/Cardputer Companion",
                                    isDirectory: true)
    }

    /// `HOME` when set, as hook runners and launchd provide it; otherwise the
    /// account's home directory.
    static var home: URL {
        if let path = ProcessInfo.processInfo.environment["HOME"], path.hasPrefix("/") {
            return URL(fileURLWithPath: path, isDirectory: true)
        }
        return FileManager.default.homeDirectoryForCurrentUser
    }

    /// Owned by the current user, mode 0600, in a 0700 directory.
    public static var socketPath: String {
        supportDirectory.appendingPathComponent("agent-hooks.sock").path
    }

    /// A stable copy of the helper that hook commands run, so moving or updating
    /// the app does not break installed hooks.
    public static var helperPath: String {
        supportDirectory.appendingPathComponent(AgentHookConfiguration.helperName).path
    }
}
