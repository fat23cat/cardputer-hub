# Native USB HID Transport — Cancelled

Status: **Removed from the current product scope on 2026-09-11**

The user cancelled USB host control. Its implementation, descriptors, TinyUSB
dependencies, tests, and validation harness have been removed. The original
implementation remains in Git history at `6547070`; this file preserves the
plan number and existing references, not a requirement to rebuild it.

USB now provides power, firmware installation, and fixed ESP32-S3 USB
Serial/JTAG diagnostics. BLE is the only implemented host-control transport.
Future transports can implement the existing `IHidTransport` boundary under
a new approved plan. They are not a Phase 2 completion gate.

See [the BLE-only Phase 2 closeout](017-hid-transport-arbitration.md) and the
[architecture](../ARCHITECTURE.md#10-connectivity-layer) for current scope.
