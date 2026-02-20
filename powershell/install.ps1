<#
.SYNOPSIS
    Installs the Clear Spotify theme for spicetify on Windows.
.DESCRIPTION
    - Kills Spotify if running
    - Detects spicetify config directory
    - Clears any previous theme and custom code snippets
    - Downloads and installs Clear theme files (user.css, color.ini, theme.js)
    - Builds/downloads and installs the audio visualizer daemon (vis-capture)
    - Configures spicetify to use the Clear theme
    - Applies the theme
    - Launches Spotify
.NOTES
    Run in PowerShell (admin not required unless Spotify is installed system-wide).
    Requires spicetify to already be installed: https://spicetify.app
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repo = "wktkow/clear-spotify-theme"
$branch = "main"
$baseUrl = "https://raw.githubusercontent.com/$repo/$branch"
$themeFiles = @("user.css", "color.ini", "theme.js")
$themeName = "Clear"

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "   $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "   $msg" -ForegroundColor Yellow }

# ── 1. Check spicetify is installed ──────────────────────────────────────────
Write-Step "Checking spicetify installation"
$spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
if (-not $spicetifyCmd) {
    Write-Host "`n   spicetify is not installed or not in PATH." -ForegroundColor Red
    Write-Host "   Install it first: https://spicetify.app" -ForegroundColor Red
    Write-Host "   Run this in PowerShell:" -ForegroundColor Yellow
    Write-Host '   iwr -useb https://raw.githubusercontent.com/spicetify/cli/main/install.ps1 | iex' -ForegroundColor White
    exit 1
}
Write-Ok "spicetify found at $($spicetifyCmd.Source)"

# ── 2. Kill Spotify ─────────────────────────────────────────────────────────
Write-Step "Stopping Spotify"
$procs = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue
if ($procs) {
    $procs | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Ok "Spotify stopped"
} else {
    Write-Ok "Spotify was not running"
}

# ── 3. Find spicetify config directory ───────────────────────────────────────
Write-Step "Locating spicetify config"
$spicetifyDir = $null

# Try the spicetify path command first
try {
    $pathOutput = & spicetify path -c 2>$null
    if ($pathOutput -and (Test-Path (Split-Path $pathOutput))) {
        $spicetifyDir = Split-Path $pathOutput
    }
} catch {}

# Fallback to common locations
if (-not $spicetifyDir) {
    $candidates = @(
        "$env:APPDATA\spicetify",
        "$env:USERPROFILE\.spicetify"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $spicetifyDir = $c; break }
    }
}

if (-not $spicetifyDir -or -not (Test-Path $spicetifyDir)) {
    Write-Host "`n   Could not find spicetify config directory." -ForegroundColor Red
    Write-Host "   Make sure spicetify is installed and has been run at least once." -ForegroundColor Red
    exit 1
}
Write-Ok "Config directory: $spicetifyDir"

$themesDir = Join-Path $spicetifyDir "Themes"
$clearDir  = Join-Path $themesDir $themeName

# ── 4. Clear previous theme and code snippets ───────────────────────────────
Write-Step "Cleaning previous installation"

# Remove old Clear theme folder if it exists
if (Test-Path $clearDir) {
    Remove-Item -Recurse -Force $clearDir
    Write-Ok "Removed old $themeName theme folder"
} else {
    Write-Ok "No previous $themeName theme found"
}

# Clear custom extensions/apps that spicetify may have injected from old themes
$configIni = Join-Path $spicetifyDir "config-xpui.ini"
if (Test-Path $configIni) {
    # Read current config to clear stale snippet references
    $configContent = Get-Content $configIni -Raw

    # Reset extensions and custom_apps lines that reference old theme JS
    # We don't nuke all extensions — just clear any theme-specific ones
    # spicetify config will set the correct ones below
    Write-Ok "Config file found, will be updated by spicetify config"
} else {
    Write-Warn "No config-xpui.ini found — spicetify may not have been initialized"
    Write-Warn "Running spicetify once to generate config..."
    try { & spicetify 2>$null } catch {}
}

# ── 5. Download theme files ─────────────────────────────────────────────────
Write-Step "Downloading $themeName theme files"
New-Item -ItemType Directory -Force -Path $clearDir | Out-Null

foreach ($file in $themeFiles) {
    $url  = "$baseUrl/$file"
    $dest = Join-Path $clearDir $file
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
        Write-Ok "$file downloaded"
    } catch {
        Write-Host "   Failed to download $file from $url" -ForegroundColor Red
        Write-Host "   Error: $_" -ForegroundColor Red
        exit 1
    }
}

# ── 6. Configure spicetify ──────────────────────────────────────────────────
Write-Step "Configuring spicetify"

# Set Clear as the active theme
& spicetify config current_theme $themeName
Write-Ok "Theme set to $themeName"

# Enable theme JS injection
& spicetify config inject_theme_js 1
Write-Ok "Theme JS injection enabled"

# Clear any leftover custom color scheme (use default from color.ini)
& spicetify config color_scheme ""
Write-Ok "Color scheme reset to default"

# ── 7. Apply ────────────────────────────────────────────────────────────────
Write-Step "Applying theme"
try {
    & spicetify backup apply
    Write-Ok "Theme applied successfully"
} catch {
    Write-Warn "spicetify backup apply failed, trying apply only..."
    try {
        & spicetify apply
        Write-Ok "Theme applied successfully"
    } catch {
        Write-Host "   Failed to apply theme: $_" -ForegroundColor Red
        Write-Host "   Try running 'spicetify restore backup apply' manually." -ForegroundColor Yellow
        exit 1
    }
}

