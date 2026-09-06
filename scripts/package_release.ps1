<#
.SYNOPSIS
    Builds the Release configuration and packs a self-contained dist\ folder + zip.

.DESCRIPTION
    Runs on a Windows machine with Visual Studio 2022 (MSVC) and CMake 3.24+:

        1. cmake -S . -B build                      (configure, CPU-only, static whisper.cpp)
        2. cmake --build build --config Release
        3. cmake --install build --config Release --prefix dist --component whisperflow
        4. Compress-Archive dist\* -> WhisperFlowClone-<version>-win-x64.zip

    dist\ contains WhisperFlowClone.exe, the MSVC runtime DLLs it needs
    (vcruntime140*.dll, msvcp140.dll, vcomp140.dll), settings.example.json,
    dictionary.json, LICENSE/LICENSES.md/NOTICE, README.md, scripts\download_model.ps1
    and an empty models\ folder. No .pdb / .lib / .obj files end up in the zip.

    Models are NOT bundled (they are not redistributed by this repository):
    run  .\dist\scripts\download_model.ps1 -Model small -Destination .\dist\models
    to make the folder fully portable, or let the app use %LOCALAPPDATA%.

.PARAMETER BuildDir
    CMake build directory. Default: build

.PARAMETER DistDir
    Output folder. Default: dist (cleaned before install)

.PARAMETER SkipBuild
    Only run the install + zip steps against an existing Release build.

.PARAMETER Portable
    Also generate dist\settings.json from settings.example.json so the app keeps
    its settings and phrase history next to the exe instead of %APPDATA%.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$DistDir = 'dist',
    [switch]$SkipBuild,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake was not found on PATH. Open a "Developer PowerShell for VS 2022" or install CMake 3.24+.'
    }

    if (-not $SkipBuild) {
        Write-Host '== Configure (Release, CPU-only, tests off)'
        cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -DWHISPERFLOW_BUILD_TESTS=OFF
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

        Write-Host '== Build'
        cmake --build $BuildDir --config Release --parallel
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
    }

    if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }

    Write-Host "== Install into $DistDir"
    cmake --install $BuildDir --config Release --prefix $DistDir --component whisperflow
    if ($LASTEXITCODE -ne 0) { throw "cmake install failed ($LASTEXITCODE)" }

    # Belt and braces: the install rules never copy debug artefacts, but a jury
    # zip must not contain them under any circumstances.
    Get-ChildItem $DistDir -Recurse -Include *.pdb, *.ilk, *.lib, *.exp, *.obj | Remove-Item -Force

    if ($Portable) {
        Copy-Item (Join-Path $DistDir 'settings.example.json') (Join-Path $DistDir 'settings.json')
        Write-Host '   portable layout: settings.json created next to the exe'
    }

    $exe = Join-Path $DistDir 'WhisperFlowClone.exe'
    if (-not (Test-Path $exe)) { throw "Expected $exe after install" }

    $version = (Select-String -Path 'CMakeLists.txt' -Pattern 'VERSION\s+(\d+\.\d+\.\d+)' |
                Select-Object -First 1).Matches[0].Groups[1].Value
    $zip = "WhisperFlowClone-$version-win-x64.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }

    Write-Host "== Zip -> $zip"
    Compress-Archive -Path (Join-Path $DistDir '*') -DestinationPath $zip -CompressionLevel Optimal

    Write-Host ''
    Write-Host "Contents of $DistDir\:"
    Get-ChildItem $DistDir -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring((Resolve-Path $DistDir).Path.Length + 1)
        Write-Host ("  {0,10:N0} KiB  {1}" -f ($_.Length / 1KB), $rel)
    }
    Write-Host ''
    Write-Host "Done: $zip"
    Write-Host 'Next: .\dist\scripts\download_model.ps1 -Model small   then   .\dist\WhisperFlowClone.exe --tray'
}
finally {
    Pop-Location
}
