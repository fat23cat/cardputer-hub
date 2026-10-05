import CompanionAgentHooks
import CompanionCore
import Foundation

/// Plan 047 checks: wire format, hook parsing, the event mapping, session
/// aggregation and expiry, and hook configuration merging.
func agentStatusChecks(_ expect: (Bool, String) -> Void, fixture: (String) -> [UInt8],
                       helloAck: (UInt16) -> [UInt8]) {
    // Wire format and session answer.
    let sample = AgentStatusSnapshot(states: [.codex: .working, .claude: .needsYou,
                                              .cursor: .doneEarlier])
    expect(AgentStatusSnapshot(states: [.codex: .working, .claude: .needsYou]).encode() ==
           [2, 1, 1, 3, 2], "only installed applications are listed")
    var none = CompanionEnvelope()
    none.kind = .response
    none.session = 42
    none.requestId = 13
    none.operation = .aiAgentStatus
    none.payload = AgentStatusSnapshot().encode()
    expect(CompanionCodec.encode(none) == fixture("ai-agent-status-response-none.bin"),
           "no installed hooks fixture")
    var response = CompanionEnvelope()
    response.kind = .response
    response.session = 42
    response.requestId = 13
    response.operation = .aiAgentStatus
    response.payload = sample.encode()
    expect(CompanionCodec.encode(response) == fixture("ai-agent-status-response.bin"),
           "agent status fixture")
    var request = CompanionEnvelope()
    request.kind = .request
    request.session = 42
    request.requestId = 13
    request.operation = .aiAgentStatus
    expect(CompanionCodec.encode(request) == fixture("ai-agent-status-request.bin"),
           "agent status request fixture")
    expect(AgentStatusSnapshot.decode(sample.encode()) == sample, "agent status round trip")
    expect(AgentStatusSnapshot.decode([2, 1, 1, 1, 2]) == nil, "an application appears once")
    expect(AgentStatusSnapshot.decode([2, 3, 2, 1, 1]) == nil, "applications follow the row order")
    expect(AgentStatusSnapshot.decode([1, 1, 5]) == nil, "unknown agent state")
    expect(AgentStatusSnapshot.decode([1, 1, 4]) == AgentStatusSnapshot(states: [.codex: .doneEarlier]),
           "done earlier is a valid state")
    expect(AgentStatusSnapshot.decode([2, 1, 1]) == nil, "truncated agent status")
    expect(AgentStatusSnapshot.decode([4, 1, 1, 3, 1, 2, 1, 1, 1]) == nil, "at most three applications")

    let store = AgentStatusStore(now: Date.init, isAlive: { _ in true })
    store.ingest(AgentHookEvent(application: .cursor, event: "beforeSubmitPrompt", session: "c"))
    var installedApps: Set<AgentApplication> = [.codex, .cursor]
    var installedReads = 0
    var reportClock = Date(timeIntervalSince1970: 0)
    let report = AgentStatusReport(store: store, installed: {
        installedReads += 1
        return installedApps
    }, now: { reportClock })
    let session = CompanionSession(applications: FakeApplications(), agentStatus: report)
    var sent: [[UInt8]] = []
    session.outgoing = { sent.append($0) }
    session.startHandshake()
    session.handle(helloAck(42))
    session.handle(CompanionCodec.encode(request)!)
    expect(CompanionCodec.decode(sent.last!)?.payload == [2, 1, 0, 2, 1],
           "AI_AGENT_STATUS lists installed applications with their states")
    installedApps = []
    _ = report.agentStatus()
    expect(installedReads == 1, "the installed set is cached between polls")
    reportClock += AgentStatusReport.installedRefresh
    expect(report.agentStatus() == AgentStatusSnapshot(), "a later poll rereads the installed set")
    installedApps = [.claude]
    report.invalidate()
    expect(report.agentStatus() == AgentStatusSnapshot(states: [.claude: .unknown]),
           "an install or removal is seen at once")
    let bare = CompanionSession(applications: FakeApplications())
    var bareSent: [[UInt8]] = []
    bare.outgoing = { bareSent.append($0) }
    bare.startHandshake()
    bare.handle(helloAck(42))
    bare.handle(CompanionCodec.encode(request)!)
    expect(CompanionCodec.decode(bareSent.last!)?.status == .notAvailable,
           "no store answers NOT_AVAILABLE")

    // Only allowlisted fields leave the hook (S25).
    let input = Data("""
    {"hook_event_name":"UserPromptSubmit","session_id":"s1","prompt_id":"p1",
     "prompt":"secret plan","cwd":"/Users/me/private","transcript_path":"/x.jsonl",
     "tool_input":{"command":"rm -rf"},"user_email":"me@example.com"}
    """.utf8)
    let parsed = AgentHookEvent.parse(application: .claude, input: input, ownerPid: 77)
    let wire = parsed?.wireData().flatMap { String(data: $0, encoding: .utf8) } ?? ""
    expect(parsed?.session == "s1" && parsed?.turn == "p1" && parsed?.ownerPid == 77,
           "hook parsing keeps session, turn and owner")
    expect(!wire.contains("secret") && !wire.contains("/Users") && !wire.contains("rm -rf") &&
           !wire.contains("example.com") && !wire.contains("jsonl"),
           "hook wire carries no prompt, path, command or e-mail")
    expect(parsed.flatMap { $0.wireData() }.flatMap(AgentHookEvent.init(wire:)) == parsed,
           "hook wire round trip")
    expect(AgentHookEvent(wire: Data(repeating: 0x20, count: AgentHookEvent.maxWireBytes + 1)) == nil,
           "oversized hook wire is rejected")
    let cursorStop = #"{"hook_event_name":"stop","conversation_id":"c9","generation_id":"g1","status":"aborted"}"#
    expect(AgentHookEvent.parse(application: .cursor, input: Data(cursorStop.utf8),
                                ownerPid: 0)?.session == "c9", "Cursor sessions use conversation_id")
    expect(AgentHookEvent.parse(application: .codex, input: Data("not json".utf8), ownerPid: 0) == nil,
           "malformed hook input is ignored")

    // Session aggregation (S1-S12).
    var clock = Date(timeIntervalSince1970: 1_000)
    var alive: Set<Int32> = [10, 11, 20, 30]
    let agents = AgentStatusStore(now: { clock }, isAlive: { alive.contains($0) })
    func event(_ app: AgentApplication, _ name: String, _ session: String, turn: String? = nil,
               status: String? = nil, notification: String? = nil, tool: String? = nil,
               pid: Int32 = 0) {
        agents.ingest(AgentHookEvent(application: app, event: name, session: session, turn: turn,
                                     status: status, notificationType: notification,
                                     toolName: tool, ownerPid: pid))
    }
    func state(_ app: AgentApplication) -> AgentState { agents.states()[app] ?? .unknown }

    expect(state(.claude) == .unknown, "nothing observed is unknown")
    event(.claude, "UserPromptSubmit", "a", turn: "p1", pid: 10)
    expect(state(.claude) == .working, "a prompt starts work")
    event(.claude, "PreToolUse", "a", tool: "Bash")
    event(.claude, "PermissionRequest", "a", tool: "Bash")
    expect(state(.claude) == .working, "a permission request may still be answered automatically")
    event(.claude, "PostToolUse", "a", tool: "Bash")
    clock += AgentStatusStore.permissionGrace
    expect(state(.claude) == .working, "an automatically approved request never needs you")
    event(.claude, "PermissionRequest", "a", tool: "Bash")
    clock += AgentStatusStore.permissionGrace
    expect(state(.claude) == .needsYou, "an unanswered permission request needs you")
    event(.claude, "PostToolUse", "a", tool: "Bash")
    expect(state(.claude) == .working, "the next tool event resumes work")
    event(.claude, "PreToolUse", "a", tool: "AskUserQuestion")
    expect(state(.claude) == .needsYou, "a question needs you")
    event(.claude, "Notification", "a", notification: "idle_prompt")
    expect(state(.claude) == .needsYou, "idle notifications change nothing")
    event(.claude, "Stop", "a", turn: "p0")
    expect(state(.claude) == .needsYou, "a stop for an earlier turn is ignored")
    event(.claude, "PostToolUse", "a")
    event(.claude, "Stop", "a", turn: "p1")
    expect(state(.claude) == .done, "a normal finish is done")
    event(.claude, "UserPromptSubmit", "b", pid: 11)
    event(.claude, "StopFailure", "b")
    expect(state(.claude) == .needsYou, "an execution error needs you, never done")

    event(.codex, "UserPromptSubmit", "x", turn: "t1", pid: 20)
    event(.codex, "UserPromptSubmit", "y", turn: "t2", pid: 20)
    event(.codex, "Stop", "x", turn: "t1")
    expect(state(.codex) == .working, "one of two chats finishing keeps working")
    event(.codex, "PermissionRequest", "y", turn: "t2")
    event(.codex, "UserPromptSubmit", "z", turn: "t3", pid: 20)
    expect(state(.codex) == .working, "a Codex auto-reviewed request is not a wait yet")
    clock += AgentStatusStore.permissionGrace
    event(.codex, "PostToolUse", "z")
    expect(state(.codex) == .needsYou, "a wait outranks concurrent work")
    event(.codex, "Interrupt", "y", turn: "t2")
    event(.codex, "SessionEnd", "z")
    expect(state(.codex) == .done, "a reported interruption is done")

    event(.cursor, "beforeSubmitPrompt", "c", turn: "g1", pid: 30)
    event(.cursor, "stop", "c", turn: "g1", status: "completed")
    expect(state(.cursor) == .working, "a Cursor stop waits for the debounce")
    clock += 0.3
    event(.cursor, "postToolUse", "c")
    clock += 1
    expect(state(.cursor) == .working, "activity after a Cursor stop cancels it")
    event(.cursor, "stop", "c", status: "aborted")
    clock += 1
    expect(state(.cursor) == .done, "a settled Cursor stop is done")
    clock += AgentStatusStore.doneRecent - 1.5
    expect(state(.cursor) == .done, "a Cursor finish ages from its stop, not its debounce")
    clock += 1
    expect(state(.cursor) == .doneEarlier, "a Cursor finish becomes done earlier")
    event(.cursor, "stop", "c", status: "error")
    expect(state(.cursor) == .needsYou, "a Cursor error needs you")
    event(.cursor, "sessionEnd", "c")
    expect(state(.cursor) == .unknown, "a session end removes the session")

    alive.remove(11)
    // Session a finished before the Cursor steps moved the clock past ten minutes.
    expect(state(.claude) == .doneEarlier, "an exited owner removes its session")
    event(.claude, "UserPromptSubmit", "c", pid: 10)
    clock += AgentStatusStore.workingExpiry + 1
    expect(state(.claude) == .doneEarlier, "an expired working session leaves aggregation")
    alive.remove(10)
    expect(state(.claude) == .unknown, "no live session is unknown")
    for index in 0..<(AgentStatusStore.maxSessions + 5) {
        event(.codex, "UserPromptSubmit", "many-\(index)")
    }
    expect(state(.codex) == .working, "session capacity stays bounded")
    expect(AgentStatusStore(now: { clock }, isAlive: { _ in true }).states() ==
           [.codex: .unknown, .claude: .unknown, .cursor: .unknown], "a new store starts empty")

    // Hook configuration (installer).
    let command = AgentHookConfiguration.command(helperPath: "/tmp/CardputerAgentHook",
                                                 application: .claude)
    let userHook: [String: Any] = ["hooks": [["type": "command", "command": "say done"]]]
    let claudeConfig: [String: Any] = ["model": "opus", "hooks": ["Stop": [userHook]]]
    let installed = try? AgentHookConfiguration.installing(.claude, command: command, into: claudeConfig)
    expect(installed.map { AgentHookConfiguration.isInstalled(.claude, in: $0) } == true,
           "Claude hooks install")
    let twice = installed.flatMap {
        try? AgentHookConfiguration.installing(.claude, command: command, into: $0)
    }
    let stops = (twice?["hooks"] as? [String: Any])?["Stop"] as? [Any]
    expect(stops?.count == 2, "reinstalling does not duplicate entries")
    let removed = twice.flatMap { try? AgentHookConfiguration.removing(.claude, from: $0) }
    let kept = (removed?["hooks"] as? [String: Any])?["Stop"] as? [[String: Any]]
    expect(removed?["model"] as? String == "opus" && kept?.count == 1 &&
           ((kept?.first?["hooks"] as? [[String: Any]])?.first?["command"] as? String) == "say done" &&
           (removed?["hooks"] as? [String: Any])?.count == 1,
           "removing keeps unrelated hooks and settings")
    let cursor = try? AgentHookConfiguration.installing(
        .cursor, command: AgentHookConfiguration.command(helperPath: "/tmp/CardputerAgentHook",
                                                         application: .cursor), into: [:])
    expect(cursor?["version"] as? Int == 1 &&
           ((cursor?["hooks"] as? [String: Any])?["stop"] as? [[String: Any]])?.count == 1,
           "Cursor hooks use the flat versioned format")
    let codex = try? AgentHookConfiguration.installing(.codex, command: "CardputerAgentHook", into: [:])
    let interrupt = ((codex?["hooks"] as? [String: Any])?["Interrupt"] as? [[String: Any]])?
        .first?["hooks"] as? [[String: Any]]
    expect(interrupt?.first?["timeout"] as? Int == 3, "Codex Interrupt hooks stay within 3 seconds")
    var shapeRejected = false
    do {
        _ = try AgentHookConfiguration.installing(.claude, command: command, into: ["hooks": [1]])
    } catch {
        shapeRejected = true
    }
    expect(shapeRejected, "an unexpected hooks shape is refused")
}
