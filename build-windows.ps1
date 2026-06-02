# build-windows.ps1 - Windows build helper for obs-titler-pro.
# Validates prerequisites, configures CMake, builds, and installs the plugin.

param(
    [string]$BuildDir,
    [string]$VcpkgDir,
    [string]$ObsSdkDir,
    [string]$InstallRoot,
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Architecture = "x64",
    [switch]$Clean,
    [switch]$RestoreTrackedSources,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

function Find-MatchingBraceIndex {
    param([string]$Text, [int]$OpenIndex)
    $depth = 0
    for ($i = $OpenIndex; $i -lt $Text.Length; $i++) {
        $ch = $Text[$i]
        if ($ch -eq '{') { $depth++ }
        elseif ($ch -eq '}') {
            $depth--
            if ($depth -eq 0) { return $i }
        }
    }
    return -1
}

function Remove-CppDefinitionRange {
    param([string]$Text, [int]$MatchIndex)
    $lineStart = $Text.LastIndexOf("`n", [Math]::Max(0, $MatchIndex - 1))
    if ($lineStart -lt 0) { $lineStart = 0 } else { $lineStart++ }
    $open = $Text.IndexOf('{', $MatchIndex)
    if ($open -lt 0) { return $Text }
    $close = Find-MatchingBraceIndex -Text $Text -OpenIndex $open
    if ($close -lt 0) { return $Text }
    $end = $close + 1
    while ($end -lt $Text.Length -and ($Text[$end] -eq "`r" -or $Text[$end] -eq "`n")) { $end++ }
    return $Text.Remove($lineStart, $end - $lineStart)
}

function Remove-DuplicateCppDefinitions {
    param([string]$File, [string[]]$Signatures, [switch]$KeepLast)
    if (-not (Test-Path $File)) { return }
    $text = Get-Content -Raw -Path $File
    $changed = $false
    foreach ($signature in $Signatures) {
        while ($true) {
            $first = $text.IndexOf($signature, [StringComparison]::Ordinal)
            if ($first -lt 0) { break }
            $next = $text.IndexOf($signature, $first + $signature.Length, [StringComparison]::Ordinal)
            if ($next -lt 0) { break }
            if ($KeepLast) {
                Write-Host "Removing earlier duplicate definition '$signature' from $File"
                $text = Remove-CppDefinitionRange -Text $text -MatchIndex $first
            } else {
                Write-Host "Removing later duplicate definition '$signature' from $File"
                $text = Remove-CppDefinitionRange -Text $text -MatchIndex $next
            }
            $changed = $true
        }
    }
    if ($changed) {
        Set-Content -Path $File -Value $text -NoNewline
    }
}

function Remove-ObsoleteCppDefinitions {
    param([string]$File, [string[]]$QualifiedNames)
    if (-not (Test-Path $File)) { return }
    $text = Get-Content -Raw -Path $File
    $changed = $false
    foreach ($name in $QualifiedNames) {
        while ($true) {
            $idx = $text.IndexOf($name, [StringComparison]::Ordinal)
            if ($idx -lt 0) { break }
            Write-Host "Removing obsolete definition '$name' from $File"
            $text = Remove-CppDefinitionRange -Text $text -MatchIndex $idx
            $changed = $true
        }
    }
    if ($changed) {
        Set-Content -Path $File -Value $text -NoNewline
    }
}


function Insert-BeforeMarker {
    param([string]$Text, [string]$Marker, [string]$Insertion)
    $idx = $Text.IndexOf($Marker, [StringComparison]::Ordinal)
    if ($idx -lt 0) { return $Text + "`n" + $Insertion }
    return $Text.Insert($idx, $Insertion + "`n")
}

function Ensure-TitleEditorCoreDefinitions {
    param([string]$File)
    if (-not (Test-Path $File)) { return }
    $text = Get-Content -Raw -Path $File
    $changed = $false

    if ($text.IndexOf("void TitleEditor::on_title_modified()", [StringComparison]::Ordinal) -lt 0) {
        Write-Host "Restoring missing TitleEditor::on_title_modified definition in $File"
        $definition = @'
void TitleEditor::on_title_modified()
{
    if (title_) setWindowTitle("OBS Titler Pro Editor  ·  modified");
    if (canvas_) canvas_->refresh_preview();
    if (title_props_) title_props_->set_title(title_);
    if (timeline_) timeline_->set_title(title_);
    push_undo_snapshot();
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
}
'@
        $text = Insert-BeforeMarker -Text $text -Marker "/* ══════════════════════════════════════════════════════════════════`n *  CanvasPreview" -Insertion $definition
        $changed = $true
    }

    $canvasDefinitions = ""
    if ($text.IndexOf("CanvasPreview::CanvasPreview(QWidget *parent)", [StringComparison]::Ordinal) -lt 0) {
        Write-Host "Restoring missing CanvasPreview constructor in $File"
        $canvasDefinitions += @'
CanvasPreview::CanvasPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 225);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background:#111;");
    setMouseTracking(true);
}

'@
    }
    if ($text.IndexOf("void CanvasPreview::set_title(std::shared_ptr<Title> t)", [StringComparison]::Ordinal) -lt 0) {
        Write-Host "Restoring missing CanvasPreview::set_title definition in $File"
        $canvasDefinitions += @'
void CanvasPreview::set_title(std::shared_ptr<Title> t)
{
    title_ = t; dirty_ = true; update();
}

'@
    }
    if (-not [string]::IsNullOrEmpty($canvasDefinitions)) {
        $text = Insert-BeforeMarker -Text $text -Marker "void CanvasPreview::set_playhead(double t)" -Insertion $canvasDefinitions.TrimEnd()
        $changed = $true
    }

    if ($changed) {
        Set-Content -Path $File -Value $text -NoNewline
    }
}

