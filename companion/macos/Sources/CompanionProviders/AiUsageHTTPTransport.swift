import Foundation

protocol AiUsageHTTPTransport: AnyObject {
    func get(_ request: URLRequest,
             completion: @escaping (Data?, HTTPURLResponse?, Error?) -> Void)
    func stop()
}

final class URLSessionAiUsageHTTP: AiUsageHTTPTransport {
    private let lock = NSLock()
    private var session = URLSessionAiUsageHTTP.makeSession()

    private static func makeSession() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 10
        config.timeoutIntervalForResource = 12
        return URLSession(configuration: config)
    }

    func get(_ request: URLRequest,
             completion: @escaping (Data?, HTTPURLResponse?, Error?) -> Void) {
        lock.lock(); let session = self.session; lock.unlock()
        session.dataTask(with: request) { data, response, error in
            completion(data, response as? HTTPURLResponse, error)
        }.resume()
    }

    /// Cancels requests in flight. An invalidated session cannot start new
    /// tasks, so a fresh one serves a collector that starts again.
    func stop() {
        lock.lock()
        let previous = session
        session = Self.makeSession()
        lock.unlock()
        previous.invalidateAndCancel()
    }
}
