# LucidGrasp installer for Windows.
#
# Usage:
#   irm https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.ps1 | iex
#
# The script resolves the latest release, verifies the SHA-256 checksum, makes
# sure the Microsoft Visual C++ runtime is present, and runs the Inno Setup
# installer silently. Qt6 and OpenCV are already bundled by that installer, so
# there is nothing else to download.
#
# Run it from an elevated or normal PowerShell window; the setup requests UAC
# elevation itself.

[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$Repo = "dialga-cmd/LucidGrasp",
    [string]$Destination = "",
    [switch]$NoDeps,
    [switch]$NoVerify,
    [switch]$Uninstall,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# Windows PowerShell 5.1 still negotiates TLS 1.0/1.1 on some older systems.
try {
    $tls12 = [System.Net.SecurityProtocolType]::Tls12
    if (-not ([System.Net.ServicePointManager]::SecurityProtocol -band $tls12)) {
        [System.Net.ServicePointManager]::SecurityProtocol = `
            [System.Net.ServicePointManager]::SecurityProtocol -bor $tls12
    }
}
catch {
    # Older .NET versions do not expose the needed APIs; Invoke-WebRequest will
    # still use whatever the system default is.
}

function Write-Step($Message) { Write-Host "==> $Message" }
function Write-Warn($Message) { Write-Warning $Message }

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Start-Elevated([string]$FilePath, [string[]]$Arguments) {
    $params = @{
        FilePath     = $FilePath
        ArgumentList = $Arguments
        Wait         = $true
        PassThru     = $true
    }
    if (-not (Test-Admin)) {
        $params.Verb = "RunAs"
    }
    return Start-Process @params
}

function Get-LatestVersion {
    $headers = @{ "User-Agent" = "LucidGrasp-Installer" }
    $api = "https://api.github.com/repos/$Repo/releases/latest"
    try {
        $release = Invoke-RestMethod -Uri $api -Headers $headers -UseBasicParsing
        if ($release.tag_name) {
            return ($release.tag_name -replace '^v', '')
        }
    }
    catch {
        # Fall through to the redirect check below, which is not rate limited.
    }

    try {
        $request = [System.Net.HttpWebRequest]::Create("https://github.com/$Repo/releases/latest")
        $request.Method = "HEAD"
        $request.AllowAutoRedirect = $false
        $request.UserAgent = "LucidGrasp-Installer"
        $response = $request.GetResponse()
        try {
            $location = $response.Headers["Location"]
        }
        finally {
            $response.Close()
        }
        if ($location -match '/tag/v?([^/]+)$') {
            return $Matches[1]
        }
    }
    catch {
        # The caller reports a single clear error below.
    }

    throw "Could not resolve the latest release. Pass -Version x.y.z."
}

function Download-File($Url, $OutFile) {
    if ($DryRun) {
        Write-Step "would download $Url"
        return
    }
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
}

function Test-Asset($FilePath, $BaseUrl) {
    if ($NoVerify) {
        Write-Warn "Skipping SHA-256 verification for $(Split-Path -Leaf $FilePath)"
        return
    }

    $leaf = Split-Path -Leaf $FilePath
    $sumUrl = "$BaseUrl/$leaf.sha256"
    $sumFile = "$FilePath.sha256"
    try {
        Download-File $sumUrl $sumFile
    }
    catch {
        throw "No SHA-256 checksum is published for $leaf. Rerun with -NoVerify to skip verification."
    }

    $expected = ((Get-Content $sumFile -Raw) -split '\s+')[0].ToLowerInvariant()
    $actual = (Get-FileHash $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        throw "SHA-256 verification failed for $leaf`n  expected: $expected`n  actual:   $actual"
    }
    Write-Step "Verified SHA-256 for $leaf"
}

function Ensure-VCRuntime {
    $system32 = Join-Path $env:SystemRoot "System32"
    $missing = @()
    foreach ($dll in @("vcruntime140.dll", "msvcp140.dll")) {
        if (-not (Test-Path (Join-Path $system32 $dll))) {
            $missing += $dll
        }
    }
    if ($missing.Count -eq 0) {
        Write-Step "Microsoft Visual C++ runtime is present."
        return
    }

    Write-Warn "Missing Microsoft Visual C++ runtime files: $($missing -join ', ')"
    if ($NoDeps) {
        Write-Warn "Install the Microsoft Visual C++ 2015-2022 Redistributable (x64) manually, or rerun without -NoDeps."
        return
    }

    $vcUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    $vcFile = Join-Path $env:TEMP ("vc_redist.x64." + [guid]::NewGuid().ToString("N") + ".exe")
    try {
        Write-Step "Downloading the Microsoft Visual C++ 2015-2022 Redistributable (x64)..."
        Download-File $vcUrl $vcFile
        if ($DryRun) { return }
        $proc = Start-Elevated $vcFile @("/install", "/quiet", "/norestart")
        if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
            throw "The Visual C++ runtime installer failed with exit code $($proc.ExitCode)."
        }
        Write-Step "Installed the Microsoft Visual C++ runtime."
    }
    finally {
        Remove-Item $vcFile -Force -ErrorAction SilentlyContinue
    }
}

function Get-UninstallEntry {
    $roots = @(
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($key in Get-ChildItem $root -ErrorAction SilentlyContinue) {
            $props = Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue
            if ($props -and $props.DisplayName -like "*LucidGrasp*") {
                return $props
            }
        }
    }
    return $null
}

function Get-UninstallerPath($Entry) {
    foreach ($candidate in @($Entry.QuietUninstallString, $Entry.UninstallString)) {
        if (-not $candidate) { continue }
        if ($candidate -match '^\s*"([^"]+)"') { return $Matches[1] }
        if ($candidate -match '^\s*(\S+?\.exe)') { return $Matches[1] }
    }
    foreach ($dir in @($Entry.InstallLocation, $Entry."Inno Setup: App Path")) {
        if (-not $dir) { continue }
        $candidate = Join-Path $dir "unins000.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Uninstall-LucidGrasp {
    $entry = Get-UninstallEntry
    if (-not $entry) {
        throw "LucidGrasp does not appear to be installed (no uninstall entry found)."
    }

    $uninstaller = Get-UninstallerPath $entry
    if (-not $uninstaller -or -not (Test-Path $uninstaller)) {
        throw "The LucidGrasp uninstaller could not be found. Remove it from Add or Remove Programs instead."
    }

    if ($DryRun) {
        Write-Step "would run $uninstaller /VERYSILENT"
        return
    }

    Write-Step "Uninstalling LucidGrasp..."
    $proc = Start-Elevated $uninstaller @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
    if ($proc.ExitCode -ne 0) {
        throw "The uninstaller failed with exit code $($proc.ExitCode)."
    }
    Write-Step "LucidGrasp was uninstalled."
}

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "LucidGrasp requires 64-bit Windows."
}

if ($Uninstall) {
    Uninstall-LucidGrasp
    return
}

if (-not $Version) {
    $Version = Get-LatestVersion
}
$Version = $Version -replace '^v', ''

if ($Version -notmatch '^[0-9A-Za-z._-]+$') {
    throw "Invalid version: $Version"
}

$baseUrl = "https://github.com/$Repo/releases/download/v$Version"
$asset = "LucidGrasp_Setup_x64.exe"

if ($DryRun) {
    Write-Step "Windows $([Environment]::OSVersion.VersionString)"
    Write-Step "Version: $Version"
    Write-Step "Asset: $asset"
    Write-Step "Installer: $baseUrl/$asset"
    if ($Destination) { Write-Step "Destination: $Destination" }
    Write-Step "Would verify the checksum, install the Visual C++ runtime if missing, and run the installer silently."
    return
}

$tempDir = Join-Path $env:TEMP ("lucidgrasp-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

try {
    $installer = Join-Path $tempDir $asset
    Write-Step "Downloading LucidGrasp $Version..."
    Download-File "$baseUrl/$asset" $installer
    Test-Asset $installer $baseUrl
    Ensure-VCRuntime

    $arguments = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
    if ($Destination) {
        $arguments += "/DIR=`"$Destination`""
    }

    Write-Step "Running the installer (Windows will ask for elevation if needed)..."
    $proc = Start-Elevated $installer $arguments
    if ($proc.ExitCode -ne 0) {
        throw "The installer failed with exit code $($proc.ExitCode)."
    }

    Write-Step "LucidGrasp $Version was installed."
    Write-Step "Launch it from the Start menu."
}
finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
