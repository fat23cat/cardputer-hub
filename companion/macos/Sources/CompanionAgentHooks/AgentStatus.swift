import Foundation

/// Desktop AI applications, numbered like the AI_USAGE provider IDs.
public enum AgentApplication: UInt8, CaseIterable {
    case codex = 1
    case cursor = 2
    case claude = 3

    /// The `--app` argument of the hook helper.
    public var name: String {
        switch self {
        case .codex: return "codex"
        case .cursor: return "cursor"
        case .claude: return "claude"
        }
    }

    public init?(name: String) {
        guard let match = Self.allCases.first(where: { $0.name == name }) else { return nil }
        self = match
    }
}

public enum AgentState: UInt8 {
    /// No live session, hooks not firing, or nothing observed yet.
    case unknown = 0
    case working = 1
    /// A permission or question wait, or an execution error.
    case needsYou = 2
    case done = 3
    /// Done more than `AgentStatusStore.doneRecent` ago; shown settled.
    case doneEarlier = 4
}

/// The AI_AGENT_STATUS response: the applications whose hooks are installed,
/// each with one state.
public struct AgentStatusSnapshot: Equatable {
    /// Wire order: Codex, Claude, Cursor (the Cardputer row order).
    public static let wireOrder: [AgentApplication] = [.codex, .claude, .cursor]

    /// Installed applications and their states; absent means not installed.
    public var states: [AgentApplication: AgentState]

    public init(states: [AgentApplication: AgentState] = [:]) {
        self.states = states
    }

    public func state(_ application: AgentApplication) -> AgentState {
        states[application] ?? .unknown
    }

    /// Count (0-3), then one (application, state) pair per installed
    /// application in the wire order.
    public func encode() -> [UInt8] {
        let installed = Self.wireOrder.filter { states[$0] != nil }
        return [UInt8(installed.count)] + installed.flatMap { [$0.rawValue, state($0).rawValue] }
    }

    public static func decode(_ payload: [UInt8]) -> AgentStatusSnapshot? {
        guard let count = payload.first, count <= wireOrder.count,
              payload.count == 1 + 2 * Int(count) else { return nil }
        var states: [AgentApplication: AgentState] = [:]
        var next = 0
        for index in stride(from: 1, to: payload.count, by: 2) {
            guard let application = AgentApplication(rawValue: payload[index]),
                  let position = wireOrder.firstIndex(of: application), position >= next,
                  let state = AgentState(rawValue: payload[index + 1]) else { return nil }
            states[application] = state
            next = position + 1
        }
        return AgentStatusSnapshot(states: states)
    }
}

/// The source of the cached snapshot that answers AI_AGENT_STATUS.
public protocol AgentStatusProviding: AnyObject {
    func agentStatus() -> AgentStatusSnapshot
}

/// Answers AI_AGENT_STATUS with only the applications whose hooks Companion
/// has installed. The installed set is read again at most every few seconds,
/// so polling once a second does not reread the configuration files.
public final class AgentStatusReport: AgentStatusProviding {
    public static let installedRefresh: TimeInterval = 5

    private let store: AgentStatusStore
    private let installed: () -> Set<AgentApplication>
    private let now: () -> Date
    private let lock = NSLock()
    private var cached: Set<AgentApplication> = []
    private var cachedAt: Date?

    public init(store: AgentStatusStore, installed: @escaping () -> Set<AgentApplication>,
                now: @escaping () -> Date = Date.init) {
        self.store = store
        self.installed = installed
        self.now = now
    }

    /// Rereads the installed set on the next answer, after an install or removal.
    public func invalidate() {
        lock.lock()
        cachedAt = nil
        lock.unlock()
    }

    public func agentStatus() -> AgentStatusSnapshot {
        lock.lock()
        let time = now()
        if cachedAt.map({ time.timeIntervalSince($0) >= Self.installedRefresh }) ?? true {
            cached = installed()
            cachedAt = time
        }
        let applications = cached
        lock.unlock()
        let states = store.states()
        return AgentStatusSnapshot(states: Dictionary(uniqueKeysWithValues:
            applications.map { ($0, states[$0] ?? .unknown) }))
    }
}
