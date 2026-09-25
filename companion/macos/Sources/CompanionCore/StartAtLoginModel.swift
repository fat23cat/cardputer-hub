public protocol LoginRegistration {
    var isEnabled: Bool { get }
    func setEnabled(_ enabled: Bool) throws
}

public final class StartAtLoginModel {
    private let service: LoginRegistration
    private var unresolvedDesired: Bool?
    public private(set) var enabled: Bool
    public private(set) var hasError = false

    public init(service: LoginRegistration) {
        self.service = service
        enabled = service.isEnabled
    }

    public func refresh() {
        enabled = service.isEnabled
        if let unresolvedDesired, enabled == unresolvedDesired {
            hasError = false
            self.unresolvedDesired = nil
        }
    }

    public func setEnabled(_ desired: Bool) {
        var failed = false
        do {
            try service.setEnabled(desired)
        } catch {
            failed = true
        }
        enabled = service.isEnabled
        hasError = failed || enabled != desired
        unresolvedDesired = hasError ? desired : nil
    }
}
