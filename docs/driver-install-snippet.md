# Installing this build

**This is an UNSIGNED developer/test build.** It is not production-ready
and has no WHQL or SignPath signature. Installing it means trusting a
self-signed certificate on this machine. See
[`SIGNING.md`](SIGNING.md) for the full signing status and roadmap.

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

If `bcdedit /set testsigning on` fails or the watermark doesn't stick,
**Secure Boot must be disabled in firmware first** — many OEM boards block
test-signing mode while Secure Boot is on.

Enabling test signing alone is **not enough**: it only relaxes the
*policy*, it does not sign the `.dll`/`.cat` in this archive. You still
have to self-sign them (next step) or Windows will refuse to load either
file.

## 2. Self-sign and install

From an elevated PowerShell prompt, inside this extracted folder:

```powershell
.\sign-and-install.ps1
```

This script creates (or reuses) a local test-signing certificate, trusts
it on this machine only (`LocalMachine\Root` and
`LocalMachine\TrustedPublisher`), signs
`JochonaDisplayAdapter.dll`/`.cat`, then creates the
`Root\JochonaDisplayAdapter` device node and installs the driver onto it.
It needs `signtool.exe`, `Inf2Cat.exe`, and `devcon.exe` on this machine
(Windows SDK / Windows Driver Kit — the script detects them and prints an
actionable error if one is missing) and prints a warning before it
touches the certificate store. See [`SIGNING.md`](SIGNING.md) for exactly
what it does and why `pnputil /add-driver ... /install` by itself is not
sufficient for this driver.

## 3. Uninstall

```powershell
.\uninstall.ps1
```

Removes the `Root\JochonaDisplayAdapter` device node, deletes the driver
package from the driver store, and removes the local test certificate
`sign-and-install.ps1` installed. Safe to re-run.

---

Contents of this archive: `JochonaDisplayAdapter.dll`, the matching
stamped `.inf`, the unsigned `.cat` catalog `Inf2Cat` produced from it
during the build, and `sign-and-install.ps1`/`uninstall.ps1`.