function Repair-KnownMergeArtifacts {
    param([string]$ScriptRoot)
    $dock = Join-Path $ScriptRoot "src\title-dock.cpp"
    $editor = Join-Path $ScriptRoot "src\title-editor.cpp"
    Remove-DuplicateCppDefinitions -File $dock -Signatures @(
        "void TitleDock::populate_exposed_text()",
        "void TitleDock::on_add_live_text_row()",
        "void TitleDock::on_move_live_text_row_up()",
        "void TitleDock::on_move_live_text_row_down()",
        "void TitleDock::select_title(const std::string &id)"
    )
    Remove-ObsoleteCppDefinitions -File $dock -QualifiedNames @(
        "TitleDock::create_template_title",
        "TitleDock::create_title_from_template"
    )
    # The known corrupted editor block was pasted before the valid coordinate
    # helpers, so keep the last helper body if a duplicate survived restore.
    Remove-DuplicateCppDefinitions -File $editor -KeepLast -Signatures @(
        "QPointF CanvasPreview::canvas_to_view(const QPointF &canvas_pt) const",
        "QPointF CanvasPreview::canvas_to_layer(const Layer &layer, const QPointF &canvas_pt) const",
        "QPointF CanvasPreview::layer_to_canvas(const Layer &layer, const QPointF &layer_pt) const"
    )
    Ensure-TitleEditorCoreDefinitions -File $editor
}


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
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    if ($env:OBS_PLUGINS_PATH) {
        $InstallRoot = $env:OBS_PLUGINS_PATH
    } elseif ($env:ProgramData) {
        $InstallRoot = Join-Path $env:ProgramData "obs-studio\plugins"
    } else {
        $InstallRoot = Join-Path $env:APPDATA "obs-studio\plugins"
    }
}

$PluginName = "obs-titler-pro"
$VcpkgToolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
$ObsArchDir = if ($Architecture -eq "Win32" -or $Architecture -eq "x86") { "32bit" } else { "64bit" }
$PluginDllName = "$PluginName.dll"
$ObsPluginRoot = Join-Path $InstallRoot $PluginName
$ObsPluginBin = Join-Path $ObsPluginRoot "bin\$ObsArchDir"
$ObsPluginData = Join-Path $ObsPluginRoot "data\locale"

Write-Host "=== Starting OBS Titler Pro build process ==="

if ($RestoreTrackedSources) {
    Write-Host "`n=== Restoring tracked source files from HEAD ==="
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Error "-RestoreTrackedSources requires git to be available in PATH."
        exit 1
    }
    $trackedSources = @(
        "src/title-dock.cpp",
        "src/title-dock.h",
        "src/title-editor.cpp",
        "src/title-editor.h"
    )
    & git -C $ScriptDir restore --source HEAD -- $trackedSources
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to restore tracked source files from HEAD."
        exit 1
    }
    Repair-KnownMergeArtifacts -ScriptRoot $ScriptDir
}


# MSVC is the authoritative duplicate-definition checker. Do not run an
# additional source scanner here: previous scanner versions produced false
# positives on valid CanvasPreview helper declarations/calls and blocked the
# Windows build before compilation could start.


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

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "`n=== Cleaning previous build directory ==="
    Write-Host "Removing: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

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
Write-Host "`n=== Building OBS Titler Pro ==="
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
Write-Host "Install root: $InstallRoot"
New-Item -ItemType Directory -Force -Path $ObsPluginBin | Out-Null
New-Item -ItemType Directory -Force -Path $ObsPluginData | Out-Null

