# Driver signing

The Jochona Display Adapter is a kernel-adjacent UMDF component: Windows
will not load it at all unless it is signed appropriately for the target
machine's signing policy. This document covers **local development
signing only** and the **release signing configuration this repository
is prepared for**. It does **not** claim SignPath (or any other CA/CI
signing service) approval has been granted — that is a separate,
external step the repository owner must complete with SignPath directly.

**This procedure is manual and developer-only.** It has not been
exercised on Windows by the Jochona team in CI or on a clean machine.
The `v1.0.0` release is an **unsigned developer/test build** — it is for
developers who build and sign locally, not an end-user installer.

## Prerequisites

- A Windows Driver Kit / Windows SDK installation providing:
  - `signtool.exe` — "Windows SDK Signing Tools for Desktop Apps"
    component (Visual Studio Installer → Individual Components, or the
    standalone Windows SDK installer).
  - `Inf2Cat.exe` and `devcon.exe` — Windows Driver Kit
    (https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk).
    `devcon.exe` ships in ready-to-run form under
    `Windows Kits\10\Tools\<version>\<arch>\devcon.exe`.
- Test signing mode enabled on the target machine (see step 1 below).
- **Secure Boot caveat:** if `bcdedit /set testsigning on` fails or the
  "Test Mode" desktop watermark doesn't appear after reboot, Secure Boot
  must be disabled in firmware first — it blocks test-signing mode on
  many OEM boards.

## Development test signing

Test signing is scoped to the developer's own machine and is reversible.
It never touches another machine's trust store and this repository's
build/release scripts **never** install a self-signed root certificate
into any store as part of a release — that practice is explicitly
disallowed for this project (see "What this repository will never do"
below).

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
   See the Secure Boot caveat above if this doesn't take effect.

2. **Build** `driver/JochonaDisplayAdapter/JochonaDisplayAdapter.sln`
   (Release or Debug, x64 or ARM64) under a Windows Driver Kit
   environment matching `.github/workflows/ci-validation.yml`. (Skip
   this step if you downloaded a release zip — it already contains the
   built `.dll`/`.inf`/unsigned `.cat`.)

3. **Self-sign the build output** with a certificate scoped to *your
   own* certificate store — never distributed, never checked into the
   repository, never installed as a trusted root on any other machine.
   The certificate goes in `Cert:\CurrentUser\My`, which is `signtool`'s
   default lookup store — no `/sm` (machine-store) flag needed on either
   `signtool` call below:

   ```powershell
   # One-time: create a local test certificate in your own user store.
   $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=Jochona Display Adapter Test" `
       -KeyUsage DigitalSignature -FriendlyName "Jochona Display Adapter Test Signing" `
       -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
   ```

   **Trusting this certificate is a separate, manual, machine-local
   step — read this before running it.** The commands below add the
   certificate to `LocalMachine\Root` and `LocalMachine\TrustedPublisher`
   so *this machine's* driver-load policy accepts it. This changes what
   your machine trusts system-wide, is scoped to this machine only, and
   must never be scripted into an installer, a release artifact, or CI
   (see "What this repository will never do" below):

   ```powershell
   Export-Certificate -Cert $cert -FilePath jochona-test.cer
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\Root
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
   ```

   Then sign the `.dll`, regenerate the catalog from it, and sign the
   catalog — **in this exact order**, because `Inf2Cat` computes the
   catalog's file hashes from the files as they exist on disk at the
   time it runs, and the zip's `.cat` was produced from the *unsigned*
   `.dll`; once the `.dll`'s signature changes its hash, the shipped
   `.cat` no longer matches and must be regenerated before it, too, is
   signed:

   ```powershell
   # 1. Sign the driver binary.
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
       "driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter\JochonaDisplayAdapter.dll"

   # 2. Regenerate the catalog from the now-signed dll.
   Inf2Cat.exe /driver:"driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter" /os:10_X64,Server10_X64

   # 3. Sign the regenerated catalog.
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
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
   Use `devcon install` instead, which creates the device node *and*
   installs the driver onto it in one step (from an elevated prompt, in
   the directory containing the signed `.inf`) — `devcon update` is the
   wrong verb here because it only updates devices that already exist,
   which this root-enumerated device never does on its own:

   ```powershell
   devcon install JochonaDisplayAdapter.inf Root\JochonaDisplayAdapter
   ```

   `devcon.exe` ships in ready-to-run form with the Windows Driver Kit,
   under `Windows Kits\10\Tools\<version>\<arch>\devcon.exe` — see
   [DevCon Install](https://learn.microsoft.com/windows-hardware/drivers/devtest/devcon-install).

5. **Remove everything again, by hand, when you're done testing.** There
   is no uninstall script — every step here is a manual, reversible,
   machine-local action and removal is the same:

   ```powershell
   # Remove the device node.
   pnputil /remove-device /deviceid ROOT\JochonaDisplayAdapter

   # Find and delete the driver package (oemN.inf under %SystemRoot%\INF
   # whose contents reference Root\JochonaDisplayAdapter) from the
   # driver store:
   pnputil /delete-driver <oemN.inf> /uninstall /force

   # Remove the test certificate from every store it was added to.
   Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher, Cert:\CurrentUser\My |
       Where-Object { $_.Subject -eq 'CN=Jochona Display Adapter Test' } |
       Remove-Item
   ```

## What this repository will never do

- **No release script ever injects a self-signed root certificate.** The
  `Import-Certificate -CertStoreLocation Cert:\LocalMachine\Root` step
  above is a manual, interactive, single-machine developer action — it
  is documented, not automated, and MUST NOT be added to CI or to any
  installer/release script. A release build that needs `LocalMachine\Root`
  trust to load is not release-ready; it needs a real signing certificate
  (see below) instead.
- **No CI job disables Secure Boot, driver signature enforcement, or
  installs trust anchors on the build agent as a substitute for real
  signing.** `.github/workflows/ci-validation.yml` only compiles the
  driver (`/p:RunApiValidator=false` skips the separate WHQL API
  validator, not signature enforcement) and uploads unsigned build
  artifacts; it performs no signing step at all.
- **No release artifact ships a script that signs, trusts, or installs
  anything automatically.** `.github/workflows/release.yml` bundles only
  the built (unsigned) driver files and
  [`docs/driver-install-snippet.md`](driver-install-snippet.md) (as
  `INSTALL.md`) describing the manual steps above — it does not bundle a
  `.ps1` helper that performs them.

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
