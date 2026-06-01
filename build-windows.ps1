# build-windows.ps1 - Simplified ASCII build script for obs-titles
# Validates prerequisites, configures CMake, builds, and installs the plugin.

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ScriptDir "build"
$VcpkgDir = "C:\vcpkg"
$VcpkgToolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
$ObsPluginRoot = "$env:APPDATA\obs-studio\plugins\obs-titles"
$ObsPluginBin = "$ObsPluginRoot\bin\64bit"
$ObsPluginData = "$ObsPluginRoot\data\locale"

Write-Host "=== Starting obs-titles build process ==="

# 1. Verify CMake and Visual Studio
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake and add it to your PATH."
    exit 1
}

# 2. Verify vcpkg toolchain
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain not found at $VcpkgToolchain. Please clone/setup vcpkg."
    exit 1
}

# 3. Detect OBS build dependencies
$ObsBuildDeps = Get-Item "C:\Users\menac\Desktop\obs-build-dependencies\plugin-deps-*" -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                Select-Object -First 1

if (-not $ObsBuildDeps) {
    Write-Error "Could not locate OBS build dependencies under C:\Users\menac\Desktop\obs-build-dependencies."
    exit 1
}
$ObsSdkDir = $ObsBuildDeps.FullName
Write-Host "Found OBS SDK: $ObsSdkDir"

# 4. Configure CMake
Write-Host "`n=== Configuring CMake ==="
$CmakeArgs = @(
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$($VcpkgToolchain.Replace('\', '/'))",
    "-DOBS_SDK_DIR=$($ObsSdkDir.Replace('\', '/'))"
)
& cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit 1
}

# 5. Build the Plugin
Write-Host "`n=== Building obs-titles ==="
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit 1
}

# 6. Copy build DLL and Locale to OBS plugins directory
Write-Host "`n=== Installing Plugin to OBS ==="
New-Item -ItemType Directory -Force -Path $ObsPluginBin | Out-Null
New-Item -ItemType Directory -Force -Path $ObsPluginData | Out-Null

$BuiltDll = Join-Path $BuildDir "obs-plugins\obs-titles.dll"
if (-not (Test-Path $BuiltDll)) {
    $BuiltDll = Join-Path $BuildDir "obs-plugins\64bit\obs-titles.dll"
}
if (-not (Test-Path $BuiltDll)) {
    $BuiltDll = Join-Path $BuildDir "Release\obs-titles.dll"
}

if (-not (Test-Path $BuiltDll)) {
    Write-Error "Could not find built obs-titles.dll."
    exit 1
}

Copy-Item -Force $BuiltDll $ObsPluginBin
Write-Host "Copied plugin DLL to: $ObsPluginBin"

$LocaleFile = Join-Path $ScriptDir "data\locale\en-US.ini"
if (Test-Path $LocaleFile) {
    Copy-Item -Force $LocaleFile $ObsPluginData
    Write-Host "Copied en-US.ini to: $ObsPluginData"
}

# 7. Copy vcpkg runtime DLL dependencies
Write-Host "`n=== Copying runtime DLLs from vcpkg ==="
$VcpkgBin = Join-Path $VcpkgDir "installed\x64-windows\bin"
$RuntimeDlls = @(
    "cairo.dll",
    "pango-1.0.dll",
    "pangocairo-1.0.dll",
    "pangoft2-1.0.dll",
    "glib-2.0.dll",
    "gobject-2.0.dll",
    "gmodule-2.0.dll",
    "gio-2.0.dll",
    "freetype.dll",
    "harfbuzz.dll",
    "libpng16.dll",
    "zlib1.dll",
    "pixman-1.dll",
    "ffi-8.dll",
    "intl.dll",
    "iconv-2.dll",
    "fontconfig.dll",
    "expat.dll",
    "brotlidec.dll",
    "brotlicommon.dll",
    "bz2.dll"
)

$CopiedCount = 0
foreach ($Dll in $RuntimeDlls) {
    $DllSrc = Join-Path $VcpkgBin $Dll
    if (Test-Path $DllSrc) {
        Copy-Item -Force $DllSrc $ObsPluginBin
        $CopiedCount++
    }
}
Write-Host "Copied $CopiedCount runtime DLL dependencies from vcpkg."
Write-Host "`n=== obs-titles built and installed successfully! ==="
