import CompanionCore
import Foundation

/// Fetches a status page for a Cardputer without Wi-Fi (plan 049). Only https
/// URLs reach it, and only when the Cardputer asks.
public final class URLSessionStatusPageFetcher: StatusPageFetching {
    /// Answered within the Cardputer's 10 s wait for a SERVICE_STATUS response.
    public static let timeout: TimeInterval = 8
    /// A `status.json` body is a few hundred bytes.
    public static let maxBodyBytes = 64 * 1024

    private let session: URLSession

    public init() {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = Self.timeout
        config.timeoutIntervalForResource = Self.timeout
        config.httpAdditionalHeaders = ["Accept": "application/json"]
        session = URLSession(configuration: config)
    }

    public func fetch(_ url: URL, completion: @escaping (StatusPageReport?) -> Void) {
        guard url.scheme == "https" else { return completion(nil) }
        let session = self.session
        Task {
            let report = await Self.load(url, session: session)
            DispatchQueue.main.async { completion(report) }
        }
    }

    /// Reads the body as it arrives and stops at `maxBodyBytes`, so the limit
    /// bounds the transfer, not only the result. Redirects must stay on https.
    static func load(_ url: URL, session: URLSession) async -> StatusPageReport? {
        guard let (bytes, response) = try? await session.bytes(from: url) else { return nil }
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode),
              http.url?.scheme == "https" else {
            bytes.task.cancel()
            return nil
        }
        var body = Data()
        do {
            for try await byte in bytes {
                body.append(byte)
                if body.count > maxBodyBytes {
                    bytes.task.cancel()
                    return nil
                }
            }
        } catch {
            return nil
        }
        return StatusPageReport.parse(body)
    }
}
