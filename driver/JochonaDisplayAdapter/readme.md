# Jochona Display Adapter driver

UMDF2/IddCx indirect display driver implementing the Jochona Display
Adapter protocol v1.0 (see `../../docs/PROTOCOL.md`).

- `Driver.cpp`/`Driver.h` — IddCx adapter/monitor/swap-chain callbacks,
  adapted from `legacy/upstream-vdd` for a single stable default slot.
- `IoControl.cpp`/`IoControl.h` — `EvtIddCxDeviceIoControl`; the WDF
  boundary that marshals buffers into `jochona::protocol::Dispatch`
  (see `../../src/protocol`).
- `JochonaDisplayAdapter.inf` — UMDF2/IddCx driver install package,
  including the SYSTEM + Administrators device ACL (`HKR,,Security`
  under `JochonaDisplayAdapter_HardwareDeviceSettings`; WDF's
  `WdfDeviceInitAssignSDDLString` is KMDF-only, so UMDF drivers must set
  this via the INF instead of at runtime).
- `Trace.h` — WPP tracing configuration.

Build with `JochonaDisplayAdapter.sln` under a Windows Driver Kit
environment (see `../../docs/SIGNING.md` for test-signing setup). The
`src/protocol` sources are compiled directly into this project so the
same code that `tests/JochonaProtocolTests` exercises ships unmodified in
the driver binary.
