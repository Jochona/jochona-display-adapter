# Installing this build

**This is an UNSIGNED test build.** It is not production-ready and has no
WHQL or SignPath signature. See [`SIGNING.md`](SIGNING.md) for the full
signing status and roadmap.

1. **Enable test signing** on the target machine (elevated PowerShell,
   reboot required):

   ```powershell
   bcdedit /set testsigning on
   Restart-Computer
   ```

   Without this, Windows will refuse to load the unsigned `.dll`/`.cat`
   in this archive. Turn it back off (`bcdedit /set testsigning off` +
   reboot) when you're done testing.

2. **Install the driver** (elevated PowerShell/cmd, after test signing is
   on and the machine has rebooted):

   ```powershell
   pnputil /add-driver JochonaDisplayAdapter.inf /install
   ```

3. **Uninstall** when you're done:

   ```powershell
   pnputil /delete-driver JochonaDisplayAdapter.inf /uninstall /force
   ```

Contents of this archive: `JochonaDisplayAdapter.dll`, the matching
stamped `.inf`, and the unsigned `.cat` catalog `Inf2Cat` produced from it
during the build — present only so test-signing mode has a catalog to
accept.
