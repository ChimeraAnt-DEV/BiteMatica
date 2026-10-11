# Builds BiteMatica for Android (arm64-v8a) and packages it as a `.AntEgg`.
#
# Mirrors build.sh. Usage:
#   .\build.ps1                 # build + package
#   .\build.ps1 -Test           # also run the host test suite first
#   .\build.ps1 -Clean          # wipe build\ and dist\ first
#   .\build.ps1 -Ndk <path>     # explicit NDK, else auto-discovered
#   .\build.ps1 -PreloaderRoot <path>
param(
    [switch]$Test,
    [switch]$Clean,
    [string]$Ndk = "",
    [string]$PreloaderRoot = ""
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Abi = "arm64-v8a"
$BuildDir = Join-Path $Root "build\android-$Abi"
$DistDir = Join-Path $Root "dist"
if ([string]::IsNullOrWhiteSpace($PreloaderRoot)) {
    $PreloaderRoot = Join-Path $Root "third_party\preloader-android"
}
# The SDK's default branch; override with $env:PRELOADER_REF to pin a branch or commit.
$PreloaderRef = if ($env:PRELOADER_REF) { $env:PRELOADER_REF } else { "main" }

function Resolve-Ndk {
    param([string]$Explicit)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { return $Explicit }
    foreach ($candidate in @($env:ANDROID_NDK_HOME, $env:ANDROID_NDK_ROOT)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path (Join-Path $candidate "build\cmake\android.toolchain.cmake"))) {
            return $candidate
        }
    }
    foreach ($sdk in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ([string]::IsNullOrWhiteSpace($sdk)) { continue }
        $ndkRoot = Join-Path $sdk "ndk"
        if (Test-Path $ndkRoot) {
            $found = Get-ChildItem $ndkRoot -Directory | Sort-Object Name -Descending |
                Where-Object { Test-Path (Join-Path $_.FullName "build\cmake\android.toolchain.cmake") } |
                Select-Object -First 1
            if ($found) { return $found.FullName }
        }
        $bundle = Join-Path $sdk "ndk-bundle"
        if (Test-Path (Join-Path $bundle "build\cmake\android.toolchain.cmake")) { return $bundle }
    }
    throw "Android NDK not found. Pass -Ndk or set ANDROID_NDK_HOME/ANDROID_NDK_ROOT/ANDROID_HOME."
}

$Version = (Get-Content (Join-Path $Root "packaging\egg.json") | ConvertFrom-Json).version

if ($Clean) {
    foreach ($dir in @((Join-Path $Root "build"), $DistDir)) {
        if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    }
}

$NdkPath = Resolve-Ndk $Ndk
$Toolchain = Join-Path $NdkPath "build\cmake\android.toolchain.cmake"
Write-Host "==> NDK: $NdkPath"

if (-not (Test-Path (Join-Path $PreloaderRoot "include\pl\Mod.hpp"))) {
    Write-Host "==> Cloning preloader-android ($PreloaderRef) into third_party\"
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "third_party") | Out-Null
    git clone --depth 1 --branch $PreloaderRef `
        https://github.com/ChimeraAnt-DEV/preloader-android.git $PreloaderRoot
}
Write-Host "==> Preloader: $PreloaderRoot"

if ($Test) {
    Write-Host "==> Host tests"
    $hostBuild = Join-Path $Root "build\host"
    python3 (Join-Path $Root "test\make_fixtures.py") (Join-Path $Root "test\fixtures")
    cmake -S $Root -B $hostBuild -DCMAKE_BUILD_TYPE=Release
    cmake --build $hostBuild --target bitematica_tests bitematica_world_tests
    & (Join-Path $hostBuild "bitematica_tests.exe")
    & (Join-Path $hostBuild "bitematica_world_tests.exe")
}

Write-Host "==> Configuring Android build"
cmake -S $Root -B $BuildDir `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-24" `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DPRELOADER_ANDROID_ROOT=$PreloaderRoot"

Write-Host "==> Building libbitematica.so"
cmake --build $BuildDir --target bitematica --parallel

$library = Join-Path $BuildDir "out\$Abi\libbitematica.so"
if (-not (Test-Path $library)) { throw "expected library not produced: $library" }

Write-Host "==> Packaging .AntEgg"
python3 (Join-Path $Root "tools\pack_antegg.py") `
    --manifest (Join-Path $Root "packaging\egg.json") `
    --preloader-manifest (Join-Path $Root "packaging\manifest.json") `
    --icon (Join-Path $Root "assets\icon\icon.png") `
    --library $library `
    --entry bin/libbitematica.so `
    --config (Join-Path $Root "packaging\config") `
    --schematics (Join-Path $Root "assets\schematics") `
    --out (Join-Path $DistDir "BiteMatica-$Version.antegg")

Write-Host "==> Done: $DistDir\BiteMatica-$Version.antegg"
