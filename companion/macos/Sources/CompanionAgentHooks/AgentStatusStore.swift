import Darwin
import Foundation

/// Per-session agent state from hook events, aggregated per application.
/// Nothing is persisted: after a Companion restart every row is unknown until
/// new events arrive.
public final class AgentStatusStore {
    public static let maxSessions = 64
    /// A session with no events for this long leaves aggregation. Expiry never
    /// produces NEEDS YOU or DONE.
    public static let workingExpiry: TimeInterval = 15 * 60
    public static let needsYouExpiry: TimeInterval = 2 * 60 * 60
    /// Cursor's `stop` can fire between model turns; a finish counts only when
    /// no new activity follows within this delay.
    public static let cursorStopDebounce: TimeInterval = 0.7
    /// After this long the latest finish is reported as done earlier, so a
    /// fresh DONE stands out from an old one.
    public static let doneRecent: TimeInterval = 10 * 60
    /// An approval request counts as NEEDS YOU only when no other event
    /// follows within this time: auto-review, policies and other hooks answer
    /// many requests without the user.
    public static let permissionGrace: TimeInterval = 15

    private enum Phase { case working, needsYou, finished }

    private struct Key: Hashable {
        let application: AgentApplication
        let session: String
    }

    private struct Session {
        var phase: Phase
        var turn: String?
        var ownerPid: Int32
        var updatedAt: Date
        var pendingFinishAt: Date?
        var finishedAt: Date?
        var permissionRequestedAt: Date?
    }

    private let now: () -> Date
    private let isAlive: (Int32) -> Bool
    private let lock = NSLock()
    private var sessions: [Key: Session] = [:]
    private var lastEvents: [AgentApplication: Date] = [:]

    public init(now: @escaping () -> Date = Date.init,
                isAlive: @escaping (Int32) -> Bool = AgentStatusStore.processAlive) {
        self.now = now
        self.isAlive = isAlive
    }

    public static func processAlive(_ pid: Int32) -> Bool {
        kill(pid, 0) == 0 || errno == EPERM
    }

    public func ingest(_ event: AgentHookEvent) {
        lock.lock()
        defer { lock.unlock() }
        let time = now()
        lastEvents[event.application] = time
        let key = Key(application: event.application, session: event.session)
        switch event.transition {
        case .ignored:
            return
        case .sessionEnded:
            sessions[key] = nil
        case .working, .needsYou, .permissionRequested:
            var session = sessions[key] ?? Session(phase: .working, turn: nil, ownerPid: 0,
                                                   updatedAt: time)
            let startsTurn = event.event == (event.application == .cursor
                ? "beforeSubmitPrompt" : "UserPromptSubmit")
            if startsTurn {
                // A new prompt owns the turn, including when its ID is absent.
                session.turn = event.turn
            } else {
                // Late tool results and waits must not restore an earlier turn.
                if let current = session.turn, let turn = event.turn, current != turn { return }
                // After a Companion restart, the first activity can establish it.
                session.turn = event.turn ?? session.turn
            }
            // A result can arrive after Claude's Stop. Only a new prompt or
            // newly started tool proves that the completed session resumed.
            if event.application == .claude, session.phase == .finished,
               event.event == "PostToolUse" || event.event == "PostToolUseFailure" { return }
            if event.transition == .permissionRequested {
                // Stays as it is until the grace period passes unanswered.
                if session.phase == .finished { session.phase = .working }
                session.permissionRequestedAt = session.permissionRequestedAt ?? time
            } else {
                session.phase = event.transition == .working ? .working : .needsYou
                session.permissionRequestedAt = nil
            }
            session.ownerPid = event.ownerPid > 0 ? event.ownerPid : session.ownerPid
            session.updatedAt = time
            session.pendingFinishAt = nil
            session.finishedAt = nil
            sessions[key] = session
        case .finished:
            var session = sessions[key] ?? Session(phase: .working, turn: event.turn, ownerPid: 0,
                                                   updatedAt: time)
            // A delayed stop for an earlier turn must not finish the current one.
            if let current = session.turn, let turn = event.turn, current != turn { return }
            session.ownerPid = event.ownerPid > 0 ? event.ownerPid : session.ownerPid
            session.updatedAt = time
            session.permissionRequestedAt = nil
            if event.application == .cursor {
                session.pendingFinishAt = time
            } else {
                session.phase = .finished
                session.finishedAt = time
            }
            sessions[key] = session
        }
        if sessions.count > Self.maxSessions,
           let oldest = sessions.min(by: { $0.value.updatedAt < $1.value.updatedAt })?.key {
            sessions[oldest] = nil
        }
    }

    /// The aggregated state of every application, installed or not.
    public func states() -> [AgentApplication: AgentState] {
        lock.lock()
        defer { lock.unlock() }
        let time = now()
        for (key, var session) in sessions {
            if let pending = session.pendingFinishAt,
               time.timeIntervalSince(pending) >= Self.cursorStopDebounce {
                session.phase = .finished
                session.finishedAt = pending
                session.pendingFinishAt = nil
                sessions[key] = session
            }
            if let requested = session.permissionRequestedAt,
               time.timeIntervalSince(requested) >= Self.permissionGrace {
                session.phase = .needsYou
                session.permissionRequestedAt = nil
                session.updatedAt = requested
                sessions[key] = session
            }
            let age = time.timeIntervalSince(session.updatedAt)
            let expired = (session.phase == .working && age > Self.workingExpiry) ||
                (session.phase == .needsYou && age > Self.needsYouExpiry)
            if expired || (session.ownerPid > 0 && !isAlive(session.ownerPid)) {
                sessions[key] = nil
            }
        }
        var states: [AgentApplication: AgentState] = [:]
        for application in AgentApplication.allCases {
            let own = sessions.filter { $0.key.application == application }.map(\.value)
            let phases = own.map(\.phase)
            if phases.contains(.needsYou) {
                states[application] = .needsYou
            } else if phases.contains(.working) {
                states[application] = .working
            } else if phases.contains(.finished) {
                let latest = own.compactMap(\.finishedAt).max() ?? time
                states[application] = time.timeIntervalSince(latest) < Self.doneRecent
                    ? .done : .doneEarlier
            } else {
                states[application] = .unknown
            }
        }
        return states
    }

    /// When the last hook event of any kind arrived, for Diagnostics.
    public func lastEvent(_ application: AgentApplication) -> Date? {
        lock.lock()
        defer { lock.unlock() }
        return lastEvents[application]
    }
}
