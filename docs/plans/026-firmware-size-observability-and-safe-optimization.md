# 026 — Firmware Size Observability and Safe Optimization

Status: **Complete**

## 0. Current Closeout Status

Software implementation and clean size measurement completed on **2026-09-15**.

Delivered:

* production size optimization in `sdkconfig.defaults`;
* explicit exclusion of Wi-Fi SoftAP and Enterprise support;
* `make firmware-size` for application, partition, and component reports;
* CI retention of both size reports without a hard budget;
* release-build size output;
* repository-level protection of the selected settings and deferred features;
* firmware-side validation of the generated effective ESP-IDF configuration;
* clean, independent A/B measurements recorded in section 13.

Final software evidence: the complete host gate passed with 72 Python tests,
274 native test cases, architecture checks, formatting, and static analysis;
the final clean ESP-IDF 5.5.5 production build passed; and
`make firmware-size` completed against that image.

The final image was staged through CRUB and the operator confirmed the focused
physical Wi-Fi/BLE smoke test passed on **2026-09-15**.

## 1. Goal

Establish a reproducible firmware-size baseline and apply a small set of low-risk build-time optimizations after Wi-Fi became part of the normal runtime in Plan 025.

The firmware should:

* report application image size in a reproducible way;
* make major component-level size contributions visible;
* optimize the production application for binary size;
* exclude Wi-Fi functionality that is outside the approved product scope and not used by the runtime;
* preserve all existing application behavior, architecture boundaries, diagnostics, security properties, and connectivity behavior;
* provide enough size observability to identify future regressions before they become a flash-layout problem.

This plan is intentionally not a general firmware-size refactor.

The desired progression is:

```text
Plan 025
Wi-Fi becomes reachable in production
        ↓
large one-time ESP-IDF Wi-Fi/TCP-IP cost becomes visible
        ↓
Plan 026
measure the production image
        ↓
apply safe configuration-level reductions
        ↓
establish a new production baseline
        ↓
continue Phase 3 / Phase 4 product work
```

---

## 2. Current Problem

Before Plan 025, the ESP32 Wi-Fi adapter and Wi-Fi service existed but were not composed by normal firmware.

The linker could therefore discard most of the Wi-Fi runtime.

Plan 025 changed normal composition to include:

```text
NetworkService
        ↓
connectivity::WiFiService
        ↓
Esp32WifiAdapter
        ↓
ESP-IDF Wi-Fi / esp_netif / TCP-IP stack
```

As a result, application image size increased substantially.

The growth is expected because the production image now contains functionality that was previously unreachable.

The relevant comparison observed after Plan 025 was approximately:

```text
before runtime Wi-Fi:
~843 KB

after runtime Wi-Fi:
~1.31 MB

growth:
~469 KB
```

Only a small fraction of this increase is project-owned `NetworkService` and adapter logic.

Most of the increase comes from ESP-IDF networking dependencies that are now linked into the application.

The problem is therefore not:

```text
NetworkService is too large
```

The actual engineering problem is:

```text
the project has crossed the point where firmware size
must become observable and intentionally managed
```

---

## 3. Scope

Included:

* clean production firmware size baseline;
* application binary size reporting;
* ESP-IDF component-level size reporting;
* reproducible developer command for inspecting firmware size;
* CI/release visibility of firmware size;
* `CONFIG_COMPILER_OPTIMIZATION_SIZE`;
* disabling unused Wi-Fi SoftAP support;
* disabling unused Wi-Fi Enterprise support;
* clean A/B measurement for every optimization;
* preservation of current Wi-Fi station behavior;
* preservation of BLE/HID behavior;
* automated build-configuration checks;
* production firmware build verification;
* focused physical smoke testing;
* documentation of the resulting baseline.

Not included:

* application architecture changes;
* changes to `NetworkService`;
* changes to `connectivity::WiFiService`;
* changes to `Esp32WifiAdapter` behavior unless required to preserve compilation;
* replacing `std::string` or other C++ abstractions for size reasons;
* manual function-level micro-optimization;
* asset compression or redesign;
* partition-table changes;
* removal of OTA slots;
* removal of diagnostics;
* lowering application log levels;
* disabling assertions;
* disabling IPv6;
* disabling WPA3;
* disabling WPA3 SAE/H2E support;
* mbedTLS trimming;
* BLE/NimBLE feature reduction beyond existing configuration;
* link-time optimization;
* custom linker scripts;
* section placement optimization;
* component splitting solely to influence linking;
* moving resources to microSD/SPIFFS;
* changing the Wi-Fi product scope;
* introducing a hard CI firmware-size budget.

Those may be evaluated later if flash pressure becomes material.

