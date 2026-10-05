// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "CardputerCompanion",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "CardputerCompanion", targets: ["CardputerCompanion"]),
        .executable(name: "CompanionCoreCheck", targets: ["CompanionCoreCheck"]),
        .executable(name: "CompanionProvidersCheck", targets: ["CompanionProvidersCheck"]),
        .executable(name: "CardputerAgentHook", targets: ["CardputerAgentHook"]),
        .library(name: "CompanionCore", targets: ["CompanionCore"]),
        .library(name: "CompanionProviders", targets: ["CompanionProviders"]),
    ],
    targets: [
        // Agent-status hooks: Foundation only, because the hook helper runs on
        // every agent event and must start quickly.
        .target(
            name: "CompanionAgentHooks",
            path: "Sources/CompanionAgentHooks"
        ),
        .executableTarget(
            name: "CardputerAgentHook",
            dependencies: ["CompanionAgentHooks"],
            path: "Sources/CardputerAgentHook"
        ),
        .target(
            name: "CompanionCore",
            dependencies: ["CompanionAgentHooks"],
            path: "Sources/CompanionCore",
            linkerSettings: [
                .linkedFramework("AppKit"),
            ]
        ),
        .executableTarget(
            name: "CardputerCompanion",
            dependencies: ["CompanionCore", "CompanionProviders", "CompanionAgentHooks"],
            path: "Sources/App",
            exclude: ["Info.plist"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("CoreBluetooth"),
                .linkedFramework("CoreWLAN"),
                .linkedFramework("IOKit"),
                .linkedFramework("ServiceManagement"),
                .linkedFramework("SystemConfiguration"),
            ]
        ),
        .executableTarget(
            name: "CompanionCoreCheck",
            dependencies: ["CompanionCore", "CompanionAgentHooks"],
            path: "Sources/CompanionCoreCheck"
        ),
        .target(
            name: "CompanionProviders",
            dependencies: ["CompanionCore"],
            path: "Sources/CompanionProviders",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("Security"),
            ]
        ),
        .executableTarget(
            name: "CompanionProvidersCheck",
            dependencies: ["CompanionProviders", "CompanionCore"],
            path: "Sources/CompanionProvidersCheck"
        ),
    ]
)
