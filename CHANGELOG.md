# Changelog

## 1.0.0

Everything in this release is new relative to the
[VirtualDrivers/Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
fork point (preserved as `legacy/upstream-vdd/`); the baseline IddCx
driver-callback architecture itself is unchanged. See
[`NOTICE.md`](NOTICE.md) for the full provenance map.

### Added

- **Jochona Display Adapter protocol v1.0** — a narrow, versioned IOCTL
  surface (`GET_PROTOCOL_VERSION`, `ENUMERATE_SLOTS`, `LEASE_SLOT`,
  `CONFIGURE_SLOT`, `RELEASE_SLOT`, `SET_RENDER_ADAPTER_LUID`,
  `GET_WATCHDOG`, `WATCHDOG_PING`), replacing the upstream driver's
  `vdd_settings.xml` file, registry path, and named-pipe/PowerShell
  control scheme that required a device uninstall/reinstall or
  `RELOAD_DRIVER` cycle for most changes. See
  [`docs/PROTOCOL.md`](docs/PROTOCOL.md).
- **Lease/watchdog/ACL model**: one stable default monitor slot
  (`JOCHONA_PROTOCOL_V1_MAX_SLOTS = 1`) guarded by opaque GUID lease
  tokens, restore-on-release to a fixed baseline mode
  (1920x1080@60, 8bpc, SDR), a WDF periodic timer that reaps leases
  silent past a watchdog timeout, and device access restricted to
  `SYSTEM` and `Administrators` via the driver INF's `HKR,,Security`
  entry (UMDF drivers cannot assign this at runtime).
- **Portable protocol implementation and test suite**: `src/protocol/`
  (`SlotStateMachine`, `ProtocolDispatcher`) has no WDF/IddCx dependency
  and compiles unmodified into the driver binary; `tests/` builds and
  runs independently of the WDK (CMake + doctest + ctest), covering the
  full slot state-transition surface and adversarial malformed-buffer
  handling at the IOCTL dispatch layer.
- **x64/ARM64 CI build and release pipeline**: `.github/workflows/ci-validation.yml`
  runs the portable protocol tests plus an MSVC/WDK driver build for
  both architectures on every push/PR; `.github/workflows/release.yml`
  builds tagged (`v*`) or manually dispatched releases for both
  architectures, packages per-architecture zips (driver DLL, stamped
  `.inf`, unsigned `.cat`, with `docs/driver-install-snippet.md` bundled
  as `INSTALL.md`) alongside a `SHA256SUMS` file, and publishes them as
  an unsigned GitHub Release on tag pushes. See
  [`docs/SIGNING.md`](docs/SIGNING.md) for current signing status
  (local test-signing only; SignPath Foundation enrollment not yet
  complete).
