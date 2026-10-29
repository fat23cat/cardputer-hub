import Darwin
import Foundation

protocol CodexJSONLTransport: AnyObject {
    var onData: ((Data) -> Void)? { get set }
    var onExit: (() -> Void)? { get set }
    var isRunning: Bool { get }
    func start(path: String) -> Bool
    /// False when the child no longer reads its input (it exited or closed it).
    func send(_ data: Data) -> Bool
    func stop()
}

final class ProcessCodexJSONLTransport: CodexJSONLTransport {
    // Restrict only this child; keep the user's configuration and authentication intact.
    static let launchArguments = [
        "-c", "analytics.enabled=false",
        "-c", "otel.exporter=\"none\"",
        "-c", "otel.trace_exporter=\"none\"",
        "-c", "otel.metrics_exporter=\"none\"",
        "-c", "features.plugins=false",
        "-c", "features.remote_plugin=false",
        "-c", "features.hooks=false",
        "-c", "features.apps=false",
        "app-server", "--listen", "stdio://",
    ]

    private let lock = NSLock()
    private var dataHandler: ((Data) -> Void)?
    private var exitHandler: (() -> Void)?
    private var process: Process?
    private var input: Pipe?
    private var output: Pipe?
    private var errors: Pipe?

    var onData: ((Data) -> Void)? {
        get { lock.lock(); defer { lock.unlock() }; return dataHandler }
        set { lock.lock(); dataHandler = newValue; lock.unlock() }
    }
    var onExit: (() -> Void)? {
        get { lock.lock(); defer { lock.unlock() }; return exitHandler }
        set { lock.lock(); exitHandler = newValue; lock.unlock() }
    }
    var isRunning: Bool { process?.isRunning == true }

    func start(path: String) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: path)
        process.arguments = Self.launchArguments
        let input = Pipe(); let output = Pipe(); let errors = Pipe()
        process.standardInput = input
        // A child that exits closes its input; writing then must fail with
        // EPIPE instead of raising SIGPIPE, which would end Companion.
        _ = fcntl(input.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)
        process.standardOutput = output
        process.standardError = errors
        errors.fileHandleForReading.readabilityHandler = { handle in
            _ = handle.availableData
        }
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            self?.onData?(data)
        }
        process.terminationHandler = { [weak self] _ in self?.onExit?() }
        self.process = process
        self.input = input
        self.output = output
        self.errors = errors
        do { try process.run() } catch {
            stop()
            return false
        }
        return true
    }

    func send(_ data: Data) -> Bool {
        guard let handle = input?.fileHandleForWriting else { return false }
        do {
            try handle.write(contentsOf: data)
            return true
        } catch {
            return false
        }
    }

    func stop() {
        onData = nil
        onExit = nil
        output?.fileHandleForReading.readabilityHandler = nil
        errors?.fileHandleForReading.readabilityHandler = nil
        if process?.isRunning == true { process?.terminate() }
        process = nil
        input = nil
        output = nil
        errors = nil
    }
}
