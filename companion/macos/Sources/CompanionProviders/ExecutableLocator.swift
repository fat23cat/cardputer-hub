import Darwin
import Foundation

/// Finds a command-line tool the GUI app's PATH may not contain.
final class ExecutableLocator {
    private let name: String
    private let environment: () -> [String: String]
    private let home: String
    private let isExecutable: (String) -> Bool
    private let loginShellPath: () -> String?
    private var cached: String?

    init(name: String,
         environment: @escaping () -> [String: String] = { ProcessInfo.processInfo.environment },
         home: String = NSHomeDirectory(),
         isExecutable: @escaping (String) -> Bool = {
             FileManager.default.isExecutableFile(atPath: $0)
         },
         loginShellPath: (() -> String?)? = nil) {
        self.name = name
        self.environment = environment
        self.home = home
        self.isExecutable = isExecutable
        self.loginShellPath = loginShellPath ?? { ExecutableLocator.findThroughLoginShell(name) }
    }

    func find() -> String? {
        if let cached, isExecutable(cached) { return cached }
        cached = nil
        let paths = (environment()["PATH"] ?? "").split(separator: ":").map(String.init)
        let candidates = paths.map { $0 + "/" + name } + [
            home + "/.local/bin/" + name, "/opt/homebrew/bin/" + name, "/usr/local/bin/" + name,
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

    private static func findThroughLoginShell(_ name: String) -> String? {
        let configured = getpwuid(getuid()).map { String(cString: $0.pointee.pw_shell) }
        let shell = configured ?? ProcessInfo.processInfo.environment["SHELL"] ?? "/bin/zsh"
        guard FileManager.default.isExecutableFile(atPath: shell) else { return nil }
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: shell)
        process.arguments = ["-l", "-c", "command -v " + name]
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
