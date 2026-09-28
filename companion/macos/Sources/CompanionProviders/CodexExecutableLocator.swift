import Darwin
import Foundation

final class CodexExecutableLocator {
    private let environment: () -> [String: String]
    private let home: String
    private let isExecutable: (String) -> Bool
    private let loginShellPath: () -> String?
    private var cached: String?

    init(environment: @escaping () -> [String: String] = { ProcessInfo.processInfo.environment },
         home: String = NSHomeDirectory(),
         isExecutable: @escaping (String) -> Bool = {
             FileManager.default.isExecutableFile(atPath: $0)
         },
         loginShellPath: @escaping () -> String? = {
             CodexExecutableLocator.findThroughLoginShell()
         }) {
        self.environment = environment
        self.home = home
        self.isExecutable = isExecutable
        self.loginShellPath = loginShellPath
    }

    func find() -> String? {
        if let cached, isExecutable(cached) { return cached }
        cached = nil
        let paths = (environment()["PATH"] ?? "").split(separator: ":").map(String.init)
        let candidates = paths.map { $0 + "/codex" } + [
            home + "/.local/bin/codex", "/opt/homebrew/bin/codex", "/usr/local/bin/codex",
        ]
        if let found = candidates.first(where: isExecutable) {
            cached = found
            return found
        }
        if let found = loginShellPath(), found.hasPrefix("/"), isExecutable(found) {
            cached = found
            return found
        }
        return nil
    }

    private static func findThroughLoginShell() -> String? {
        let configured = getpwuid(getuid()).map { String(cString: $0.pointee.pw_shell) }
        let shell = configured ?? ProcessInfo.processInfo.environment["SHELL"] ?? "/bin/zsh"
        guard FileManager.default.isExecutableFile(atPath: shell) else { return nil }
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: shell)
        process.arguments = ["-l", "-c", "command -v codex"]
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        let finished = DispatchSemaphore(value: 0)
        process.terminationHandler = { _ in finished.signal() }
        do { try process.run() } catch { return nil }
        guard finished.wait(timeout: .now() + 2) == .success else {
            if process.isRunning { process.terminate() }
            return nil
        }
        guard process.terminationStatus == 0,
              let text = String(data: output.fileHandleForReading.readDataToEndOfFile(),
                                encoding: .utf8) else { return nil }
        return text.split(whereSeparator: \.isNewline).map(String.init)
            .last(where: { $0.hasPrefix("/") })
    }
}
