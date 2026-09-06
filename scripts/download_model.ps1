<#
.SYNOPSIS
    Downloads a ggml Whisper model for WhisperFlowClone and verifies its SHA-1.

.DESCRIPTION
    Model weights are never committed to this repository (see .gitignore).
    The file is fetched from the official whisper.cpp collection on Hugging Face
    (https://huggingface.co/ggerganov/whisper.cpp, MIT) and written to the first
    directory WhisperFlowClone.exe searches:

        %LOCALAPPDATA%\WhisperFlowClone\models\ggml-<model>.bin

    Use -Destination to put it somewhere else, e.g. next to the executable
    (dist\models) for a portable/USB-stick layout.

    The SHA-1 checksums below are the ones published by whisper.cpp
    (models/README.md, v1.9.3). A mismatch deletes the file and fails the script.

.PARAMETER Model
    tiny | base | small (default) | medium | large-v3 | large-v3-turbo

.PARAMETER Destination
    Target directory. Default: %LOCALAPPDATA%\WhisperFlowClone\models

.PARAMETER Force
    Re-download even if a file with a valid checksum already exists.

.EXAMPLE
    .\scripts\download_model.ps1
    .\scripts\download_model.ps1 -Model base
    .\scripts\download_model.ps1 -Model small -Destination .\dist\models
#>
[CmdletBinding()]
param(
    [ValidateSet('tiny', 'base', 'small', 'medium', 'large-v3', 'large-v3-turbo')]
    [string]$Model = 'small',

    [string]$Destination = '',

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# Published by ggml-org/whisper.cpp in models/README.md (same values on the
# Hugging Face model card). Keep in sync when bumping the whisper.cpp pin.
$knownSha1 = @{
    'tiny'           = 'bd577a113a864445d4c299885e0cb97d4ba92b5f'
    'base'           = '465707469ff3a37a2b9b8d8f89f2f99de7299dac'
    'small'          = '55356645c2b361a969dfd0ef2c5a50d530afd8d5'
    'medium'         = 'fd9727b6e1217c2f614f9b698455c4ffd82463b4'
    'large-v3'       = 'ad82bf6a9043ceed055076d0fd39f5f186ff8062'
    'large-v3-turbo' = '4af2b29d7ec73d781377bfd1758ca957a807e941'
}

$approxSize = @{
    'tiny' = '75 MiB'; 'base' = '142 MiB'; 'small' = '466 MiB'
    'medium' = '1.5 GiB'; 'large-v3' = '2.9 GiB'; 'large-v3-turbo' = '1.5 GiB'
}

function Get-Sha1Hex([string]$path) {
    return (Get-FileHash -Algorithm SHA1 -Path $path).Hash.ToLowerInvariant()
}

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $root = $env:LOCALAPPDATA
    if ([string]::IsNullOrWhiteSpace($root)) { $root = $env:APPDATA }
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw 'Neither LOCALAPPDATA nor APPDATA is set. Pass -Destination explicitly.'
    }
    $Destination = Join-Path $root 'WhisperFlowClone\models'
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$Destination = (Resolve-Path $Destination).Path

$fileName = "ggml-$Model.bin"
$target   = Join-Path $Destination $fileName
$partial  = "$target.part"
$url      = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$fileName"
$expected = $knownSha1[$Model]

if ((Test-Path $target) -and -not $Force) {
    Write-Host "Found existing $target - verifying SHA-1..."
    $actual = Get-Sha1Hex $target
    if ($actual -eq $expected) {
        Write-Host ("Already present and valid: {0} ({1:N1} MiB)" -f $target, ((Get-Item $target).Length / 1MB))
        Write-Host 'Run WhisperFlowClone.exe --list-models to confirm the app sees it.'
        return
    }
    Write-Warning "Checksum mismatch ($actual) - re-downloading."
    Remove-Item $target -Force
}

Write-Host "Downloading $fileName (~$($approxSize[$Model]))"
Write-Host "  from: $url"
Write-Host "  to:   $target"

if (Test-Path $partial) { Remove-Item $partial -Force }

# TLS 1.2 for Windows PowerShell 5.1; no-op on PowerShell 7.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }

$previousProgress = $ProgressPreference
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is ~10x slower with the progress bar
try {
    Invoke-WebRequest -Uri $url -OutFile $partial -UseBasicParsing
} finally {
    $ProgressPreference = $previousProgress
}

$size = (Get-Item $partial).Length
if ($size -lt 1MB) {
    Remove-Item $partial -Force
    throw "Downloaded file is only $size bytes - the download failed. Try again or fetch $fileName manually from https://huggingface.co/ggerganov/whisper.cpp/tree/main"
}

Write-Host 'Verifying SHA-1...'
$actual = Get-Sha1Hex $partial
if ($actual -ne $expected) {
    Remove-Item $partial -Force
    throw "SHA-1 mismatch for ${fileName}: expected $expected, got $actual. The file was deleted; try again."
}

Move-Item -Force $partial $target
Write-Host ("Done: {0} ({1:N1} MiB, SHA-1 OK)" -f $target, ($size / 1MB))
Write-Host 'Next: run WhisperFlowClone.exe --tray, then hold Ctrl+Shift+Space and speak.'
