[CmdletBinding()]
param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot 'dist'),
    [string] $QtRoot = (Join-Path $env:LOCALAPPDATA 'DSO138-Desktop-Tools\Qt\6.8.3\msvc2022_64'),
    [string] $VisualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community',
    [string] $CMakeExe,
    [string] $NinjaExe,
    [string] $VcVars64,
    [string] $RuntimeRedistDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Require-Path([string] $Path, [string] $Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Add-EnvironmentFromVsVars([string] $BatchFile) {
    $cmdLine = 'call "{0}" -vcvars_ver=14.44 >nul && set' -f $BatchFile
    $environmentLines = & $env:ComSpec /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) { throw "Visual Studio environment setup failed ($LASTEXITCODE)." }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
}

function Invoke-Native([string] $Executable, [string[]] $Arguments, [string] $Description) {
    Write-Host "> $Description"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Description failed with exit code $LASTEXITCODE." }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $CMakeExe) {
    $CMakeExe = Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not $NinjaExe) {
    $NinjaExe = Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}
if (-not $VcVars64) {
    $VcVars64 = Join-Path $VisualStudioRoot 'VC\Auxiliary\Build\vcvars64.bat'
}
Require-Path $CMakeExe 'CMake 4.1.1 executable'
Require-Path $NinjaExe 'Ninja executable'
Require-Path $VcVars64 'Visual Studio x64 environment script'
Require-Path (Join-Path $QtRoot 'bin\windeployqt.exe') 'Qt 6.8.3 windeployqt'
Require-Path (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake') 'Qt 6.8.3 CMake package'
$qtVersionText = (& (Join-Path $QtRoot 'bin\windeployqt.exe') --version 2>&1 | Out-String)
if ($qtVersionText -notmatch '6\.8\.3') { throw "Expected Qt 6.8.3, got: $qtVersionText" }
$cmakeVersionText = (& $CMakeExe --version 2>&1 | Out-String)
if ($cmakeVersionText -notmatch 'cmake version 4\.1\.1') { throw "Expected CMake 4.1.1, got: $cmakeVersionText" }

Add-EnvironmentFromVsVars $VcVars64
if ([string]::IsNullOrWhiteSpace($env:VCToolsVersion) -or -not $env:VCToolsVersion.StartsWith('14.44.')) {
    throw "Expected MSVC 14.44 (vcvars64 -vcvars_ver=14.44); got '$env:VCToolsVersion'."
}
$dumpbin = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe'
Require-Path $dumpbin 'MSVC dumpbin dependency inspector'
if (-not $RuntimeRedistDirectory) {
    if ($env:VCToolsRedistDir) {
        $candidate = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath (Join-Path $candidate 'vcruntime140.dll')) {
            $RuntimeRedistDirectory = $candidate
        }
    }
    if (-not $RuntimeRedistDirectory) {
        # A newer VS installation may set VCToolsRedistDir for a different toolset.
        $RuntimeRedistDirectory = Get-ChildItem -LiteralPath (Join-Path $VisualStudioRoot 'VC\Redist\MSVC') -Directory -Filter '14.44.*' |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'vcruntime140.dll') } |
            Select-Object -First 1
    }
    if (-not $RuntimeRedistDirectory) { throw 'Install the x64 MSVC 14.44 redistribution files or pass -RuntimeRedistDirectory.' }
}
Require-Path (Join-Path $RuntimeRedistDirectory 'vcruntime140.dll') 'Matching x64 VC143 runtime directory'
if ((Split-Path -Leaf $RuntimeRedistDirectory) -ne 'Microsoft.VC143.CRT' -or
    $RuntimeRedistDirectory -notmatch '[\\/]x64[\\/]Microsoft\.VC143\.CRT$') {
    throw 'RuntimeRedistDirectory must be an x64\Microsoft.VC143.CRT directory selected by this vcvars environment or passed explicitly.'
}

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($output) | Out-Null
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected a major.minor.patch version in VERSION.' }
$baseName = "DSO138Studio-$version-windows-x64"
$zipPath = Join-Path $output "$baseName.zip"
$hashPath = "$zipPath.sha256"
if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $hashPath)) {
    throw "Package output already exists. Choose a new OutputDirectory or move the prior output first: $zipPath"
}

# Every run gets its own new staging/build directory. Existing user output and build trees are not touched.
$work = Join-Path ([System.IO.Path]::GetTempPath()) "DSO138Studio-package-$([guid]::NewGuid().ToString('N'))"
$build = Join-Path $work 'build'
$stage = Join-Path $work 'stage'
$licenses = Join-Path $stage 'licenses'
$sbom = Join-Path $licenses 'sbom'
[System.IO.Directory]::CreateDirectory($build) | Out-Null
[System.IO.Directory]::CreateDirectory($stage) | Out-Null
[System.IO.Directory]::CreateDirectory($licenses) | Out-Null
[System.IO.Directory]::CreateDirectory($sbom) | Out-Null

$env:PATH = "$(Join-Path $QtRoot 'bin');$(Split-Path -Parent $CMakeExe);$(Split-Path -Parent $NinjaExe);$env:PATH"
$configureArgs = @('--preset', 'windows-release', '-B', $build,
    "-DCMAKE_PREFIX_PATH=$QtRoot", "-DCMAKE_MAKE_PROGRAM=$NinjaExe")
Invoke-Native $CMakeExe $configureArgs 'Configure Windows Release preset in isolated build directory'
Invoke-Native $CMakeExe @('--build', $build, '--config', 'Release', '--parallel') 'Build DSO138 Studio'

