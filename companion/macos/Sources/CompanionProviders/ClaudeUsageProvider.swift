import CompanionCore
import Foundation
import Security

struct ClaudeCredential {
    let accessToken: String
    let subscriptionType: String?
    let expiresAt: Date?
}

enum ClaudeCredentialResult {
    case value(ClaudeCredential)
    case absent
    case denied
    case failed
    case interactionRequired
}

protocol ClaudeCredentialReading {
    func read(allowInteraction: Bool) -> ClaudeCredentialResult
}

// Claude Code owns this item and refreshes its token; the Companion only reads it.
final class KeychainClaudeCredentials: ClaudeCredentialReading {
    private let keychain: LegacyKeychainRead
    init(keychain: LegacyKeychainRead = .shared) { self.keychain = keychain }

    func read(allowInteraction: Bool) -> ClaudeCredentialResult {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "Claude Code-credentials",
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: CFTypeRef?
        let status = keychain.copyMatching(query as CFDictionary,
                                          allowInteraction: allowInteraction, result: &result)
        if status == errSecItemNotFound { return .absent }
        if status == errSecInteractionNotAllowed { return .interactionRequired }
        if status == errSecUserCanceled || status == errSecAuthFailed {
            return allowInteraction ? .denied : .interactionRequired
        }
        guard status == errSecSuccess, let data = result as? Data,
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let oauth = object["claudeAiOauth"] as? [String: Any],
              let token = oauth["accessToken"] as? String, !token.isEmpty,
              !token.contains("\r"), !token.contains("\n")
        else { return .failed }
        let expiresAt = (oauth["expiresAt"] as? NSNumber)
            .map { Date(timeIntervalSince1970: $0.doubleValue / 1000) }
        return .value(ClaudeCredential(accessToken: token,
                                       subscriptionType: oauth["subscriptionType"] as? String,
                                       expiresAt: expiresAt))
    }
}

/// Calls a refresh completion at most once, whichever of the read timeout and
/// the result comes first.
private final class RefreshCompletion {
    private let lock = NSLock()
    private var done: ((AiUsageRefreshOutcome) -> Void)?
    init(_ done: @escaping (AiUsageRefreshOutcome) -> Void) { self.done = done }
    var finished: Bool { lock.lock(); defer { lock.unlock() }; return done == nil }
    func callAsFunction(_ outcome: AiUsageRefreshOutcome) {
        lock.lock(); let callback = done; done = nil; lock.unlock()
        callback?(outcome)
    }
}

final class ClaudeUsageProvider: AiUsageProviderRefreshing {
    /// A declined initial prompt pauses even silent reads for this interval.
    static let deniedRetryInterval: TimeInterval = 3600
    /// The token lasts hours, but sign-out or an account switch must show up
    /// sooner, so the Keychain item is read again after this interval.
    static let credentialRecheckInterval: TimeInterval = 300
    /// The usage service rate-limits its callers, so it is asked at most this
    /// often; refreshes in between report the last sample.
    static let requestInterval: TimeInterval = 60
    /// Upper bound of the pause after repeated rate-limit responses.
    static let maximumRateLimitPause: TimeInterval = 1800
    /// A sample stays current for one request interval plus the collector's
    /// 90-second freshness window.
    static let sampleLifetime: TimeInterval = requestInterval + 90
    private let credentials: ClaudeCredentialReading
    private let http: AiUsageHTTPTransport
    private let now: () -> Date
    private let readTimeout: TimeInterval
    // macOS may block the Keychain read on its access prompt, so it runs on its
    // own queue and a refresh stops waiting after readTimeout.
    private let readQueue = DispatchQueue(label: "org.cardputer.companion.claude-keychain")
    private let lock = NSLock()
    private var credential: ClaudeCredential?
    private var credentialReadAt = Date.distantPast
    private var reading = false
    // Only the first read in this provider's lifetime may request authorization.
    // Wake, periodic rechecks, and expired-token recovery must remain silent.
    private var attemptedCredentialRead = false
    private var interactionRetryAt = Date.distantPast
    private var deniedAt: Date?
    private var lastSample: (sample: AiUsageProviderSnapshot, at: Date)?
    private var nextRequestAt = Date.distantPast
    private var rateLimitPause: TimeInterval = 0

    convenience init() {
        self.init(credentials: KeychainClaudeCredentials(), http: URLSessionAiUsageHTTP())
    }

    init(credentials: ClaudeCredentialReading, http: AiUsageHTTPTransport,
         now: @escaping () -> Date = Date.init, readTimeout: TimeInterval = 5) {
        self.credentials = credentials
        self.http = http
        self.now = now
        self.readTimeout = readTimeout
    }

