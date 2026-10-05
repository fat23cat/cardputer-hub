import CompanionAgentHooks
import Darwin
import Foundation
import os.log

private let log = Logger(subsystem: "org.cardputer.companion", category: "agent-hooks")

/// Receives hook events from `CardputerAgentHook` on a private Unix-domain
/// socket and hands them to the store. Event content is never logged.
final class AgentHookReceiver {
    private let store: AgentStatusStore
    private let queue = DispatchQueue(label: "org.cardputer.companion.agent-hooks")
    private var listener: Int32 = -1
    private var source: DispatchSourceRead?

    init(store: AgentStatusStore) {
        self.store = store
    }

    func start() {
        queue.async { [weak self] in self?.listen() }
    }

    func stop() {
        queue.sync {
            source?.cancel()
            source = nil
        }
    }

    private func listen() {
        let directory = AgentHookPaths.supportDirectory
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
        chmod(directory.path, 0o700)
        let path = AgentHookPaths.socketPath
        unlink(path)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return log.error("agent hook socket failed") }
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(path.utf8)
        guard bytes.count < MemoryLayout.size(ofValue: address.sun_path) else {
            close(fd)
            return log.error("agent hook socket path too long")
        }
        withUnsafeMutableBytes(of: &address.sun_path) { $0.copyBytes(from: bytes) }
        let previousMask = umask(0o177)
        let bound = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        umask(previousMask)
        guard bound == 0, Darwin.listen(fd, 16) == 0 else {
            close(fd)
            return log.error("agent hook socket bind failed")
        }
        listener = fd
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.accept() }
        source.setCancelHandler {
            close(fd)
            unlink(path)
        }
        self.source = source
        source.resume()
    }

    private func accept() {
        let client = Darwin.accept(listener, nil, nil)
        guard client >= 0 else { return }
        defer { close(client) }
        // Only the current user's processes may report events.
        var uid = uid_t()
        var gid = gid_t()
        guard getpeereid(client, &uid, &gid) == 0, uid == getuid() else { return }
        var timeout = timeval(tv_sec: 0, tv_usec: 500_000)
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        var data = Data()
        var buffer = [UInt8](repeating: 0, count: 1024)
        while data.count <= AgentHookEvent.maxWireBytes {
            let count = read(client, &buffer, buffer.count)
            if count <= 0 { break }
            data.append(contentsOf: buffer[..<count])
        }
        // Oversized or malformed input is dropped without blocking anything.
        guard let event = AgentHookEvent(wire: data) else { return }
        store.ingest(event)
    }
}
