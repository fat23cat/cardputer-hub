// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "CardputerCompanion",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "CardputerCompanion", targets: ["CardputerCompanion"]),
        .executable(name: "CompanionCoreCheck", targets: ["CompanionCoreCheck"]),
        .executable(name: "CompanionProvidersCheck", targets: ["CompanionProvidersCheck"]),
        .library(name: "CompanionCore", targets: ["CompanionCore"]),
        .library(name: "CompanionProviders", targets: ["CompanionProviders"]),
    ],
    targets: [
        .target(
            name: "CompanionCore",
            path: "Sources/CompanionCore",
            linkerSettings: [
                .linkedFramework("AppKit"),
            ]
        ),
        .executableTarget(
            name: "CardputerCompanion",
            dependencies: ["CompanionCore", "CompanionProviders"],
            path: "Sources/App",
            exclude: ["Info.plist"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("CoreBluetooth"),
                .linkedFramework("IOKit"),
                .linkedFramework("ServiceManagement"),
                .linkedFramework("SystemConfiguration"),
            ]
        ),
        .executableTarget(
            name: "CompanionCoreCheck",
            dependencies: ["CompanionCore"],
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
