# Driver signing

The Jochona Display Adapter is a kernel-adjacent UMDF component: Windows
will not load it at all unless it is signed appropriately for the target
machine's signing policy. This document covers **local development
signing only** and the **release signing configuration this repository
is prepared for**. It does **not** claim SignPath (or any other CA/CI
signing service) approval has been granted — that is a separate,
external step the repository owner must complete with SignPath directly.

## Development test signing

Test signing is scoped to the developer's own machine and is reversible.
It never touches another machine's trust store. **The CI/release build
pipeline** (`.github/workflows/ci-validation.yml`,
`.github/workflows/release.yml`) **never installs a self-signed root
certificate anywhere** — it only compiles and packages an unsigned
`.dll`/`.cat`, nothing more (see "What this repository will never do"
below). What *is* shipped in each release zip is `sign-and-install.ps1`,
a script that performs the manual steps below for you, **on the machine
that runs it, only when you choose to run it** — it is never invoked by
CI or by any part of the release pipeline itself.

1. **Enable test signing mode** (elevated PowerShell, reboot required):

   ```powershell
   bcdedit /set testsigning on
   Restart-Computer
   ```

   Confirm it took effect: `bcdedit /enum | Select-String testsigning`
   should show `Yes`. A watermark appears in the desktop corner while
   test signing is on — this is expected and is Windows' own signal that
   the machine is running in a reduced-trust mode; turn it back off
   (`bcdedit /set testsigning off` + reboot) when you're done developing.

   If this fails or the watermark doesn't stick, Secure Boot must be
   disabled in firmware first — it blocks test-signing mode on many OEM
   boards.

2. **Build** `driver/JochonaDisplayAdapter/JochonaDisplayAdapter.sln`
   (Release or Debug, x64 or ARM64) under a Windows Driver Kit
   environment matching `.github/workflows/ci-validation.yml`. (Skip
   this step if you downloaded a release zip — it already contains the
   built `.dll`/`.inf`/`.cat`.)

3. **Self-sign the build output** with a certificate scoped to *this
   machine's* certificate store — never distributed, never checked into
   the repository:

   ```powershell
   # One-time: create a local test certificate.
   $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=Jochona Display Adapter Test" `
       -KeyUsage DigitalSignature -FriendlyName "Jochona Display Adapter Test Signing" `
       -CertStoreLocation "Cert:\LocalMachine\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")

   # Trust it locally so *this* machine's driver-load policy accepts it —
   # scoped to LocalMachine\Root and LocalMachine\TrustedPublisher ON
   # THIS MACHINE ONLY. Remove it again with uninstall.ps1 (or by hand)
   # when you're done; see below.
   Export-Certificate -Cert $cert -FilePath jochona-test.cer
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\Root
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher

   # Sign the built driver binary, then regenerate and sign the catalog.
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com `
       "driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter\JochonaDisplayAdapter.dll"
   Inf2Cat.exe /driver:"driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter" /os:10_X64,Server10_X64
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com `
       "driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter\JochonaDisplayAdapter.cat"
   ```

