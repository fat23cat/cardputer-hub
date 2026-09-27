import CompanionCore

protocol AiUsageProviderRefreshing: AnyObject {
    func refresh(_ done: @escaping (AiUsageProviderSnapshot?, Bool) -> Void)
    func stop()
}
