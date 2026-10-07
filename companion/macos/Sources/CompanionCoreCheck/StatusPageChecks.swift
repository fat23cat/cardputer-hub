import CompanionCore
import Foundation

final class FakeStatusPages: StatusPageFetching {
    var requested: [URL] = []
    var pending: [(StatusPageReport?) -> Void] = []
    func fetch(_ url: URL, completion: @escaping (StatusPageReport?) -> Void) {
        requested.append(url)
        pending.append(completion)
    }
}

/// Plan 049 checks: wire format, the Statuspage parser, and the session's
/// asynchronous answer.
func statusPageChecks(_ expect: (Bool, String) -> Void, fixture: (String) -> [UInt8],
                      helloAck: (UInt16) -> [UInt8]) {
    let github = "https://www.githubstatus.com/api/v2/status.json"
    var request = CompanionEnvelope()
    request.kind = .request
    request.session = 42
    request.requestId = 14
    request.operation = .serviceStatus
    request.payload = ServiceStatusWire.request(github) ?? []
    expect(CompanionCodec.encode(request) == fixture("service-status-request.bin"),
           "service status request fixture")
    expect(ServiceStatusWire.request("http://www.githubstatus.com/") == nil, "https only")
    expect(ServiceStatusWire.request("https://") == nil, "a host is required")

    var response = CompanionEnvelope()
    response.kind = .response
    response.session = 42
    response.requestId = 14
    response.operation = .serviceStatus
    response.payload = StatusPageReport(level: .minor, description: "Partially Degraded Service").encode()
    expect(CompanionCodec.encode(response) == fixture("service-status-response.bin"),
           "service status response fixture")
    var unavailable = response
    unavailable.status = .notAvailable
    unavailable.payload = []
    expect(CompanionCodec.encode(unavailable) == fixture("service-status-not-available.bin"),
           "service status failure fixture")
    expect(StatusPageReport.decode([0, 0]) == nil, "unknown is never sent")
    expect(StatusPageReport.decode([6, 0]) == nil, "no such level")
    expect(StatusPageReport.decode([3, 4, 0x41]) == nil, "length disagrees with payload")

    // Parser: the same rules as the firmware's parseStatusPage().
    func parse(_ text: String) -> StatusPageReport? { StatusPageReport.parse(Data(text.utf8)) }
    expect(parse(#"{"page":{"name":"GitHub"},"status":{"indicator":"none","description":"All Systems Operational"}}"#) ==
           StatusPageReport(level: .operational, description: "All Systems Operational"),
           "statusPage none is operational")
    for (indicator, level) in [("minor", ServiceStatusLevel.minor), ("major", .major),
                               ("critical", .critical), ("maintenance", .maintenance)] {
        expect(parse(#"{"status":{"indicator":"\#(indicator)"}}"#)?.level == level,
               "statusPage \(indicator)")
    }
    expect(parse(#"{"status":{"indicator":"purple"}}"#) == nil, "unknown indicator")
    expect(parse("<html>Bad gateway</html>") == nil, "not JSON")
    expect(parse(#"{"page":{"indicator":"none"},"status":{}}"#) == nil,
           "indicator outside the status object")
    let long = parse(#"{"status":{"indicator":"minor","description":"\#(String(repeating: "x", count: 47))état"}}"#)
    expect(long?.description.utf8.count == 47, "description cut at a character boundary")

    // Session: the page is fetched asynchronously and answered for its session only.
    let pages = FakeStatusPages()
    let session = CompanionSession(applications: FakeApplications(), statusPages: pages)
    var sent: [[UInt8]] = []
    session.outgoing = { sent.append($0) }
    session.startHandshake()
    session.handle(helloAck(42))
    sent.removeAll()
    expect(session.handle(CompanionCodec.encode(request)!), "service status request accepted")
    expect(pages.requested == [URL(string: github)!], "the named page is fetched")
    expect(sent.isEmpty, "no answer before the fetch completes")
    pages.pending.removeFirst()(StatusPageReport(level: .minor, description: "Partially Degraded Service"))
    expect(sent.last == fixture("service-status-response.bin"), "fetched page is answered")
    session.handle(CompanionCodec.encode(request)!)
    pages.pending.removeFirst()(nil)
    expect(sent.last == fixture("service-status-not-available.bin"), "failed fetch is not available")

    session.handle(CompanionCodec.encode(request)!)
    let count = sent.count
    session.reset()
    pages.pending.removeFirst()(StatusPageReport(level: .major, description: ""))
    expect(sent.count == count, "an answer for an ended session is dropped")

    var plain = request
    plain.payload = [UInt8(github.utf8.count - 1)] + Array(github.replacingOccurrences(of: "https", with: "http").utf8)
    expect(CompanionCodec.encode(plain) == nil, "a plain http request is malformed")
}