4. **Create the device node and install the driver.** This matters:
   `JochonaDisplayAdapter.inf` declares a software/root-enumerated device
   (`Root\JochonaDisplayAdapter`, no physical hardware ever enumerates
   it), and `pnputil /add-driver ... /install` **only stages the driver
   into the driver store and updates devices that already exist** — it
   never creates this device node, so that command alone leaves nothing
   installed. PnPUtil has no command that creates a root-enumerated
   device node (its full verb list is `/add-driver`, `/delete-driver`,
   `/export-driver`, `/enum-drivers`, `/enum-devices`,
   `/enum-devicetree`, `/disable-device`, `/enable-device`,
   `/restart-device`, `/remove-device`, `/scan-devices`, `/enum-classes`,
   `/enum-interfaces`, `/enum-containers` — see [PnPUtil Command
   Syntax](https://learn.microsoft.com/windows-hardware/drivers/devtest/pnputil-command-syntax)).
   Use `devcon` instead, which creates the device node *and* installs
   the driver onto it in one step (from an elevated prompt, in the
   directory containing the signed `.inf`):

   ```powershell
   devcon install JochonaDisplayAdapter.inf Root\JochonaDisplayAdapter
   ```

   `devcon.exe` ships in ready-to-run form with the Windows Driver Kit,
   under `Windows Kits\10\Tools\<version>\<arch>\devcon.exe` — see
   [DevCon Install](https://learn.microsoft.com/windows-hardware/drivers/devtest/devcon-install).
   To remove the device node and driver package again, use
   `pnputil /remove-device /deviceid ROOT\JochonaDisplayAdapter` followed
   by `pnputil /delete-driver <oemN.inf> /uninstall /force` for the
   package found under `%SystemRoot%\INF` — or just run `uninstall.ps1`.

`sign-and-install.ps1` / `uninstall.ps1` (shipped in each release zip,
source in [`docs/`](.) alongside this file) automate steps 1 (reboot
required first), 3, and 4 end to end, including locating `signtool`,
`Inf2Cat`, and `devcon` on this machine and printing an actionable error
if one is missing.

## What this repository will never do

- **No part of the CI/release build pipeline ever installs a
  self-signed root certificate anywhere, or signs anything.**
  `.github/workflows/ci-validation.yml` and
  `.github/workflows/release.yml` only compile and package an unsigned
  `.dll`/`.inf`/`.cat` — the `Import-Certificate` /
  `LocalMachine\Root` trust step only ever runs inside
  `sign-and-install.ps1`, which is never invoked by CI and only runs
  when a human downloads a release and chooses to run it, on their own
  machine. A release build that *needs* `LocalMachine\Root` trust to
  load is not release-ready; it needs a real signing certificate (see
  below) instead — that remains the target state, the local-signing
  script is a stopgap for testing until then.
- **No CI job disables Secure Boot, driver signature enforcement, or
  installs trust anchors on the build agent as a substitute for real
  signing.** `.github/workflows/ci-validation.yml` only compiles the
  driver (`/p:RunApiValidator=false` skips the separate WHQL API
  validator, not signature enforcement) and uploads unsigned build
  artifacts; it performs no signing step at all.

## Release signing: SignPath Foundation

Public releases are intended to be signed through the [SignPath
Foundation](https://signpath.org/) open-source program, which issues an
EV code-signing certificate usable from a CI pipeline without the
private key ever leaving SignPath's HSM.

**Status: not yet enrolled.** Nothing in this repository claims
SignPath approval — enrollment (project application, artifact
configuration, and organization admin review) is a manual step the
repository owner completes directly with SignPath and is out of scope
for this codebase change. What *is* here is the shape a SignPath-driven
release pipeline expects, so enrollment is a matter of configuration,
not restructuring:

- A deterministic, reproducible unsigned build artifact per
  architecture (`FilesToPackage` in `JochonaDisplayAdapter.vcxproj`
  already isolates the driver DLL as the single package input).
- `.inf`/`.cat` generation kept as a separate, scriptable step
  (`Inf2Cat.exe`, shown above) so SignPath's artifact-signing step can
  slot in between "build" and "catalog" without a script rewrite.
- No hardcoded certificate thumbprints, subject names, or private key
  material anywhere in this repository — SignPath's CI integration
  supplies the signing identity at release time, not at commit time.

When enrollment is complete, the release workflow's signing step should:

1. Upload the unsigned build artifact to SignPath via their CI action/API.
2. Receive the signed `.dll` and `.cat` back.
3. Publish exactly those signed artifacts as the GitHub Release assets —
   never a locally test-signed build.

Until then, only test-signed builds exist, and installing one requires
the machine-local test-signing steps above, on the installer's own
machine, by the installer's own choice.
