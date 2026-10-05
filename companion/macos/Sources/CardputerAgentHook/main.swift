import CompanionAgentHooks
import Darwin
import Foundation

// Run by Claude Code, Codex and Cursor hooks: `CardputerAgentHook --app <name>`.
// Forwards the allowlisted fields of one hook event to Cardputer Companion and
// exits. It never blocks or decides anything for the agent: when Companion is
// not running it exits at once, and it always exits 0 with the neutral answer
// each application expects.

/// Hard limit on the whole run, so a stuck pipe cannot hold the agent.
alarm(2)

let arguments = CommandLine.arguments
let application = arguments.firstIndex(of: "--app")
    .flatMap { arguments.indices.contains($0 + 1) ? arguments[$0 + 1] : nil }
    .flatMap(AgentApplication.init(name:))

var input = Data()
while input.count <= 16 * 1024 * 1024 {
    let chunk = FileHandle.standardInput.availableData
    if chunk.isEmpty { break }
    input.append(chunk)
}

/// The agent process: the nearest ancestor that is not a shell, because hook
/// runners start commands through `sh -c`.
func ownerPid() -> Int32 {
    let shells: Set<String> = ["sh", "bash", "zsh", "dash", "fish", "env"]
    var pid = getppid()
    for _ in 0..<8 {
        var info = kinfo_proc()
        var size = MemoryLayout<kinfo_proc>.stride
        var name: [Int32] = [CTL_KERN, KERN_PROC, KERN_PROC_PID, pid]
        guard pid > 1, sysctl(&name, 4, &info, &size, nil, 0) == 0, size > 0 else { return 0 }
        let command = withUnsafeBytes(of: info.kp_proc.p_comm) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        if !shells.contains(command) { return pid }
        pid = info.kp_eproc.e_ppid
    }
    return 0
}

func send(_ data: Data, to path: String) {
    let fd = socket(AF_UNIX, SOCK_STREAM, 0)
    guard fd >= 0 else { return }
    defer { close(fd) }
    var timeout = timeval(tv_sec: 0, tv_usec: 300_000)
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    var noSignal: Int32 = 1
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, socklen_t(MemoryLayout<Int32>.size))
    var address = sockaddr_un()
    address.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)
    guard bytes.count < MemoryLayout.size(ofValue: address.sun_path) else { return }
    withUnsafeMutableBytes(of: &address.sun_path) { buffer in
        buffer.copyBytes(from: bytes)
    }
    let connected = withUnsafePointer(to: &address) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
            connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
    guard connected == 0 else { return }
    _ = data.withUnsafeBytes { write(fd, $0.baseAddress, data.count) }
}

var eventName: String?
if let application,
   let event = AgentHookEvent.parse(application: application, input: input, ownerPid: ownerPid()) {
    eventName = event.event
    if let wire = event.wireData() {
        send(wire, to: AgentHookPaths.socketPath)
    }
}

switch application {
case .codex:
    print("{}")
case .cursor:
    print(eventName == "beforeSubmitPrompt" ? "{\"continue\":true}" : "{}")
case .claude, .none:
    break
}
exit(0)
