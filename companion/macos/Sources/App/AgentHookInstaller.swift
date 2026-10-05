import CompanionAgentHooks
import Foundation

/// Installs and removes Companion's own hook entries in the user-level
/// configuration of each application, on explicit user action only. Every
/// write keeps a backup and replaces the file atomically through symlinks.
final class AgentHookInstaller {
    enum Failure: LocalizedError {
        case notPlainJSON(String)
        case unexpectedShape(String)
        case missingHelper

        var errorDescription: String? {
            switch self {
            case .notPlainJSON(let path):
                return "\(path) is not plain JSON. Edit it by hand or remove comments first."
            case .unexpectedShape(let detail):
                return "Unexpected hook configuration: \(detail)."
            case .missingHelper:
                return "The hook helper is missing from Cardputer Companion.app."
            }
        }
    }

    private let fileManager = FileManager.default

    func configurationURL(_ application: AgentApplication) -> URL {
        let home = fileManager.homeDirectoryForCurrentUser
        switch application {
        case .claude:
            return home.appendingPathComponent(".claude/settings.json")
        case .codex:
            let codexHome = ProcessInfo.processInfo.environment["CODEX_HOME"]
                .map { URL(fileURLWithPath: $0, isDirectory: true) }
                ?? home.appendingPathComponent(".codex", isDirectory: true)
            return codexHome.appendingPathComponent("hooks.json")
        case .cursor:
            return home.appendingPathComponent(".cursor/hooks.json")
        }
    }

    /// Nil when the file cannot be read as plain JSON.
    func isInstalled(_ application: AgentApplication) -> Bool? {
        guard let config = try? read(configurationURL(application)) else { return nil }
        return AgentHookConfiguration.isInstalled(application, in: config)
    }

    /// Copies the bundled helper to its stable location. Called at launch so an
    /// updated Companion also updates the helper that installed hooks run.
    func refreshHelper() throws {
        guard let bundled = Bundle.main.executableURL?.deletingLastPathComponent()
            .appendingPathComponent(AgentHookConfiguration.helperName),
            fileManager.isExecutableFile(atPath: bundled.path) else { throw Failure.missingHelper }
        try fileManager.createDirectory(at: AgentHookPaths.supportDirectory,
                                        withIntermediateDirectories: true,
                                        attributes: [.posixPermissions: 0o700])
        let target = URL(fileURLWithPath: AgentHookPaths.helperPath)
        let staging = target.appendingPathExtension("new")
        try? fileManager.removeItem(at: staging)
        try fileManager.copyItem(at: bundled, to: staging)
        if fileManager.fileExists(atPath: target.path) {
            _ = try fileManager.replaceItemAt(target, withItemAt: staging)
        } else {
            try fileManager.moveItem(at: staging, to: target)
        }
    }

    func install(_ application: AgentApplication) throws {
        try refreshHelper()
        let command = AgentHookConfiguration.command(helperPath: AgentHookPaths.helperPath,
                                                     application: application)
        try update(application) {
            try AgentHookConfiguration.installing(application, command: command, into: $0)
        }
    }

    func remove(_ application: AgentApplication) throws {
        try update(application) { try AgentHookConfiguration.removing(application, from: $0) }
    }

    private func update(_ application: AgentApplication,
                        _ change: ([String: Any]) throws -> [String: Any]) throws {
        let url = configurationURL(application).resolvingSymlinksInPath()
        let current = try read(url)
        let next: [String: Any]
        do {
            next = try change(current)
        } catch AgentHookConfiguration.Failure.unexpectedShape(let detail) {
            throw Failure.unexpectedShape(detail)
        }
        try fileManager.createDirectory(at: url.deletingLastPathComponent(),
                                        withIntermediateDirectories: true)
        if fileManager.fileExists(atPath: url.path) {
            let backup = url.appendingPathExtension("cardputer-backup")
            try? fileManager.removeItem(at: backup)
            try fileManager.copyItem(at: url, to: backup)
        }
        let data = try JSONSerialization.data(withJSONObject: next,
                                              options: [.prettyPrinted, .withoutEscapingSlashes])
        try (data + Data("\n".utf8)).write(to: url, options: .atomic)
    }

    private func read(_ url: URL) throws -> [String: Any] {
        guard let data = try? Data(contentsOf: url.resolvingSymlinksInPath()) else { return [:] }
        if data.allSatisfy({ $0 == 0x20 || $0 == 0x0A || $0 == 0x0D || $0 == 0x09 }) { return [:] }
        guard let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
            throw Failure.notPlainJSON(url.path)
        }
        return object
    }
}
