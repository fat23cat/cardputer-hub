import CompanionCore
import Foundation
import Security

enum CursorCredentialResult {
    case value(String)
    case absent
    case failed
}

protocol CursorCredentialReading {
    func read() -> CursorCredentialResult
}

final class KeychainCursorCredentials: CursorCredentialReading {
    func read() -> CursorCredentialResult {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "cursor-access-token",
            kSecAttrAccount as String: "cursor-user",
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: CFTypeRef?
        let status = LegacyKeychainRead.shared.copyMatching(query as CFDictionary,
                                                           allowInteraction: true, result: &result)
        if status == errSecItemNotFound { return .absent }
        guard status == errSecSuccess else { return .failed }
        guard let data = result as? Data,
              let value = String(data: data, encoding: .utf8), !value.isEmpty,
              !value.contains("\r"), !value.contains("\n"), !value.contains(";")
        else { return .absent }
        return .value(value)
    }
}

final class CursorUsageProvider: AiUsageProviderRefreshing {
    private let credentials: CursorCredentialReading
    private let http: AiUsageHTTPTransport
    private let readTimeout: TimeInterval
    // macOS may block the Keychain read on its access prompt. The collector
    // calls refresh on its shared queue, so the read runs here and a refresh
    // stops waiting after readTimeout.
    private let readQueue = DispatchQueue(label: "org.cardputer.companion.cursor-keychain")
    private let lock = NSLock()
    private var reading = false
    /// A read answered after its refresh stopped waiting; the next refresh
    /// uses it instead of asking the Keychain (and the user) again.
    private var lateResult: CursorCredentialResult?

    convenience init() {
        self.init(credentials: KeychainCursorCredentials(), http: URLSessionAiUsageHTTP())
    }

    init(credentials: CursorCredentialReading, http: AiUsageHTTPTransport,
         readTimeout: TimeInterval = 5) {
        self.credentials = credentials
        self.http = http
        self.readTimeout = readTimeout
    }

    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) {
        let finish = RefreshCompletion(done)
        lock.lock()
        if let result = lateResult {
            lateResult = nil
            lock.unlock()
            complete(result, finish)
            return
        }
        // An open Keychain prompt is waiting for the user, not failing.
        guard !reading else { lock.unlock(); finish(.waiting); return }
        reading = true
        lock.unlock()
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + readTimeout) {
            finish(.waiting)
        }
        readQueue.async { [self] in
            let result = credentials.read()
            lock.lock()
            reading = false
            let late = finish.finished
            if late { lateResult = result }
            lock.unlock()
            if !late { complete(result, finish) }
        }
    }

    private func complete(_ result: CursorCredentialResult, _ finish: RefreshCompletion) {
        switch result {
        case .value(let token): request(token, finish)
        case .absent: finish(.absent)
        case .failed: finish(.failed)
        }
    }

    private func request(_ token: String, _ done: RefreshCompletion) {
        guard let cookie = Self.cookie(for: token) else { done(.failed); return }
        guard let url = URL(string: "https://cursor.com/api/usage-summary")
        else { done(.failed); return }
        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue(cookie, forHTTPHeaderField: "Cookie")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        http.get(request) { data, response, _ in
            guard response?.statusCode == 200,
                  let data, data.count <= 128 * 1024,
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
            else { done(.failed); return }
            done(AiUsageNormalization.cursor(object).map { .sample($0) } ?? .failed)
        }
    }

    func stop() { http.stop() }

    private static func cookie(for token: String) -> String? {
        let parts = token.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 3, parts.allSatisfy({ !$0.isEmpty }) else { return nil }
        var base64 = String(parts[1]).replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        base64 += String(repeating: "=", count: (4 - base64.count % 4) % 4)
        guard let payload = Data(base64Encoded: base64),
              let claims = try? JSONSerialization.jsonObject(with: payload) as? [String: Any],
              let subject = claims["sub"] as? String,
              let userId = subject.split(separator: "|", omittingEmptySubsequences: false).last,
              !userId.isEmpty else { return nil }
        let allowed = CharacterSet(charactersIn:
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
        guard let encoded = "\(userId)::\(token)".addingPercentEncoding(withAllowedCharacters: allowed)
        else { return nil }
        return "WorkosCursorSessionToken=\(encoded)"
    }
}
