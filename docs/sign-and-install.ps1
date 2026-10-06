<#
.SYNOPSIS
    Self-signs and installs the unsigned Jochona Display Adapter test build.

.DESCRIPTION
    v1.0.0 is an UNSIGNED developer/test build: the CI build in this
    repository produces an unsigned JochonaDisplayAdapter.dll and .cat, and
    test-signing mode alone (bcdedit /set testsigning on) does NOT sign
    them for you -- Windows still refuses to load an unsigned catalog. This
    script does the rest of the work by hand, on THIS machine only:

      1. Creates (or reuses) a local test-signing certificate in
         Cert:\LocalMachine\My and trusts it as both a root CA and a
         trusted publisher, LOCAL MACHINE ONLY.
      2. Signs JochonaDisplayAdapter.dll with that certificate.
      3. Regenerates JochonaDisplayAdapter.cat with Inf2Cat and signs it.
      4. Creates the Root\JochonaDisplayAdapter device node and installs
         the driver onto it with devcon (JochonaDisplayAdapter.inf
         declares a software/root-enumerated device -- pnputil /add-driver
         alone only stages the driver and updates EXISTING matching
         devices, it never creates this one; see
         https://learn.microsoft.com/windows-hardware/drivers/devtest/devcon-install).

    This is a local developer/test workflow, not a production install --
    see SIGNING.md in the project repository for the full signing status
    (SignPath Foundation enrollment is not yet complete) and for how to
    remove everything this script installs (or just run uninstall.ps1,
    shipped alongside this script).

.NOTES
    Prerequisites this script does NOT install for you:
      - Test signing mode enabled and the machine rebooted:
            bcdedit /set testsigning on
            Restart-Computer
        If testsigning won't turn on, Secure Boot must be disabled in
        firmware first -- Secure Boot blocks test-signing mode on many
        OEM boards.
      - signtool.exe  -- Windows SDK "Windows SDK Signing Tools for
        Desktop Apps" component (Visual Studio Installer, or the
        standalone Windows SDK installer).
      - Inf2Cat.exe and devcon.exe -- Windows Driver Kit
        (https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk).
        devcon.exe ships in ready-to-run form under
        Windows Kits\10\Tools\<version>\<arch>\devcon.exe.
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

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$infPath = Join-Path $root 'JochonaDisplayAdapter.inf'
$dllPath = Join-Path $root 'JochonaDisplayAdapter.dll'
$catPath = Join-Path $root 'JochonaDisplayAdapter.cat'

if (-not (Test-Path -LiteralPath $infPath)) {
    throw "JochonaDisplayAdapter.inf not found next to this script ($root). Run this script from inside the extracted release zip."
}
if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "JochonaDisplayAdapter.dll not found next to this script ($root). Run this script from inside the extracted release zip."
}

# ---- figure out which Inf2Cat /os: list applies, from the stamped inf itself ----
# (so this script works unmodified in both the x64 and ARM64 release zips)
$infText = Get-Content -LiteralPath $infPath -Raw
$archMatch = [regex]::Match($infText, '(?m)^\[Standard\.NT(?<arch>[A-Za-z0-9]+)\]')
if (-not $archMatch.Success) {
    throw "Could not find a [Standard.NT<arch>] section in $infPath -- this does not look like a stamped driver inf."
}
$stampedArch = $archMatch.Groups['arch'].Value
$inf2catOsList = switch -Regex ($stampedArch) {
    '^(amd64|AMD64)$' { '10_X64,Server10_X64' }
    '^(ARM64|arm64)$' { '10_VB_ARM64,10_NI_ARM64,10_CO_ARM64,10_GE_ARM64,Server10_ARM64' }
    default {
        throw "Unrecognized stamped architecture '$stampedArch' in $infPath -- update this script's /os: mapping (see https://learn.microsoft.com/windows-hardware/drivers/devtest/inf2cat)."
    }
}

