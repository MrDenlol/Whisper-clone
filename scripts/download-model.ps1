<#
.SYNOPSIS
    Downloads a ggml Whisper model into the WhisperFlowClone models folder.

.DESCRIPTION
    Models are never committed to this repository. This script puts the file
    where WhisperFlowClone looks for it by default:

        %LOCALAPPDATA%\WhisperFlowClone\models\ggml-<model>.bin

.PARAMETER Model
    tiny | base | small (default) | medium | large-v3 | large-v3-turbo

.PARAMETER Destination
    Optional target directory. Defaults to %LOCALAPPDATA%\WhisperFlowClone\models.

.EXAMPLE
    .\scripts\download-model.ps1
    .\scripts\download-model.ps1 -Model base
    .\scripts\download-model.ps1 -Model small -Destination D:\models
#>
[CmdletBinding()]
param(
    [ValidateSet('tiny', 'base', 'small', 'medium', 'large-v3', 'large-v3-turbo')]
    [string]$Model = 'small',

    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $root = $env:LOCALAPPDATA
    if ([string]::IsNullOrWhiteSpace($root)) {
        $root = $env:APPDATA
    }
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw 'Neither LOCALAPPDATA nor APPDATA is set. Pass -Destination explicitly.'
    }
    $Destination = Join-Path $root 'WhisperFlowClone\models'
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$fileName = "ggml-$Model.bin"
$target = Join-Path $Destination $fileName
$url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$fileName"

if (Test-Path $target) {
    $existing = (Get-Item $target).Length
    if ($existing -gt 1MB) {
        Write-Host "Already present: $target ($([math]::Round($existing / 1MB, 1)) MiB)"
        return
    }
    Write-Warning "Removing incomplete download: $target"
    Remove-Item $target -Force
}

Write-Host "Downloading $fileName"
Write-Host "  from: $url"
Write-Host "  to:   $target"

$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing

$size = (Get-Item $target).Length
if ($size -lt 1MB) {
    Remove-Item $target -Force
    throw "Downloaded file is only $size bytes - the download failed. Try again or fetch the file manually from https://huggingface.co/ggerganov/whisper.cpp/tree/main"
}

Write-Host ("Done: {0} ({1} MiB)" -f $target, [math]::Round($size / 1MB, 1))
Write-Host 'Run WhisperFlowClone.exe (no arguments) to record and transcribe.'
