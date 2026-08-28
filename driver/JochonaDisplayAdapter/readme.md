# Jochona Display Adapter driver

UMDF2/IddCx indirect display driver implementing the Jochona Display
Adapter protocol v1.0 (see `../../docs/PROTOCOL.md`).

- `Driver.cpp`/`Driver.h` — IddCx adapter/monitor/swap-chain callbacks,
  adapted from `legacy/upstream-vdd` for a single stable default slot.
- `IoControl.cpp`/`IoControl.h` — `EvtIddCxDeviceIoControl`; the WDF
  boundary that marshals buffers into `jochona::protocol::Dispatch`
  (see `../../src/protocol`).
- `Acl.cpp`/`Acl.h` — SYSTEM + Administrators device ACL.
- `Trace.h` — WPP tracing configuration.
- `JochonaDisplayAdapter.inf` — UMDF2/IddCx driver install package.

Build with `JochonaDisplayAdapter.sln` under a Windows Driver Kit
environment (see `../../docs/SIGNING.md` for test-signing setup). The
`src/protocol` sources are compiled directly into this project so the
same code that `tests/JochonaProtocolTests` exercises ships unmodified in
the driver binary.
