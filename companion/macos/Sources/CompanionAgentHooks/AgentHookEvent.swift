import Foundation

/// One hook event reduced to the fields needed for status. Prompts, answers,
/// paths, commands, tool input and output, and e-mail are never read.
public struct AgentHookEvent: Equatable {
    /// Longest kept string field, in characters.
    public static let maxFieldLength = 128
    /// Largest message the helper sends to Companion.
    public static let maxWireBytes = 2048

    public var application: AgentApplication
    public var event: String
    public var session: String
    public var turn: String?
    public var status: String?
    public var notificationType: String?
    public var toolName: String?
    /// The agent process that ran the hook, or 0 when unknown.
    public var ownerPid: Int32

    public init(application: AgentApplication, event: String, session: String, turn: String? = nil,
                status: String? = nil, notificationType: String? = nil, toolName: String? = nil,
                ownerPid: Int32 = 0) {
        self.application = application
        self.event = event
        self.session = session
        self.turn = turn
        self.status = status
        self.notificationType = notificationType
        self.toolName = toolName
        self.ownerPid = ownerPid
    }

    /// Reads a hook's JSON input. Only allowlisted keys are looked at.
    public static func parse(application: AgentApplication, input: Data,
                             ownerPid: Int32) -> AgentHookEvent? {
        guard let object = (try? JSONSerialization.jsonObject(with: input)) as? [String: Any],
              let event = field(object, "hook_event_name") else { return nil }
        let sessionKeys = application == .cursor ? ["conversation_id", "session_id"] : ["session_id"]
        let turnKey: String
        switch application {
        case .codex: turnKey = "turn_id"
        case .cursor: turnKey = "generation_id"
        case .claude: turnKey = "prompt_id"
        }
        return AgentHookEvent(
            application: application, event: event,
            session: sessionKeys.lazy.compactMap { field(object, $0) }.first ?? "",
            turn: field(object, turnKey), status: field(object, "status"),
            notificationType: field(object, "notification_type"),
            toolName: field(object, "tool_name"), ownerPid: max(0, ownerPid))
    }

    /// The compact JSON the helper sends over the local socket.
    public func wireData() -> Data? {
        var object: [String: Any] = ["app": application.name, "event": event, "session": session,
                                     "pid": Int(ownerPid)]
        object["turn"] = turn
        object["status"] = status
        object["notification"] = notificationType
        object["tool"] = toolName
        guard let data = try? JSONSerialization.data(withJSONObject: object),
              data.count <= Self.maxWireBytes else { return nil }
        return data
    }

    public init?(wire: Data) {
        guard wire.count <= Self.maxWireBytes,
              let object = (try? JSONSerialization.jsonObject(with: wire)) as? [String: Any],
              let name = object["app"] as? String, let application = AgentApplication(name: name),
              let event = Self.field(object, "event"),
              let session = object["session"] as? String, session.count <= Self.maxFieldLength,
              let pid = object["pid"] as? Int, pid >= 0, pid <= Int(Int32.max) else { return nil }
        self.init(application: application, event: event, session: session,
                  turn: Self.field(object, "turn"), status: Self.field(object, "status"),
                  notificationType: Self.field(object, "notification"),
                  toolName: Self.field(object, "tool"), ownerPid: Int32(pid))
    }

    private static func field(_ object: [String: Any], _ key: String) -> String? {
        guard let value = object[key] as? String, !value.isEmpty else { return nil }
        return String(value.prefix(maxFieldLength))
    }
}

/// What one event means for its session.
public enum AgentTransition: Equatable {
    case working
    /// An approval request that a policy, auto-review or another hook may still
    /// answer; it needs the user only if nothing happens for a while.
    case permissionRequested
    case needsYou
    case finished
    case sessionEnded
    case ignored
}

extension AgentHookEvent {
    /// The documented event mapping for each application (plan 047).
    public var transition: AgentTransition {
        switch application {
        case .claude:
            switch event {
            case "UserPromptSubmit", "PostToolUse", "PostToolUseFailure":
                return .working
            case "PreToolUse":
                return toolName == "AskUserQuestion" ? .needsYou : .working
            case "PermissionRequest":
                return .permissionRequested
            case "StopFailure":
                return .needsYou
            case "Notification":
                switch notificationType {
                case "permission_prompt", "elicitation_dialog": return .needsYou
                case "idle_prompt": return .finished
                default: return .ignored
                }
            case "Stop":
                return .finished
            case "SessionEnd":
                return .sessionEnded
            default:
                return .ignored
            }
        case .codex:
            switch event {
            case "UserPromptSubmit", "PreToolUse", "PostToolUse":
                return .working
            case "PermissionRequest":
                return .permissionRequested
            case "Stop", "Interrupt":
                return .finished
            case "SessionEnd":
                return .sessionEnded
            default:
                return .ignored
            }
        case .cursor:
            switch event {
            case "beforeSubmitPrompt", "postToolUse", "postToolUseFailure":
                return .working
            case "stop":
                switch status {
                case "error": return .needsYou
                case "completed", "aborted": return .finished
                default: return .ignored
                }
            case "sessionEnd":
                return .sessionEnded
            default:
                return .ignored
            }
        }
    }
}