---

## 4. Guiding Principle

Optimization must remain evidence-driven.

Do not change source architecture merely because the binary became larger when Wi-Fi was activated.

The order must be:

```text
measure
    ↓
identify
    ↓
change one configuration
    ↓
clean rebuild
    ↓
measure again
    ↓
validate behavior
    ↓
keep or revert
```

Do not combine several unmeasured size optimizations and then infer their individual effect from the final binary.

---

## 5. Baseline

Before changing production configuration, create a clean baseline from the Plan-025 production image.

The baseline must come from:

```text
ESP-IDF 5.5.5
ESP32-S3 target
normal production firmware
validation harnesses disabled
current partitions.csv
current sdkconfig.defaults
clean build directory
```

Record at minimum:

```text
cardputer_hub.bin size
total application image usage
remaining app-partition capacity
per-component flash contribution
```

Use ESP-IDF's size tooling rather than only filesystem size.

The equivalent inspection should include:

```bash
idf.py -B build size
idf.py -B build size-components
```

`size-files` may be used during investigation when a component requires deeper inspection:

```bash
idf.py -B build size-files
```

It does not need to become part of normal CI output.

The baseline report should make large networking components visible instead of only reporting:

```text
cardputer_hub.bin = N bytes
```

---

## 6. Developer Size Command

Add one documented project-level entry point for firmware-size inspection.

Preferred interface:

```bash
make firmware-size
```

The command should operate on a completed production build and print at least:

```text
application image size
partition usage
component-level contribution
```

It may internally call:

```text
idf.py size
idf.py size-components
```

Do not create a second firmware build configuration.

Production size reporting must inspect the same artifact produced by the normal ESP-IDF production build.

The intended workflow is:

```bash
make build
make firmware-size
```

or an equivalent single command if implementation makes that cleaner.

---

## 7. CI / Release Observability

Firmware size should become visible in automated production builds.

At minimum, successful firmware CI should retain or print:

```text
final application image size
ESP-IDF size summary
component-level size summary
```

A text artifact is acceptable, for example:

```text
firmware-size.txt
firmware-size-components.txt
```

Exact artifact names are implementation details.

This plan must **not** introduce a hard size-regression gate such as:

```text
fail if firmware > 1.4 MB
fail if firmware grows by >5%
```

There is not enough historical data yet to define a meaningful budget.

Plan 026 establishes observability first.

A later plan may introduce a firmware-size budget once normal growth patterns from Mini Apps and network Services are understood.

---

## 8. Experiment A — Optimize Application for Size

Evaluate:

```text
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
```

The production application should use ESP-IDF's supported size optimization mode instead of debug-oriented application optimization.

Perform a clean A/B comparison:

```text
A:
current production configuration

B:
same revision
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
```

For both builds record:

```text
cardputer_hub.bin
total flash usage
component size summary
```

Do not infer the result from an incremental build.

The build directory must be fully reconfigured or rebuilt cleanly so stale compiler flags cannot influence the comparison.

### Acceptance for Experiment A

Keep size optimization if:

* the application binary becomes smaller;
* all host checks pass;
* production firmware compiles;
* startup remains correct;
* display/input behavior remains correct;
* BLE host restoration and connection remain correct;
* Wi-Fi station connection remains correct;
* normal diagnostics remain available.

If optimized compilation exposes undefined behavior or functional regressions, fix the underlying valid defect only if it is clearly bounded.

Do not broaden this plan into unrelated refactoring.

---

## 9. Experiment B — Disable Wi-Fi SoftAP Support

The current architecture uses Wi-Fi as a station:

```text
Cardputer
    ↓
existing access point
```

The approved current scope does not require the Cardputer itself to operate as an access point.

Evaluate explicit production configuration equivalent to:

```text
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n
```

Perform a clean A/B comparison against the accepted result of Experiment A.

Record the exact binary delta.

### Required behavior after disabling SoftAP support

The following must remain unchanged:

```text
station initialization
open-network connection
WPA/WPA2 personal connection
DHCP/IP acquisition
disconnect
retry/backoff
RSSI reporting
Wi-Fi + BLE coexistence
```

No application code should depend on SoftAP APIs.

### Acceptance for Experiment B

Keep the option if:

* production firmware builds cleanly;
* the image becomes smaller;
* all existing automated checks pass;
* normal station behavior remains unchanged.

---

## 10. Experiment C — Disable Wi-Fi Enterprise Support

Enterprise Wi-Fi is explicitly outside the currently approved Wi-Fi scope.

Evaluate:

```text
CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT=n
```

Perform another clean A/B comparison.

This change must not alter support for the currently required network classes:

