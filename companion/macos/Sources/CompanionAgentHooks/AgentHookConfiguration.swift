import Foundation

/// Merges and removes Companion's own entries in a user-level hook
/// configuration. Entries are recognised by the helper name in their command;
/// every other hook is left as it is.
public enum AgentHookConfiguration {
    public static let helperName = "CardputerAgentHook"

    public enum Failure: Error, Equatable {
        /// The file is valid JSON but not in the documented shape.
        case unexpectedShape(String)
    }

    /// The events each application needs for the plan 047 mapping.
    public static func events(for application: AgentApplication) -> [String] {
        switch application {
        case .claude:
            return ["UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure",
                    "PermissionRequest", "Notification", "Stop", "StopFailure", "SessionEnd"]
        case .codex:
            return ["UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest", "Stop",
                    "Interrupt", "SessionEnd"]
        case .cursor:
            return ["beforeSubmitPrompt", "postToolUse", "postToolUseFailure", "stop", "sessionEnd"]
        }
    }

    public static func command(helperPath: String, application: AgentApplication) -> String {
        "\"\(helperPath.replacingOccurrences(of: "\"", with: "\\\""))\" --app \(application.name)"
    }

    public static func isInstalled(_ application: AgentApplication, in config: [String: Any]) -> Bool {
        guard let hooks = config["hooks"] as? [String: Any] else { return false }
        return events(for: application).allSatisfy { event in
            handlers(in: hooks[event], application: application).contains(where: isOwn)
        }
    }

    public static func installing(_ application: AgentApplication, command: String,
                                  into config: [String: Any]) throws -> [String: Any] {
        var result = try removing(application, from: config)
        var hooks = result["hooks"] as? [String: Any] ?? [:]
        for event in events(for: application) {
            var list = hooks[event] as? [Any] ?? []
            switch application {
            case .cursor:
                list.append(["command": command, "timeout": 5] as [String: Any])
            case .claude, .codex:
                // Codex limits Interrupt and SessionEnd hooks to three seconds.
                let timeout = event == "Interrupt" || event == "SessionEnd" ? 3 : 5
                list.append(["hooks": [["type": "command", "command": command,
                                        "timeout": timeout] as [String: Any]]] as [String: Any])
            }
            hooks[event] = list
        }
        result["hooks"] = hooks
        if application == .cursor, result["version"] == nil {
            result["version"] = 1
        }
        return result
    }

    public static func removing(_ application: AgentApplication,
                                from config: [String: Any]) throws -> [String: Any] {
        var result = config
        guard let rawHooks = config["hooks"] else { return result }
        guard var hooks = rawHooks as? [String: Any] else {
            throw Failure.unexpectedShape("\"hooks\" is not an object")
        }
        for (event, value) in hooks {
            guard let list = value as? [Any] else {
                if events(for: application).contains(event) {
                    throw Failure.unexpectedShape("hooks.\(event) is not a list")
                }
                continue
            }
            var kept: [Any] = []
            var removed = false
            for entry in list {
                if application == .cursor {
                    if let handler = entry as? [String: Any], isOwn(handler) {
                        removed = true
                    } else {
                        kept.append(entry)
                    }
                    continue
                }
                guard var group = entry as? [String: Any],
                      let groupHooks = group["hooks"] as? [Any] else {
                    kept.append(entry)
                    continue
                }
                let others = groupHooks.filter { !(($0 as? [String: Any]).map(isOwn) ?? false) }
                if others.count == groupHooks.count {
                    kept.append(entry)
                    continue
                }
                removed = true
                if !others.isEmpty {
                    group["hooks"] = others
                    kept.append(group)
                }
            }
            if removed {
                hooks[event] = kept.isEmpty ? nil : kept
            }
        }
        result["hooks"] = hooks.isEmpty ? nil : hooks
        return result
    }

    private static func handlers(in value: Any?, application: AgentApplication) -> [[String: Any]] {
        guard let list = value as? [Any] else { return [] }
        if application == .cursor { return list.compactMap { $0 as? [String: Any] } }
        return list.compactMap { ($0 as? [String: Any])?["hooks"] as? [Any] }
            .flatMap { $0.compactMap { $0 as? [String: Any] } }
    }

    private static func isOwn(_ handler: [String: Any]) -> Bool {
        (handler["command"] as? String)?.contains(helperName) ?? false
    }
}