    func refresh(_ done: @escaping (AiUsageRefreshOutcome) -> Void) {
        let finish = RefreshCompletion(done)
        let currentTime = now()
        lock.lock()
        if let deniedAt, currentTime.timeIntervalSince(deniedAt) < Self.deniedRetryInterval {
            lock.unlock(); finish(.absent); return
        }
        if let credential, !expired(credential) {
            // Recheck beside the request, so a failed or slow read never costs a
            // working token; sign-out or an account switch lands on a later refresh.
            let recheck = !reading &&
                currentTime.timeIntervalSince(credentialReadAt) >= Self.credentialRecheckInterval
            if recheck { reading = true }
            let throttled = currentTime < nextRequestAt
            let cached = cachedOutcome(currentTime)
            lock.unlock()
            if recheck { readKeychain(nil, allowInteraction: false) }
            if throttled { finish(cached) } else { request(credential, finish) }
            return
        }
        // An open Keychain prompt is waiting for the user, not failing.
        guard !reading else { lock.unlock(); finish(.waiting); return }
        guard currentTime >= interactionRetryAt else {
            let cached = cachedOutcome(currentTime)
            lock.unlock(); finish(cached); return
        }
        let allowInteraction = !attemptedCredentialRead
        attemptedCredentialRead = true
        reading = true
        lock.unlock()
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + readTimeout) {
            finish(.waiting)
        }
        readKeychain(finish, allowInteraction: allowInteraction)
    }

    private func readKeychain(_ finish: RefreshCompletion?, allowInteraction: Bool) {
        readQueue.async { [self] in
            let result = credentials.read(allowInteraction: allowInteraction)
            lock.lock()
            reading = false
            switch result {
            case .value(let value):
                credential = value; credentialReadAt = now(); deniedAt = nil
                interactionRetryAt = .distantPast
            case .absent: credential = nil; deniedAt = nil
            case .denied: credential = nil; deniedAt = now()
            // Keep a still-valid token; the next recheck is due in five minutes.
            case .interactionRequired:
                credentialReadAt = now()
                interactionRetryAt = now().addingTimeInterval(Self.credentialRecheckInterval)
            case .failed: if credential != nil { credentialReadAt = now() }
            }
            let throttled = now() < nextRequestAt
            let cached = cachedOutcome(now())
            lock.unlock()
            guard let finish, !finish.finished else { return }
            switch result {
            // Claude Code refreshes its token only while it runs.
            case .value(let value) where expired(value): finish(cached)
            case .value where throttled: finish(cached)
            case .value(let value): request(value, finish)
            case .absent, .denied: finish(.absent)
            case .interactionRequired: finish(cached)
            case .failed: finish(.failed)
            }
        }
    }

    func stop() { http.stop() }

    /// The last sample, aged to `now`, while it is recent; otherwise unavailable.
    /// Callers hold `lock`.
    private func cachedOutcome(_ now: Date) -> AiUsageRefreshOutcome {
        guard let lastSample, now.timeIntervalSince(lastSample.at) < Self.sampleLifetime
        else { return .unavailable }
        return .sample(lastSample.sample.aged(now: now))
    }

    private func expired(_ credential: ClaudeCredential) -> Bool {
        credential.expiresAt.map { $0 <= now() } ?? false
    }

    private func request(_ credential: ClaudeCredential, _ finish: RefreshCompletion) {
        guard let url = URL(string: "https://api.anthropic.com/api/oauth/usage")
        else { finish(.failed); return }
        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue("Bearer \(credential.accessToken)", forHTTPHeaderField: "Authorization")
        request.setValue("oauth-2025-04-20", forHTTPHeaderField: "anthropic-beta")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        http.get(request) { [self] data, response, _ in
            let currentTime = now()
            lock.lock(); defer { lock.unlock() }
            switch response?.statusCode {
            case 401, 403:
                if self.credential?.accessToken == credential.accessToken { self.credential = nil }
                finish(.failed)
            case 429:
                // Waiting is the only fix, so this is not a failure to retry.
                rateLimitPause = min(max(rateLimitPause * 2, Self.requestInterval),
                                     Self.maximumRateLimitPause)
                let retryAfter = response?.value(forHTTPHeaderField: "Retry-After")
                    .flatMap(TimeInterval.init) ?? 0
                nextRequestAt = currentTime.addingTimeInterval(
                    min(max(rateLimitPause, retryAfter), Self.maximumRateLimitPause))
                finish(cachedOutcome(currentTime))
            case 200:
                guard let data, data.count <= 128 * 1024,
                      let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                      let sample = AiUsageNormalization.claude(
                          object, subscriptionType: credential.subscriptionType, now: currentTime)
                else { fallthrough }
                lastSample = (sample, currentTime)
                nextRequestAt = currentTime.addingTimeInterval(Self.requestInterval)
                rateLimitPause = 0
                finish(.sample(sample))
            default:
                // Keep collector retries from reaching the service more than once a minute.
                nextRequestAt = currentTime.addingTimeInterval(Self.requestInterval)
                finish(.failed)
            }
        }
    }
}