```text
open personal network
WPA/WPA2 personal network
currently supported personal security modes
```

### Acceptance for Experiment C

Keep the option if:

* production firmware builds;
* the image becomes smaller;
* station functionality remains correct;
* existing Wi-Fi tests pass;
* physical personal-network connection succeeds.

---

## 11. Optimizations Explicitly Deferred

### 11.1 WPA3

Do not disable:

```text
CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
CONFIG_ESP_WIFI_ENABLE_SAE_H2E
```

in this plan.

Removing them changes network compatibility and therefore represents a product capability decision, not merely build cleanup.

A later size review may evaluate WPA3 only if the supported Wi-Fi security contract is explicitly defined.

---

### 11.2 IPv6

Do not disable IPv6 solely for firmware size.

Later phases introduce:

```text
WeatherService
RemoteControlService
VpsService
Home Assistant
other network integrations
```

Their networking requirements are not yet known.

Changing the product to IPv4-only now would be premature.

---

### 11.3 Link-Time Optimization

Do not enable LTO in Plan 026.

LTO may provide additional reduction, but it changes build/link characteristics and makes the experiment materially broader.

Evaluate it later only if:

```text
size pressure remains meaningful
        AND
safe configuration reductions are exhausted
```

---

### 11.4 Logging

Do not reduce normal project diagnostics merely to save flash.

Current informational diagnostics are useful while connectivity and later Services are still being developed.

Identity-bearing Wi-Fi and Bluetooth logs already have their own security-specific suppression.

A future release-profile design may revisit compile-time logs independently.

---

### 11.5 Assertions

Do not silence or disable ESP-IDF assertions in this plan.

The project is still in active platform development and failure diagnostics are more valuable than the expected small code-size reduction.

---

### 11.6 Source-Level Micro-Optimization

Do not rewrite project code for individual kilobytes.

Examples specifically outside this plan:

```text
replace std::string
replace optional
collapse Services
merge abstractions
remove domain snapshots
inline architectural boundaries
hand-pack runtime objects
convert normal C++ code to C
```

These changes would trade maintainability for size before flash capacity requires it.

---

## 12. Build Configuration Ownership

Accepted optimization settings belong in the reproducible production configuration:

```text
sdkconfig.defaults
```

Do not rely on a developer-local generated `sdkconfig`.

The repository must continue to produce the same optimization profile from a clean checkout.

Extend existing build-configuration tests so they verify the selected invariants.

At minimum, tests should protect:

```text
size optimization enabled
SoftAP support disabled
Enterprise Wi-Fi support disabled
existing BLE configuration preserved
existing USB Serial/JTAG diagnostics preserved
8 MB flash configuration preserved
custom partition table preserved
```

The exact Python assertions may follow the existing `test_esp_idf_build_config.py` structure.

---

## 13. Measurement Record

The accepted experiments were rebuilt independently with ESP-IDF 5.5.5, the
ESP32-S3 target, a separate clean build directory and a separate generated
`sdkconfig` derived from the version-controlled defaults. Every comparison used
the same `CARDPUTER_HUB_VERSION_OVERRIDE=plan026-measurement`; validation
harnesses were disabled and compiler caching was unavailable. `Image size` is
the exact `cardputer_hub.bin` byte count. `ESP-IDF total` is the unpadded image
usage reported by `idf.py size`.

| Configuration                         | Image size | Delta from prior | ESP-IDF total | Result   |
| ------------------------------------- | ---------: | ---------------: | ------------: | -------- |
| Plan 025 baseline                     |  1,312,464 |                — |     1,312,341 | baseline |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` |  1,194,400 |         -118,064 |     1,194,283 | kept     |
| SoftAP disabled                       |  1,146,592 |          -47,808 |     1,146,471 | kept     |
| Enterprise disabled / final Plan 026  |  1,146,080 |             -512 |     1,145,959 | kept     |

The final image is **166,384 bytes smaller** than the Plan-025 baseline. The
unchanged smallest application partition is 3,342,336 bytes; the final binary
leaves 2,196,256 bytes free (66% as rounded by ESP-IDF), compared with
2,029,872 bytes free at baseline.

Largest flash contributors before and after the accepted configuration changes:

| Archive                    | Baseline |   Final |  Delta |
| -------------------------- | -------: | ------: | -----: |
| `libnet80211.a`            |  136,344 | 107,213 | -29,131 |
| `libmain.a`                |  118,528 | 102,492 | -16,036 |
| `libbtdm_app.a`            |   93,759 |  87,186 |  -6,573 |
| `libbt.a`                  |   85,639 |  74,416 | -11,223 |
| `liblwip.a`                |   82,786 |  69,208 | -13,578 |
| `libm5stack__m5gfx.a`      |   80,379 |  68,509 | -11,870 |
| `libwpa_supplicant.a`      |   67,620 |  39,739 | -27,881 |
| `libmbedcrypto.a`          |   62,126 |  55,583 |  -6,543 |

The final `make firmware-size` run produced both
`firmware-size.txt` and `firmware-size-components.txt` from the same completed
production artifact.

The purpose is to make future questions answerable:

```text
Why did firmware grow?
Which subsystem grew?
Did the growth come from our code or ESP-IDF?
How much did a build option actually save?
```

without manual reverse engineering.

---

## 14. Physical Smoke Test

Because Plan 026 changes compiler optimization and Wi-Fi build configuration, perform a focused hardware smoke test after automated checks pass.

The test does not need to repeat the complete Phase-2 physical acceptance matrix.

Required checks:

```text
boot normal firmware
        ↓
