<#
.SYNOPSIS
    Removes the Jochona Display Adapter test device/driver and the local
    test-signing certificate installed by sign-and-install.ps1.

.DESCRIPTION
    Undoes everything sign-and-install.ps1 does, on THIS machine only:
      1. Removes the Root\JochonaDisplayAdapter device node.
      2. Deletes the JochonaDisplayAdapter driver package from the driver
         store.
      3. Removes the "CN=Jochona Display Adapter Test" certificate from
         Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher, and
         Cert:\LocalMachine\My.

    Safe to re-run: every step is a no-op if its target is already gone.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---- elevation check ----
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated (Administrator) PowerShell session."
}

# ---- remove the device node ----
Write-Host "Removing Root\JochonaDisplayAdapter device node (if present)..."
pnputil /remove-device /deviceid 'ROOT\JochonaDisplayAdapter' | Out-Host
# pnputil exits non-zero when there is nothing matching /deviceid -- that is
# the expected, non-fatal case on a machine that was never installed (or is
# already uninstalled), so this step does not throw on failure.

# ---- delete the driver package from the driver store ----
# pnputil's own listing commands (/enum-drivers) label fields in the
# system's display language, so matching on those labels is unreliable.
# Every published driver package's inf is copied verbatim to
# %SystemRoot%\INF as oemNN.inf; find ours there instead by its hardware ID,
# which is locale-independent.
$infDir = Join-Path $env:SystemRoot 'INF'
$ourPackages =
    Get-ChildItem -Path $infDir -Filter 'oem*.inf' -ErrorAction SilentlyContinue |
    Where-Object { (Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue) -match '(?i)Root\\JochonaDisplayAdapter' }

if (-not $ourPackages) {
    Write-Host "No JochonaDisplayAdapter driver package found in the driver store; nothing to delete."
} else {
    foreach ($pkg in $ourPackages) {
        Write-Host "Deleting driver package $($pkg.Name)..."
        pnputil /delete-driver $pkg.Name /uninstall /force
        if ($LASTEXITCODE -ne 0) { throw "pnputil /delete-driver failed for $($pkg.Name) (exit $LASTEXITCODE)." }
    }
}

# ---- remove the local test certificate sign-and-install.ps1 installed ----
$subject = 'CN=Jochona Display Adapter Test'
foreach ($store in 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher', 'Cert:\LocalMachine\My') {
    Get-ChildItem $store | Where-Object { $_.Subject -eq $subject } | ForEach-Object {
        Write-Host "Removing test certificate $($_.Thumbprint) from $store..."
        Remove-Item -LiteralPath $_.PSPath -Force
    }
}

Write-Host "Uninstall complete." -ForegroundColor Green