# ── 8. Build and install audio visualizer daemon ─────────────────────────────
Write-Step "Setting up audio visualizer daemon"

# Permanent install location (survives theme folder wipes)
$visDir = Join-Path $env:LOCALAPPDATA "ClearVis"
New-Item -ItemType Directory -Force -Path $visDir | Out-Null
$visBin = Join-Path $visDir "vis-capture.exe"

# Kill any existing vis-capture so we can replace the binary and free the port
$oldProcs = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
if ($oldProcs) {
    $oldProcs | Stop-Process -Force
    Start-Sleep -Seconds 1
    Write-Ok "Stopped existing vis-capture"
}

# Download native source files to a temp build dir
$buildDir = Join-Path $env:TEMP "clearvis-build"
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
$buildCommon = Join-Path $buildDir "native\common"
$buildWin = Join-Path $buildDir "native\windows"
New-Item -ItemType Directory -Force -Path $buildCommon | Out-Null
New-Item -ItemType Directory -Force -Path $buildWin | Out-Null

$nativeFiles = @(
    "native/common/protocol.h",
    "native/common/fft.h",
    "native/common/ws_server.h",
    "native/windows/main.cpp",
    "native/windows/build.bat"
)

$nativeOk = $true
foreach ($nf in $nativeFiles) {
    $url  = "$baseUrl/$nf"
    $dest = Join-Path $buildDir $nf
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    } catch {
        Write-Warn "Failed to download $nf — skipping daemon setup"
        $nativeOk = $false
        break
    }
}

if ($nativeOk) {
    # Try to build with cl.exe if available
    $clCmd = Get-Command cl -ErrorAction SilentlyContinue
    $built = $false

    if ($clCmd) {
        Write-Ok "Found cl.exe, building vis-capture..."
        Push-Location $buildWin
        try {
            & cl /O2 /EHsc /std:c++17 /I"$buildCommon" /Fe:"$visBin" main.cpp ole32.lib ws2_32.lib
            if ($LASTEXITCODE -eq 0 -and (Test-Path $visBin)) {
                Write-Ok "vis-capture.exe built successfully"
                $built = $true
            } else {
                Write-Warn "cl.exe build failed — see errors above"
            }
        } catch {
            Write-Warn "cl.exe build failed: $_"
        }
        Pop-Location
    }

    if (-not $built) {
        Write-Warn "Trying to download pre-built binary..."
        $relUrl = "https://github.com/$repo/releases/latest/download/vis-capture.exe"
        try {
            Invoke-WebRequest -Uri $relUrl -OutFile $visBin -UseBasicParsing
            if (Test-Path $visBin) {
                Write-Ok "Downloaded pre-built vis-capture.exe"
                $built = $true
            }
        } catch {
            Write-Warn "Could not download pre-built binary"
            Write-Warn "Install Visual Studio Build Tools and re-run, or build manually with build.bat"
        }
    }

    if ($built -and (Test-Path $visBin)) {
        # Use Task Scheduler for hidden auto-start (no console window flash)
        $taskName = "ClearVisCapture"
        $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($existingTask) {
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        }

        $action  = New-ScheduledTaskAction -Execute $visBin
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
        $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

        Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Description "Clear Spotify Visualizer Audio Bridge" | Out-Null
        Write-Ok "Registered '$taskName' scheduled task (auto-starts on login)"

        # Remove legacy startup shortcut if present
        $legacyShortcut = Join-Path ([System.IO.Path]::Combine($env:APPDATA, "Microsoft\Windows\Start Menu\Programs\Startup")) "ClearVis.lnk"
        if (Test-Path $legacyShortcut) {
            Remove-Item $legacyShortcut -Force
            Write-Ok "Removed legacy startup shortcut"
        }

        # Start it right now
        Start-Process -FilePath $visBin -WindowStyle Hidden
        Write-Ok "vis-capture started"
    }
}

# Clean up build dir
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }

# ── 9. Launch Spotify ────────────────────────────────────────────────────────
Write-Step "Launching Spotify"
$spotifyExe = $null

# Check common install locations
$candidates = @(
    "$env:APPDATA\Spotify\Spotify.exe",
    "$env:LOCALAPPDATA\Microsoft\WindowsApps\Spotify.exe",
    "${env:ProgramFiles}\WindowsApps\SpotifyAB.SpotifyMusic_*\Spotify.exe",
    "${env:ProgramFiles(x86)}\Spotify\Spotify.exe"
)

foreach ($c in $candidates) {
    $resolved = Resolve-Path $c -ErrorAction SilentlyContinue
    if ($resolved) { $spotifyExe = $resolved.Path; break }
}

if ($spotifyExe) {
    Start-Process $spotifyExe
    Write-Ok "Spotify launched"
} else {
    # Try Start-Process with just the name (relies on PATH or shell association)
    try {
        Start-Process "spotify"
        Write-Ok "Spotify launched"
    } catch {
        Write-Warn "Could not auto-launch Spotify — please start it manually"
    }
}

Write-Host "`n   Clear theme installed successfully!" -ForegroundColor Green
Write-Host "   Enjoy your clean Spotify experience." -ForegroundColor White
Write-Host ""