Home / Settings still usable
        ↓
BLE starts/restores normally
        ↓
connect to configured personal Wi-Fi
        ↓
obtain IP address
        ↓
report Connected state and RSSI
        ↓
disconnect or temporarily remove AP
        ↓
observe existing retry behavior
        ↓
restore AP
        ↓
observe reconnection
        ↓
confirm BLE host remains usable
```

If convenient, verify one open network as well.

No SoftAP or Enterprise Wi-Fi test is required because those capabilities are intentionally excluded.

### 2026-09-15 Physical Status

Passed on the final Plan-026 image after staging it through Cardputer Firmware
Manager and installing it with CRUB. The operator confirmed normal firmware
operation, including the focused Wi-Fi/BLE smoke sequence above.

---

## 15. Regression Safety

Plan 026 must not change any public Service contract.

The following boundaries remain unchanged:

```text
Apps / System UI
        ↓
NetworkService
        ↓
connectivity::WiFiService
        ↓
IWifiAdapter
        ↓
Esp32WifiAdapter
```

No new dependency exception should be added.

The architecture checker must continue to pass.

Plan 026 must also preserve:

```text
ConfigurationService schema version 4
Wi-Fi credential persistence
credential secrecy
BLE pairing/bond persistence
HostService behavior
ActionBus behavior
UI scheduling
audio behavior
microSD behavior
OTA partition layout
```

---

## 16. Acceptance Criteria

Plan 026 is complete when:

* a clean Plan-025-equivalent firmware-size baseline is recorded;
* firmware size can be inspected through one documented project command;
* ESP-IDF application and component size summaries are available;
* production CI exposes firmware-size information;
* CI does not yet impose an arbitrary hard size budget;
* `CONFIG_COMPILER_OPTIMIZATION_SIZE` has been independently measured;
* the size optimization is retained only if production behavior remains correct;
* SoftAP support has been independently measured and disabled if safe;
* Enterprise Wi-Fi support has been independently measured and disabled if safe;
* every accepted optimization has an exact recorded binary delta;
* `sdkconfig.defaults` owns all accepted production settings;
* build-configuration tests protect the selected settings;
* no application architecture or public Service API was changed for size reasons;
* no partition layout was changed;
* IPv6 remains unchanged;
* WPA3 support remains unchanged;
* LTO remains unchanged;
* logging and assertions remain unchanged;
* host checks pass;
* architecture checks pass;
* static analysis passes;
* production ESP-IDF 5.5.5 firmware builds successfully;
* focused Wi-Fi/BLE hardware smoke testing passes;
* the final production image size and component summary are recorded in this plan.

---

## 17. Follow-Up

After Plan 026, return to product roadmap work.

The next firmware-size review should be triggered by evidence, not by schedule.

Reasonable triggers include:

```text
application partition usage approaches an agreed threshold

a single plan unexpectedly adds a large amount of flash

TLS/network Services materially increase the image

Mini App infrastructure changes linking behavior

OTA/partition requirements change
```

Potential later optimization topics include:

```text
LTO
logging profiles
WPA3 capability review
IPv6 capability review
TLS/mbedTLS configuration
asset storage/compression
component-level dead-code analysis
partition-layout review
```

Those should remain separate decisions with their own measurements and product trade-offs.

---

## 18. Intended Outcome

Plan 026 is successful even if it does not recover most of the approximately 469 KB added when Wi-Fi entered the production runtime.

That increase represents real functionality.

The objective is instead:

```text
understand the image
        +
remove clearly unused framework features
        +
use an appropriate production optimization level
        +
make future growth visible
```

The expected long-term benefit is not only a smaller firmware image.

It is the ability to detect and explain firmware growth before flash capacity becomes a constraint.
