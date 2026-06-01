# build-windows.ps1 - Windows build helper for obs-titles.
# Validates prerequisites, configures CMake, builds, and installs the plugin.

param(
    [string]$BuildDir,
    [string]$VcpkgDir,
    [string]$ObsSdkDir,
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Architecture = "x64",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ScriptDir "build"
}
if ([string]::IsNullOrWhiteSpace($VcpkgDir)) {
    $VcpkgDir = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }
}
if ([string]::IsNullOrWhiteSpace($ObsSdkDir) -and $env:OBS_SDK_DIR) {
    $ObsSdkDir = $env:OBS_SDK_DIR
}
if ([string]::IsNullOrWhiteSpace($ObsSdkDir) -and $env:OBS_STUDIO_DIR) {
    $ObsSdkDir = $env:OBS_STUDIO_DIR
}

$VcpkgToolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
$ObsPluginRoot = Join-Path $env:APPDATA "obs-studio\plugins\obs-titles"
$ObsPluginBin = Join-Path $ObsPluginRoot "bin\64bit"
$ObsPluginData = Join-Path $ObsPluginRoot "data\locale"

Write-Host "=== Starting obs-titles build process ==="

# 1. Verify CMake and Visual Studio
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake and add it to your PATH."
    exit 1
}

# 2. Verify vcpkg toolchain
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain not found at $VcpkgToolchain. Pass -VcpkgDir or set VCPKG_ROOT."
    exit 1
}

# 3. Detect OBS SDK/build dependencies without machine-specific paths.
function Test-ObsSdkDir {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    $HasObsLib = (Test-Path (Join-Path $Path "lib\obs.lib")) -or
                 (Test-Path (Join-Path $Path "lib\obs\obs.lib"))
    $HasObsFrontendLib = (Test-Path (Join-Path $Path "lib\obs-frontend-api.lib")) -or
                         (Test-Path (Join-Path $Path "lib\obs\obs-frontend-api.lib"))
    $HasObsHeader = (Test-Path (Join-Path $Path "include\obs.h")) -or
                    (Test-Path (Join-Path $Path "include\obs\obs.h"))
    return ($HasObsLib -and $HasObsFrontendLib -and $HasObsHeader)
}

if (-not (Test-ObsSdkDir $ObsSdkDir)) {
    $CandidateRoots = @(
        (Join-Path $env:USERPROFILE "Desktop\obs-build-dependencies"),
        (Join-Path $env:USERPROFILE "Downloads\obs-build-dependencies"),
        (Join-Path $ScriptDir "obs-build-dependencies"),
        (Join-Path $env:ProgramFiles "obs-studio")
    )

    if ($env:ProgramW6432) {
        $CandidateRoots += (Join-Path $env:ProgramW6432 "obs-studio")
    }

    foreach ($Root in $CandidateRoots) {
        if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root)) {
            continue
        }

        $Candidates = @($Root)
        $Candidates += @(Get-ChildItem -Path $Root -Directory -Filter "plugin-deps-*" -ErrorAction SilentlyContinue |
                         Sort-Object Name -Descending |
                         ForEach-Object { $_.FullName })

        foreach ($Candidate in $Candidates) {
            if (Test-ObsSdkDir $Candidate) {
                $ObsSdkDir = $Candidate
                break
            }
        }

        if (Test-ObsSdkDir $ObsSdkDir) {
            break
        }
    }
}

if (-not (Test-ObsSdkDir $ObsSdkDir)) {
    Write-Error "Could not locate an OBS SDK/install tree. Pass -ObsSdkDir or set OBS_SDK_DIR/OBS_STUDIO_DIR."
    exit 1
}
Write-Host "Found OBS SDK: $ObsSdkDir"

# 4. Configure CMake
Write-Host "`n=== Configuring CMake ==="
$CmakeArgs = @(
    "-B", $BuildDir,
    "-G", $Generator,
    "-A", $Architecture,
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

if ($SkipInstall) {
    Write-Host "`n=== Build complete; skipping OBS install because -SkipInstall was set. ==="
    exit 0
}

# 6. Copy build DLL and Locale to OBS plugins directory
Write-Host "`n=== Installing Plugin to OBS ==="
New-Item -ItemType Directory -Force -Path $ObsPluginBin | Out-Null
New-Item -ItemType Directory -Force -Path $ObsPluginData | Out-Null

$BuiltDllCandidates = @(
    (Join-Path $BuildDir "obs-plugins\Release\obs-titles.dll"),
    (Join-Path $BuildDir "obs-plugins\obs-titles.dll"),
    (Join-Path $BuildDir "obs-plugins\64bit\obs-titles.dll"),
    (Join-Path $BuildDir "Release\obs-titles.dll")
)
$BuiltDll = $BuiltDllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $BuiltDll) {
    Write-Error "Could not find built obs-titles.dll. Checked: $($BuiltDllCandidates -join ', ')"
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
