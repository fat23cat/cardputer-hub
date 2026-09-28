import CompanionCore

protocol AiUsageProviderRefreshing: AnyObject {
    func refresh(_ done: @escaping (AiUsageProviderSnapshot?, Bool) -> Void)
    func setRecoveryHandler(_ handler: @escaping () -> Void)
    func stop()
}

extension AiUsageProviderRefreshing {
    func setRecoveryHandler(_ handler: @escaping () -> Void) {}
}
