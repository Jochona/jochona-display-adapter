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

2. **Build** `driver/JochonaDisplayAdapter/JochonaDisplayAdapter.sln`
   (Release or Debug, x64 or ARM64) under a Windows Driver Kit
   environment matching `.github/workflows/ci-validation.yml`.

3. **Self-sign the build output** with a certificate scoped to *your own
   development machine's* certificate store — never distributed, never
   checked into the repository, never installed as a trusted root on any
   other machine:

   ```powershell
   # One-time: create a local test certificate in your own user store.
   $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=Jochona Display Adapter Test" `
       -KeyUsage DigitalSignature -FriendlyName "Jochona Display Adapter Test Signing" `
       -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")

   # Trust it locally so *your* machine's driver-load policy accepts it —
   # scoped to CurrentUser\TrustedPublisher and LocalMachine\Root ON THIS
   # MACHINE ONLY. Do not script this step into a release pipeline or
   # ship it to end users; see below.
   Export-Certificate -Cert $cert -FilePath jochona-test.cer
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\Root
   Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher

   # Sign the built driver binary and catalog.
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com `
       "driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter\JochonaDisplayAdapter.dll"
   Inf2Cat.exe /driver:"driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter" /os:10_X64,Server10_X64
   signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com `
       "driver\JochonaDisplayAdapter\x64\Debug\JochonaDisplayAdapter\JochonaDisplayAdapter.cat"
   ```

4. Install with `Root\JochonaDisplayAdapter` via `pnputil` or `devcon`
   pointed at the signed `.inf`.

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
