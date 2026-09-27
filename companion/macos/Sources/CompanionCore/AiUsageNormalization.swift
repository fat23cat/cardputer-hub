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

    public static func codex(_ result: [String: Any], accountPlan: String? = nil,
                             now: Date = Date()) -> AiUsageProviderSnapshot? {
        let limits = dictionary(result["rateLimitsByLimitId"]).flatMap { dictionary($0["codex"]) }
            ?? dictionary(result["rateLimits"]) ?? result
        let detectedPlan = plan(limits["planType"] as? String ?? accountPlan)
        if let credits = dictionary(limits["individualLimit"]) ??
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
        return AiUsageProviderSnapshot(provider: .codex,
                                       plan: detectedPlan == .unknown ? .plus : detectedPlan,
                                       metrics: Array(metrics.prefix(2)))
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
