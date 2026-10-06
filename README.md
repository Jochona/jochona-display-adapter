# Jochona Display Adapter

A UMDF2/IddCx indirect display driver for Windows that exposes one stable
virtual monitor slot to [Jochona Host](https://github.com/Jochona/jochona-host)
over a small, versioned IOCTL protocol — lease it, configure its
mode/HDR, release it, and Windows sees a real display the whole time.

Forked from [VirtualDrivers/Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
(MIT), itself derived from Microsoft's
[IndirectDisplay sample driver](https://github.com/microsoft/Windows-driver-samples/tree/master/video/IndirectDisplay)
(MS-PL). Full pre-fork history and both upstream licenses are preserved —
see [`NOTICE.md`](NOTICE.md) for the complete provenance map and license
text, and [`legacy/upstream-vdd`](legacy/upstream-vdd) for the untouched
pre-fork source.

For an end-to-end Windows Host + Bazzite Client setup that puts this
driver in context, see the
[constellation repo's getting-started walkthrough](https://github.com/Jochona/jochona-constellation#getting-started-windows-host--bazzite-client).

## What changed from upstream

The upstream driver is controlled by a `vdd_settings.xml` file, a
registry path, and a named pipe driven by PowerShell scripts — most
changes require a device uninstall/reinstall or a `RELOAD_DRIVER` cycle.
Jochona Display Adapter replaces all of that with **Jochona Display
Adapter protocol v1.0**: a narrow, versioned IOCTL surface
(`GET_PROTOCOL_VERSION`, `ENUMERATE_SLOTS`, `LEASE_SLOT`,
`CONFIGURE_SLOT`, `RELEASE_SLOT`, `SET_RENDER_ADAPTER_LUID`,
`GET_WATCHDOG`, `WATCHDOG_PING`) against one stable default monitor slot
with opaque GUID lease tokens, restore-on-release, a watchdog that reaps
orphaned leases, and a device ACL restricted to `SYSTEM` and
`Administrators` — see [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

Device interface GUID: `b98f1cbd-eaec-5d98-82ab-e899d2643fa1` — this and
the protocol version are shared byte-for-byte with Jochona Host via
[`include/jochona/display_adapter_abi.h`](include/jochona/display_adapter_abi.h),
which Host may duplicate verbatim with provenance.

## Repository layout

```
include/jochona/        Canonical C-compatible ABI header (protocol structs, IOCTL codes, GUID)
src/protocol/            Portable protocol/state-machine implementation (no WDF/IddCx dependency)
tests/                    Unit tests for src/protocol (doctest; build with CMake, any platform)
driver/JochonaDisplayAdapter/   The UMDF2/IddCx driver itself (Windows/WDK only)
docs/                     Protocol reference and driver-signing instructions
legacy/upstream-vdd/      Pre-fork upstream source, kept for provenance/history; not built
Common/, ThirdParty/      Unchanged upstream build dependencies (LUID helper, WDF headers submodule)
```

## Building

Requires a Windows Driver Kit environment (see
`.github/workflows/ci-validation.yml` for the exact toolchain: MSVC via
`microsoft/setup-msbuild`, `windowsdriverkit11` via Chocolatey). Clone
with submodules for the Windows-Driver-Frameworks headers:

```bash
git clone --recurse-submodules https://github.com/Jochona/jochona-display-adapter.git
```

Then open `driver/JochonaDisplayAdapter/JochonaDisplayAdapter.sln` (x64
or ARM64, Debug or Release) in Visual Studio, or build headlessly with
`msbuild`. The `src/protocol` sources compile directly into the driver
binary — the same code `tests/JochonaProtocolTests` exercises ships
unmodified.

For local test-signing and the SignPath-based release signing plan, see
[`docs/SIGNING.md`](docs/SIGNING.md). The CI/release pipeline itself
never signs anything or installs a certificate; the `sign-and-install.ps1`
script shipped in each release zip does, but only on the machine that
runs it and only when a user explicitly chooses to.

Tagged releases (`v*`) publish **unsigned, developer/test-build**
per-architecture zips via `.github/workflows/release.yml` on the
[Releases page](https://github.com/Jochona/jochona-display-adapter/releases),
each containing the driver, `INSTALL.md`, and `sign-and-install.ps1`/
`uninstall.ps1` to self-sign and install locally — read
[`docs/SIGNING.md`](docs/SIGNING.md) before installing one; these
releases are marked pre-release and are not production-ready (no WHQL
or SignPath signature yet).

## Testing

`tests/` builds and runs independently of the WDK (portable C++17, no
Windows dependency):

```bash
cmake -S tests -B build
cmake --build build
ctest --test-dir build
```

Covers the full `SlotStateMachine` state-transition surface (lease →
configure → release, busy/mismatch/not-found rejections, watchdog
orphan reaping) and adversarial malformed-buffer handling at the IOCTL
dispatch layer.

## License

MIT for Jochona-authored code, **MIT AND MS-PL** for the portions
carried forward from the upstream IndirectDisplay sample driver lineage.
See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md). See
[`CHANGELOG.md`](CHANGELOG.md) for release history.

## Credits

This project would not exist without the upstream Virtual Display Driver
team and Microsoft's original sample driver authors — full credits in
[`NOTICE.md`](NOTICE.md).
