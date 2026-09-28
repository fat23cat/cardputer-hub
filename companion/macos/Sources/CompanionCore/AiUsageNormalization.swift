import Foundation

public enum AiUsageNormalization {
    private static func dictionary(_ value: Any?) -> [String: Any]? { value as? [String: Any] }
    private static func number(_ value: Any?) -> Double? {
        let number: Double
        if let value = value as? NSNumber {
            number = value.doubleValue
        } else if let value = value as? String, let parsed = Double(value) {
            number = parsed
        } else {
            return nil
        }
        return number.isFinite && number >= 0 ? number : nil
    }
    private static func whole(_ value: Any?) -> UInt32? {
        guard let value = number(value) else { return nil }
        let rounded = value.rounded()
        guard rounded <= Double(UInt32.max) else { return nil }
        return UInt32(rounded)
    }
    private static func percent(_ value: Any?) -> UInt8? {
        guard let value = number(value), value <= 100 else { return nil }
        return UInt8(value.rounded())
    }
    private static func reset(_ value: Any?, now: Date) -> (UInt32, UInt32) {
        let timestamp: UInt32?
        if let raw = value as? String {
            let fractional = ISO8601DateFormatter()
            fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
            let date = fractional.date(from: raw) ?? ISO8601DateFormatter().date(from: raw) ??
                ISO8601DateFormatter().date(from: raw + "T00:00:00Z")
            timestamp = date.flatMap { $0.timeIntervalSince1970 >= 0 &&
                $0.timeIntervalSince1970 <= Double(UInt32.max)
                ? UInt32($0.timeIntervalSince1970) : nil }
        } else {
            timestamp = whole(value)
        }
        guard let timestamp else { return (0, 0) }
        let seconds = max(0, Double(timestamp) - now.timeIntervalSince1970)
        return (timestamp, UInt32(min(seconds, Double(UInt32.max))))
    }
    private static func plan(_ text: String?) -> AiPlan {
        switch text?.lowercased() {
        case "plus": return .plus
        case "business": return .business
        case "enterprise": return .enterprise
        default: return .unknown
        }
    }

    private static func creditTitle(_ value: Any?) -> String {
        guard let supplied = (value as? String)?.trimmingCharacters(in: .whitespacesAndNewlines),
              !supplied.isEmpty else { return "RESET CREDIT" }
        let title = supplied.uppercased()
        if title == "FULL RESET" || title.hasPrefix("FULL RESET ") ||
            title.hasPrefix("FULL RESET(") { return "FULL RESET" }
        guard title.unicodeScalars.allSatisfy({ (0x20...0x7e).contains($0.value) })
        else { return "RESET CREDIT" }
        if title.utf8.count <= 16 { return title }
        var prefix = ""
        for scalar in title.unicodeScalars {
            let bytes = String(scalar).utf8.count
            if prefix.utf8.count + bytes > 13 { break }
            prefix.append(String(scalar))
        }
        return prefix.isEmpty ? "RESET CREDIT" : prefix + "..."
    }

    private static func resetCredits(_ value: Any?, now: Date) -> AiResetCredits? {
        guard let data = dictionary(value), let countValue = number(data["availableCount"]),
              countValue.rounded(.down) == countValue, countValue <= 255 else { return nil }
        let availableCount = UInt8(countValue)
        let raw: [Any]
        if data["credits"] == nil || data["credits"] is NSNull {
            raw = []
        } else if let entries = data["credits"] as? [Any] {
            raw = entries
        } else {
            return nil
        }
        var credits: [(Int, AiResetCredit)] = []
        for (index, item) in raw.enumerated() {
            guard let entry = dictionary(item), let status = entry["status"] as? String else {
                return nil
            }
            guard status == "available" || status == "usable" else { continue }
            if let type = entry["resetType"] as? String, type != "codexRateLimits" {
                continue
            }
            let title = creditTitle(entry["title"])
            let (expiresAt, remaining) = reset(entry["expiresAt"], now: now)
            credits.append((index, AiResetCredit(title: title, expiresAt: expiresAt,
                                                 expiresRemainingSeconds: remaining)))
        }
        credits.sort {
            let left = $0.1.expiresAt == 0 ? UInt32.max : $0.1.expiresAt
            let right = $1.1.expiresAt == 0 ? UInt32.max : $1.1.expiresAt
            return left == right ? $0.0 < $1.0 : left < right
        }
        return AiResetCredits(availableCount: availableCount,
                              credits: Array(credits.prefix(min(Int(availableCount), 4))).map(\.1))
    }

