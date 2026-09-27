import Foundation

protocol CodexJSONLTransport: AnyObject {
    var onData: ((Data) -> Void)? { get set }
    var onExit: (() -> Void)? { get set }
    var isRunning: Bool { get }
    func start(path: String) -> Bool
    func send(_ data: Data)
    func stop()
}

final class ProcessCodexJSONLTransport: CodexJSONLTransport {
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
        process.arguments = ["app-server", "--listen", "stdio://"]
        let input = Pipe(); let output = Pipe(); let errors = Pipe()
        process.standardInput = input
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

    func send(_ data: Data) { input?.fileHandleForWriting.write(data) }

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