$hostArch = switch ($env:PROCESSOR_ARCHITECTURE) {
    'AMD64' { 'x64' }
    'ARM64' { 'arm64' }
    'x86' { 'x86' }
    default { throw "Unrecognized host architecture '$($env:PROCESSOR_ARCHITECTURE)'." }
}

# ---- locate WDK/SDK tools (installed separately; see .NOTES above) ----
function Find-KitTool {
    param([string]$ExeName, [string]$SubdirPattern)
    $cmd = Get-Command $ExeName -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $kitsRoots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10",
        "${env:ProgramFiles}\Windows Kits\10"
    ) | Where-Object { Test-Path -LiteralPath $_ }
    foreach ($kitsRoot in $kitsRoots) {
        $found =
            Get-ChildItem -Path (Join-Path $kitsRoot $SubdirPattern) -Filter $ExeName -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

$signtool = Find-KitTool -ExeName 'signtool.exe' -SubdirPattern "bin\*\$hostArch"
if (-not $signtool) {
    throw "signtool.exe not found. Install the 'Windows SDK Signing Tools for Desktop Apps' component (Visual Studio Installer -> Individual Components, or the standalone Windows SDK installer), then re-run this script."
}

$inf2cat = Find-KitTool -ExeName 'Inf2Cat.exe' -SubdirPattern 'bin\*\x86'
if (-not $inf2cat) {
    throw "Inf2Cat.exe not found. Install the Windows Driver Kit (https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk), then re-run this script."
}

$devcon = Find-KitTool -ExeName 'devcon.exe' -SubdirPattern "Tools\*\$hostArch"
if (-not $devcon) {
    throw "devcon.exe not found under Windows Kits\10\Tools. Install the Windows Driver Kit (https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk), which ships devcon.exe in ready-to-run form, then re-run this script."
}

Write-Host "signtool: $signtool"
Write-Host "Inf2Cat : $inf2cat"
Write-Host "devcon  : $devcon"

# ---- create or reuse the local test-signing certificate ----
$subject = 'CN=Jochona Display Adapter Test'
$cert = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating local test-signing certificate ($subject) in Cert:\LocalMachine\My..."
    $cert = New-SelfSignedCertificate -Type Custom -Subject $subject `
        -KeyUsage DigitalSignature -FriendlyName 'Jochona Display Adapter Test Signing' `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
} else {
    Write-Host "Reusing existing local test-signing certificate (thumbprint $($cert.Thumbprint))."
}

Write-Warning @"
This installs a SELF-SIGNED certificate as a trusted root AND trusted
publisher in the LOCAL MACHINE certificate store so Windows will load this
unsigned test driver. This weakens this machine's code-signing trust for as
long as the certificate stays installed. Run the bundled uninstall.ps1 to
remove it (and the driver) when you are done testing. Do not run this on a
machine you do not control.
"@

$cerPath = Join-Path $root 'jochona-test.cer'
try {
    Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
} finally {
    Remove-Item -LiteralPath $cerPath -Force -ErrorAction SilentlyContinue
}

# ---- sign the dll, regenerate the catalog, sign the catalog ----
& $signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com $dllPath
if ($LASTEXITCODE -ne 0) { throw "signtool failed signing $dllPath (exit $LASTEXITCODE)." }

& $inf2cat "/driver:$root" "/os:$inf2catOsList"
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed to regenerate the catalog for $root (exit $LASTEXITCODE)." }
if (-not (Test-Path -LiteralPath $catPath)) { throw "Inf2Cat did not produce $catPath." }

& $signtool sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com $catPath
if ($LASTEXITCODE -ne 0) { throw "signtool failed signing $catPath (exit $LASTEXITCODE)." }

# ---- create the device node and install the driver onto it ----
Push-Location $root
try {
    & $devcon install 'JochonaDisplayAdapter.inf' 'Root\JochonaDisplayAdapter'
    if ($LASTEXITCODE -ne 0) { throw "devcon install failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
}

Write-Host "Installed. Verify with: pnputil /enum-devices /deviceid ROOT\JochonaDisplayAdapter" -ForegroundColor Green
