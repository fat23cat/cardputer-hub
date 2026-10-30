import Foundation

/// Asks Claude Code to renew its OAuth token. Companion never refreshes or
/// writes the credential itself; the CLI renews it as on its own next use.
protocol ClaudeTokenRenewing: AnyObject {
    /// Calls `done` with true when the CLI finished successfully.
    func renew(_ done: @escaping (Bool) -> Void)
}

final class ProcessClaudeTokenRenewer: ClaudeTokenRenewing {
    // One tiny request is enough for the CLI to renew an expired token. An
    // empty directory and project-only settings keep CLAUDE.md, user hooks
    // (Companion's agent-status hooks among them), MCP servers, tools and a
    // saved session out of it.
    static let arguments = [
        "-p", "ok", "--model", "haiku", "--tools", "",
        "--no-session-persistence", "--strict-mcp-config",
        "--setting-sources", "project",
    ]

    private let locator: ExecutableLocator
    private let timeout: TimeInterval
    // The locator caches without locking and may wait on a login shell.
    private let queue = DispatchQueue(label: "org.cardputer.companion.claude-renewal")

    init(locator: ExecutableLocator = ExecutableLocator(name: "claude"),
         timeout: TimeInterval = 60) {
        self.locator = locator
        self.timeout = timeout
    }

    func renew(_ done: @escaping (Bool) -> Void) {
        queue.async { [self] in
            guard let path = locator.find() else { done(false); return }
            let directory = FileManager.default.temporaryDirectory
                .appendingPathComponent("claude-token-renewal", isDirectory: true)
            try? FileManager.default.createDirectory(at: directory,
                                                     withIntermediateDirectories: true)
            let process = Process()
            process.executableURL = URL(fileURLWithPath: path)
            process.arguments = Self.arguments
            process.currentDirectoryURL = directory
            process.standardInput = FileHandle.nullDevice
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            process.terminationHandler = { done($0.terminationStatus == 0) }
            do { try process.run() } catch { done(false); return }
            queue.asyncAfter(deadline: .now() + timeout) {
                if process.isRunning { process.terminate() }
            }
        }
    }
}