    public static func codex(_ result: [String: Any], accountPlan: String? = nil,
                             now: Date = Date()) -> AiUsageProviderSnapshot? {
        let limits = dictionary(result["rateLimitsByLimitId"]).flatMap { dictionary($0["codex"]) }
            ?? dictionary(result["rateLimits"]) ?? result
        let detectedPlan = plan(limits["planType"] as? String ?? accountPlan)
        if detectedPlan != .plus, let credits = dictionary(limits["individualLimit"]) ??
                dictionary(dictionary(result["rateLimits"])?["individualLimit"]) ??
                dictionary(result["individualLimit"]),
           let limit = whole(credits["limit"]), let used = whole(credits["used"]),
           let remainingPercent = percent(credits["remainingPercent"]), limit > 0, used <= limit {
            let (resetAt, remainingSeconds) = reset(credits["resetsAt"], now: now)
            let metric = AiUsageMetric(kind: .credits, unit: .credits, used: used,
                                       limit: limit, remaining: limit - used,
                                       remainingPercent: remainingPercent, resetAt: resetAt,
                                       resetRemainingSeconds: remainingSeconds)
            return AiUsageProviderSnapshot(provider: .codex,
                                           plan: detectedPlan == .unknown ? .business : detectedPlan,
                                           metrics: [metric])
        }
        var metrics: [AiUsageMetric] = []
        for field in ["primary", "secondary"] {
            guard let window = dictionary(limits[field]),
                  let duration = whole(window["windowDurationMins"]),
                  let usedPercent = percent(window["usedPercent"]),
                  let kind: AiMetricKind = duration == 300 ? .fiveHour : duration == 10080 ? .week : nil
            else { continue }
            let (resetAt, remainingSeconds) = reset(window["resetsAt"], now: now)
            metrics.append(AiUsageMetric(kind: kind, unit: .percent, used: UInt32(usedPercent),
                                         limit: 100, remaining: UInt32(100 - usedPercent),
                                         remainingPercent: 100 - usedPercent,
                                         resetAt: resetAt, resetRemainingSeconds: remainingSeconds))
        }
        guard !metrics.isEmpty else { return nil }
        metrics.sort { $0.kind.rawValue < $1.kind.rawValue }
        let normalizedPlan = detectedPlan == .unknown ? AiPlan.plus : detectedPlan
        let resetValue = limits["rateLimitResetCredits"] ?? result["rateLimitResetCredits"]
        return AiUsageProviderSnapshot(provider: .codex, plan: normalizedPlan,
                                       metrics: Array(metrics.prefix(2)),
                                       resetCredits: normalizedPlan == .plus
                                           ? resetCredits(resetValue, now: now) : nil)
    }

    public static func claude(_ response: [String: Any], subscriptionType: String?,
                              now: Date = Date()) -> AiUsageProviderSnapshot? {
        func metric(_ kind: AiMetricKind, used value: Any?, resetsAt: Any?) -> AiUsageMetric? {
            guard let value = number(value) else { return nil }
            let used = UInt8(min(value, 100).rounded())
            let (resetAt, remainingSeconds) = reset(resetsAt, now: now)
            return AiUsageMetric(kind: kind, unit: .percent, used: UInt32(used), limit: 100,
                                 remaining: UInt32(100 - used), remainingPercent: 100 - used,
                                 resetAt: resetAt, resetRemainingSeconds: remainingSeconds)
        }
        // `limits` is the current shape; the per-window objects are the older one.
        // Scoped (model-specific) limits are not the plan-wide rolling windows.
        let limits = (response["limits"] as? [Any] ?? []).compactMap(dictionary)
        let windows: [(AiMetricKind, String, String)] = [(.fiveHour, "session", "five_hour"),
                                                          (.week, "weekly_all", "seven_day")]
        let metrics = windows.compactMap { kind, limitKind, legacyField -> AiUsageMetric? in
            if let entry = limits.first(where: {
                   $0["kind"] as? String == limitKind && ($0["scope"] == nil || $0["scope"] is NSNull)
               }), let value = metric(kind, used: entry["percent"], resetsAt: entry["resets_at"]) {
                return value
            }
            guard let window = dictionary(response[legacyField]) else { return nil }
            return metric(kind, used: window["utilization"], resetsAt: window["resets_at"])
        }
        guard !metrics.isEmpty else { return nil }
        let plan: AiPlan
        switch subscriptionType?.lowercased() {
        case "pro": plan = .pro
        case "max": plan = .max
        case "enterprise": plan = .enterprise
        default: plan = .unknown
        }
        return AiUsageProviderSnapshot(provider: .claude, plan: plan, metrics: metrics)
    }

    public static func cursor(_ response: [String: Any], now: Date = Date())
        -> AiUsageProviderSnapshot? {
        guard let individual = dictionary(response["individualUsage"]),
              let overall = dictionary(individual["overall"]),
              (overall["enabled"] as? Bool) == true,
              let used = whole(overall["used"]), let limit = whole(overall["limit"]),
              limit > 0, used <= limit else { return nil }
        let remaining = whole(overall["remaining"]) ?? limit - used
        guard remaining <= limit else { return nil }
        let left = UInt8(min(100, (UInt64(remaining) * 100 + UInt64(limit / 2)) / UInt64(limit)))
        let (resetAt, remainingSeconds) = reset(response["billingCycleEnd"], now: now)
        let metric = AiUsageMetric(kind: .money, unit: .cents, used: used, limit: limit,
                                   remaining: remaining, remainingPercent: left,
                                   resetAt: resetAt, resetRemainingSeconds: remainingSeconds)
        return AiUsageProviderSnapshot(provider: .cursor,
                                       plan: plan(response["membershipType"] as? String),
                                       metrics: [metric])
    }
}
