# Installing this build

**This is an UNSIGNED developer/test build, not an end-user installer.**
`v1.0.0` has no WHQL or SignPath signature, and this procedure has not
been exercised on Windows by the Jochona team in CI. It requires the
Windows Driver Kit / Windows SDK (`signtool`, `Inf2Cat`, `devcon`) and
manually trusting a self-signed certificate on your own machine. There
is no script that does this for you — every step below is a command you
run and understand yourself. See [`SIGNING.md`](SIGNING.md) for the full
signing status, policy, and roadmap.

## Prerequisites

- `signtool.exe` — Windows SDK "Windows SDK Signing Tools for Desktop
  Apps" component.
- `Inf2Cat.exe` and `devcon.exe` — Windows Driver Kit
  (https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk).
- Test signing mode enabled on this machine (step 1 below).
- **Secure Boot:** if test signing won't enable, disable Secure Boot in
  firmware first — many OEM boards block test-signing mode while it's on.

## 1. Enable test signing

Elevated PowerShell, reboot required:

```powershell
bcdedit /set testsigning on
Restart-Computer
```

Confirm it took effect: `bcdedit /enum | Select-String testsigning` should
show `Yes`, and a "Test Mode" watermark appears in the desktop corner.
Turn it back off (`bcdedit /set testsigning off` + reboot) when you're
done testing.

Enabling test signing alone is **not enough**: it only relaxes the
*policy*, it does not sign the unsigned `.dll`/`.cat` in this archive.
You still have to self-sign them (next step) or Windows will refuse to
load either file.

## 2. Create and trust a local test certificate

From an elevated PowerShell prompt:

```powershell
# One-time: create a local test certificate in your own user store.
# signtool looks in Cert:\CurrentUser\My by default, so no /sm flag is
# needed on the signtool calls below.
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=Jochona Display Adapter Test" `
    -KeyUsage DigitalSignature -FriendlyName "Jochona Display Adapter Test Signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
```

**Read this before running the next three lines.** They add the
certificate to this machine's `LocalMachine\Root` and
`LocalMachine\TrustedPublisher` stores so *this machine's* driver-load
policy accepts it. This is a manual, machine-wide trust decision you are
making about your own machine — never run this as part of an automated
install, and never do it on a machine you don't control:

```powershell
Export-Certificate -Cert $cert -FilePath jochona-test.cer
Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath jochona-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

## 3. Sign the dll, regenerate the catalog, sign the catalog — in this order

The `.cat` in this archive is **unsigned** and was generated from the
**unsigned** `.dll`. Once you sign the `.dll`, its hash changes, which
invalidates that `.cat`; you must regenerate it from the now-signed
`.dll` before signing the catalog itself. From this extracted folder:

```powershell
# 1. Sign the driver binary.
signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 JochonaDisplayAdapter.dll

# 2. Regenerate the catalog from the now-signed dll.
#    /os list depends on the zip's architecture: 10_X64,Server10_X64 for
#    the x64 zip, 10_VB_ARM64,10_NI_ARM64,10_CO_ARM64,10_GE_ARM64,Server10_ARM64
#    for the ARM64 zip (see JochonaDisplayAdapter.inf's [Standard.NT<arch>] section).
Inf2Cat.exe /driver:. /os:10_X64,Server10_X64

# 3. Sign the regenerated catalog.
signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 JochonaDisplayAdapter.cat
```

## 4. Create the device node and install the driver

`JochonaDisplayAdapter.inf` declares a software/root-enumerated device
(`Root\JochonaDisplayAdapter`) — no physical hardware ever enumerates it,
and `pnputil /add-driver ... /install` only stages the driver and
updates devices that **already exist**; it never creates this one.
PnPUtil has no command that creates a root-enumerated device node. Use
`devcon install` instead (`devcon update` is the wrong verb here — it
also only updates existing devices), from an elevated prompt in this
folder:

```powershell
devcon install JochonaDisplayAdapter.inf Root\JochonaDisplayAdapter
```

`devcon.exe` ships with the Windows Driver Kit under
`Windows Kits\10\Tools\<version>\<arch>\devcon.exe` — see
[DevCon Install](https://learn.microsoft.com/windows-hardware/drivers/devtest/devcon-install).

Verify with: `pnputil /enum-devices /deviceid ROOT\JochonaDisplayAdapter`

## 5. Uninstall

There is no uninstall script. Remove the device node and driver package
first (no certificate store changes needed for this part):

```powershell
pnputil /remove-device /deviceid ROOT\JochonaDisplayAdapter
```

Then find and delete the driver package: every installed package's `.inf`
is copied to `%SystemRoot%\INF` as `oemNN.inf`; find the one referencing
`Root\JochonaDisplayAdapter` and remove it:

```powershell
pnputil /delete-driver <oemNN.inf> /uninstall /force
```

Finally, as a **separate, manual decision**, remove the test certificate
from the stores step 2 added it to, if you no longer want this machine
to trust it:

```powershell
Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher, Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq 'CN=Jochona Display Adapter Test' } |
    Remove-Item
```

---

Contents of this archive: `JochonaDisplayAdapter.dll`, the matching
stamped `.inf`, and the unsigned `.cat` catalog `Inf2Cat` produced from
it during the build. No scripts are included — every step above is run
by hand.