$app = $stage
Invoke-Native $CMakeExe @('--install', $build, '--prefix', $app, '--config', 'Release') 'Install application into clean staging directory'
$exe = Join-Path $app 'DSO138Studio.exe'
Require-Path $exe 'Installed application executable'

$deploy = Join-Path $QtRoot 'bin\windeployqt.exe'
Invoke-Native $deploy @('--release', '--no-translations', '--no-compiler-runtime',
    '--no-opengl-sw', '--no-system-d3d-compiler', '--no-system-dxc-compiler',
    '--skip-plugin-types', 'generic,iconengines,imageformats,networkinformation,styles,tls',
    '--dir', $app, $exe) 'Deploy required Qt DLLs and Windows platform plugin'

# Copy only VC143 runtime DLLs imported by the deployed app and its Qt dependencies.
# The lookup is restricted to the exact redist chosen by this vcvars environment.
$runtimeByName = @{}
Get-ChildItem -LiteralPath $RuntimeRedistDirectory -File -Filter '*.dll' | ForEach-Object {
    $runtimeByName[$_.Name] = $_.FullName
}
$pending = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $app -File -Recurse | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object { $pending.Enqueue($_.FullName) }
$examined = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$stagedRuntime = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
while ($pending.Count -gt 0) {
    $binary = $pending.Dequeue()
    if (-not $examined.Add($binary)) { continue }
    $dependencies = & $dumpbin /nologo /dependents $binary 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect native dependencies: $binary" }
    foreach ($line in $dependencies) {
        if ($line -match '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') {
            $name = $Matches[1]
            $availableRuntime = $runtimeByName.Keys | Where-Object { $_ -ieq $name } | Select-Object -First 1
            if ($availableRuntime) {
                if ($stagedRuntime.Add($availableRuntime)) {
                    $runtimeFile = Join-Path $app $availableRuntime
                    Copy-Item -LiteralPath $runtimeByName[$availableRuntime] -Destination $runtimeFile
                    $pending.Enqueue($runtimeFile)
                }
            } elseif ($name -match '^(?:concrt|msvcp|vcruntime|vccorlib)140.*\.dll$') {
                throw "Required VC143 runtime DLL is missing from the selected redistributable: $name"
            }
        }
    }
}
if ($stagedRuntime.Count -eq 0) { throw 'The app did not import any VC143 runtime files; verify the release build configuration.' }

Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $licenses 'THIRD_PARTY_NOTICES.md')

# Preserve upstream license text byte-for-byte; SBOMs are copied from this Qt installation.
$lgpl = Join-Path $licenses 'LGPL-3.0.txt'
Invoke-WebRequest -Uri 'https://www.gnu.org/licenses/lgpl-3.0.txt' -OutFile $lgpl
$gpl = Join-Path $licenses 'GPL-3.0.txt'
Invoke-WebRequest -Uri 'https://www.gnu.org/licenses/gpl-3.0.txt' -OutFile $gpl
$qtSbomRoot = Join-Path $QtRoot 'sbom'
foreach ($sbomName in @('qtbase-6.8.3.spdx.json', 'qtserialport-6.8.3.spdx.json')) {
    $sbomFile = Join-Path $qtSbomRoot $sbomName
    Require-Path $sbomFile "Qt SPDX SBOM $sbomName"
    Copy-Item -LiteralPath $sbomFile -Destination $sbom
}

$manifestPath = Join-Path $stage 'BUILD-MANIFEST.txt'
Set-Content -LiteralPath $manifestPath -Value "DSO138 Studio $version Windows x64 package`nQt 6.8.3; MSVC $env:VCToolsVersion; app-local VC143 runtime: $($stagedRuntime -join ', ')`n`nSHA-256  path" -Encoding utf8NoBOM
Get-ChildItem -LiteralPath $stage -File -Recurse | Where-Object { $_.FullName -ne $manifestPath } | Sort-Object FullName | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($stage, $_.FullName)
    $digest = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    Add-Content -LiteralPath $manifestPath -Value "$digest  $relative" -Encoding utf8NoBOM
}

$tempZip = Join-Path $work "$baseName.zip"
Add-Type -AssemblyName System.IO.Compression
$zipStream = [System.IO.File]::Open($tempZip, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$archive = [System.IO.Compression.ZipArchive]::new($zipStream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
try {
    $fixedTimestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object { [System.IO.Path]::GetRelativePath($stage, $_.FullName) } | ForEach-Object {
        $relative = [System.IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/')
        $entry = $archive.CreateEntry($relative, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $fixedTimestamp
        $inputStream = [System.IO.File]::OpenRead($_.FullName)
        $entryStream = $entry.Open()
        try { $inputStream.CopyTo($entryStream) }
        finally { $entryStream.Dispose(); $inputStream.Dispose() }
    }
}
finally { $archive.Dispose(); $zipStream.Dispose() }
$zipDigest = (Get-FileHash -LiteralPath $tempZip -Algorithm SHA256).Hash.ToLowerInvariant()
$tempHash = Join-Path $work "$baseName.zip.sha256"
Set-Content -LiteralPath $tempHash -Value "$zipDigest  $baseName.zip" -Encoding ascii
# File.Move without overwrite keeps any existing user artifact intact if another process races this check.
[System.IO.File]::Move($tempZip, $zipPath)
[System.IO.File]::Move($tempHash, $hashPath)
Write-Host "Package: $zipPath"
Write-Host "SHA-256: $hashPath"
Write-Host "Staging retained for inspection: $work"
