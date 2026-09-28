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
        let status = SecItemCopyMatching(query as CFDictionary, &result)
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

    convenience init() {
        self.init(credentials: KeychainCursorCredentials(), http: URLSessionAiUsageHTTP())
    }

    init(credentials: CursorCredentialReading, http: AiUsageHTTPTransport) {
        self.credentials = credentials
        self.http = http
    }

    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) {
        let token: String
        switch credentials.read() {
        case .value(let value): token = value
        case .absent: done(.absent); return
        case .failed: done(.failed); return
        }
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