$StagedPluginRoot = Join-Path $BuildDir $PluginName
$BuiltDllCandidates = @(
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\Release\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\RelWithDebInfo\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\Debug\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\Release\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\$ObsArchDir\$PluginDllName"),
    (Join-Path $BuildDir "Release\$PluginDllName"),
    (Join-Path $BuildDir "RelWithDebInfo\$PluginDllName"),
    (Join-Path $BuildDir "Debug\$PluginDllName")
)
$BuiltDll = $BuiltDllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $BuiltDll) {
    $RecursiveMatch = Get-ChildItem -Path $BuildDir -Filter $PluginDllName -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object @{ Expression = { if ($_.FullName -like "*\$PluginName\bin\$ObsArchDir*") { 0 } else { 1 } } }, FullName |
        Select-Object -First 1
    if ($RecursiveMatch) {
        $BuiltDll = $RecursiveMatch.FullName
    }
}

if (-not $BuiltDll) {
    Write-Error "Could not find built $PluginDllName. Checked known locations: $($BuiltDllCandidates -join ', '). Also searched recursively under $BuildDir."
    exit 1
}

Copy-Item -Force $BuiltDll $ObsPluginBin
Write-Host "Copied plugin DLL from: $BuiltDll"
Write-Host "Copied plugin DLL to: $ObsPluginBin"

$StagedData = Join-Path $StagedPluginRoot "data"
if (Test-Path $StagedData) {
    Copy-Item -Force -Recurse (Join-Path $StagedData "*") (Join-Path $ObsPluginRoot "data")
    Write-Host "Copied staged plugin data to: $(Join-Path $ObsPluginRoot 'data')"
} else {
    $LocaleFile = Join-Path $ScriptDir "data\locale\en-US.ini"
    if (Test-Path $LocaleFile) {
        Copy-Item -Force $LocaleFile $ObsPluginData
        Write-Host "Copied en-US.ini to: $ObsPluginData"
    }
}

# 7. Copy runtime DLL dependencies next to the plugin binary.
# A plugin can compile and still fail to load in OBS if Qt/Cairo/Pango DLLs are
# not beside obs-titler-pro.dll, so copy every vcpkg runtime DLL rather than trying
# to maintain a fragile hand-written dependency list.
Write-Host "`n=== Copying runtime DLL dependencies ==="
$RuntimeDllDirs = @()
$VcpkgTriplets = @($Architecture.ToLower())
if ($Architecture -eq "x64") {
    $VcpkgTriplets += "x64-windows"
} elseif ($Architecture -eq "Win32") {
    $VcpkgTriplets += "x86-windows"
}

foreach ($Triplet in ($VcpkgTriplets | Select-Object -Unique)) {
    $CandidateBin = Join-Path $VcpkgDir "installed\$Triplet\bin"
    if (Test-Path $CandidateBin) {
        $RuntimeDllDirs += $CandidateBin
    }
}

$CopiedCount = 0
foreach ($RuntimeDllDir in ($RuntimeDllDirs | Select-Object -Unique)) {
    Write-Host "Copying DLLs from: $RuntimeDllDir"
    $RuntimeDlls = Get-ChildItem -Path $RuntimeDllDir -Filter "*.dll" -File -ErrorAction SilentlyContinue
    foreach ($Dll in $RuntimeDlls) {
        Copy-Item -Force $Dll.FullName $ObsPluginBin
        $CopiedCount++
    }
}

if ($CopiedCount -eq 0) {
    Write-Warning "No vcpkg runtime DLLs were copied. If OBS says obs-titler-pro failed to load, check for missing Qt/Cairo/Pango DLLs in $ObsPluginBin."
} else {
    Write-Host "Copied $CopiedCount runtime DLL dependencies."
}

$ExpectedDlls = @(
    $PluginDllName,
    "cairo.dll",
    "pango-1.0.dll",
    "pangocairo-1.0.dll"
)
$MissingExpectedDlls = @()
foreach ($Dll in $ExpectedDlls) {
    if (-not (Test-Path (Join-Path $ObsPluginBin $Dll))) {
        $MissingExpectedDlls += $Dll
    }
}
if ($MissingExpectedDlls.Count -gt 0) {
    Write-Warning "The install folder is missing expected DLL(s): $($MissingExpectedDlls -join ', '). OBS may report that obs-titler-pro failed to load."
}

Write-Host "`nInstalled OBS plugin layout:"
Write-Host "  $ObsPluginRoot"
Write-Host "  $ObsPluginBin\$PluginDllName"
Write-Host "  $ObsPluginData\en-US.ini"
Write-Host "`n=== OBS Titler Pro built and installed successfully! ==="
