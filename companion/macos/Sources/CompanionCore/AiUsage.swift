import Foundation

public enum AiUsageState: UInt8 { case discovering = 1, ready = 2 }
public extension AiUsageState {
    static func forCache(pending: Int, hasUsableProvider: Bool) -> AiUsageState {
        pending == 0 || hasUsableProvider ? .ready : .discovering
    }
}
public enum AiProviderId: UInt8 { case codex = 1, cursor = 2 }
public enum AiPlan: UInt8 { case unknown = 0, plus = 1, business = 2, enterprise = 3 }
public enum AiFreshness: UInt8 { case fresh = 1, stale = 2 }
public enum AiMetricKind: UInt8 { case fiveHour = 1, week = 2, credits = 3, money = 4 }
public enum AiMetricUnit: UInt8 { case percent = 1, credits = 2, cents = 3 }

public struct AiUsageMetric: Equatable {
    public var kind: AiMetricKind
    public var unit: AiMetricUnit
    public var used: UInt32
    public var limit: UInt32
    public var remaining: UInt32
    public var remainingPercent: UInt8
    public var resetAt: UInt32
    public var resetRemainingSeconds: UInt32

    public init(kind: AiMetricKind, unit: AiMetricUnit, used: UInt32, limit: UInt32,
                remaining: UInt32, remainingPercent: UInt8, resetAt: UInt32,
                resetRemainingSeconds: UInt32) {
        self.kind = kind; self.unit = unit; self.used = used; self.limit = limit
        self.remaining = remaining; self.remainingPercent = remainingPercent
        self.resetAt = resetAt; self.resetRemainingSeconds = resetRemainingSeconds
    }
}

public struct AiUsageProviderSnapshot: Equatable {
    public var provider: AiProviderId
    public var plan: AiPlan
    public var freshness: AiFreshness
    public var metrics: [AiUsageMetric]

    public init(provider: AiProviderId, plan: AiPlan, freshness: AiFreshness = .fresh,
                metrics: [AiUsageMetric]) {
        self.provider = provider; self.plan = plan; self.freshness = freshness
        self.metrics = metrics
    }
}

public struct AiUsageSnapshot: Equatable {
    public var generation: UInt32
    public var state: AiUsageState
    public var providers: [AiUsageProviderSnapshot]

    public init(generation: UInt32 = 0, state: AiUsageState = .discovering,
                providers: [AiUsageProviderSnapshot] = []) {
        self.generation = generation; self.state = state; self.providers = providers
    }

    public func encode() -> [UInt8]? {
        guard providers.count <= 2, Set(providers.map(\.provider)).count == providers.count else { return nil }
        var bytes: [UInt8] = [1, state.rawValue, UInt8(providers.count)]
        Self.put(generation, into: &bytes)
        for provider in providers {
            guard (1...2).contains(provider.metrics.count) else { return nil }
            bytes += [provider.provider.rawValue, provider.plan.rawValue,
                      provider.freshness.rawValue, UInt8(provider.metrics.count)]
            for metric in provider.metrics {
                guard metric.remainingPercent <= 100,
                      metric.limit == 0 || (metric.used <= metric.limit && metric.remaining <= metric.limit)
                else { return nil }
                bytes += [metric.kind.rawValue, metric.unit.rawValue]
                Self.put(metric.used, into: &bytes)
                Self.put(metric.limit, into: &bytes)
                Self.put(metric.remaining, into: &bytes)
                bytes.append(metric.remainingPercent)
                Self.put(metric.resetAt, into: &bytes)
                Self.put(metric.resetRemainingSeconds, into: &bytes)
            }
        }
        return bytes.count <= CompanionConstants.maxPayloadSize ? bytes : nil
    }

    public static func decode(_ bytes: [UInt8]) -> AiUsageSnapshot? {
        guard bytes.count >= 7, bytes[0] == 1,
              let state = AiUsageState(rawValue: bytes[1]), bytes[2] <= 2
        else { return nil }
        var pos = 7
        var providers: [AiUsageProviderSnapshot] = []
        for _ in 0..<Int(bytes[2]) {
            guard pos + 4 <= bytes.count,
                  let provider = AiProviderId(rawValue: bytes[pos]),
                  let plan = AiPlan(rawValue: bytes[pos + 1]),
                  let freshness = AiFreshness(rawValue: bytes[pos + 2]),
                  (1...2).contains(bytes[pos + 3]),
                  !providers.contains(where: { $0.provider == provider })
            else { return nil }
            let count = Int(bytes[pos + 3]); pos += 4
            var metrics: [AiUsageMetric] = []
            for _ in 0..<count {
                guard pos + 23 <= bytes.count,
                      let kind = AiMetricKind(rawValue: bytes[pos]),
                      let unit = AiMetricUnit(rawValue: bytes[pos + 1])
                else { return nil }
                pos += 2
                let used = get(bytes, at: pos); pos += 4
                let limit = get(bytes, at: pos); pos += 4
                let remaining = get(bytes, at: pos); pos += 4
                let percent = bytes[pos]; pos += 1
                let resetAt = get(bytes, at: pos); pos += 4
                let resetRemaining = get(bytes, at: pos); pos += 4
                guard percent <= 100, limit == 0 || (used <= limit && remaining <= limit)
                else { return nil }
                metrics.append(AiUsageMetric(kind: kind, unit: unit, used: used, limit: limit,
                                             remaining: remaining, remainingPercent: percent,
                                             resetAt: resetAt, resetRemainingSeconds: resetRemaining))
            }
            providers.append(AiUsageProviderSnapshot(provider: provider, plan: plan,
                                                     freshness: freshness, metrics: metrics))
        }
        guard pos == bytes.count else { return nil }
        return AiUsageSnapshot(generation: get(bytes, at: 3), state: state, providers: providers)
    }

    private static func put(_ value: UInt32, into bytes: inout [UInt8]) {
        for shift in stride(from: 0, to: 32, by: 8) {
            bytes.append(UInt8(truncatingIfNeeded: value >> shift))
        }
    }
    private static func get(_ bytes: [UInt8], at index: Int) -> UInt32 {
        (0..<4).reduce(UInt32(0)) { $0 | (UInt32(bytes[index + $1]) << ($1 * 8)) }
    }
}

public protocol AiUsageCollecting: AnyObject {
    func snapshot() -> AiUsageSnapshot?
}
