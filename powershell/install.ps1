<#
.SYNOPSIS
    Installs the Clear Spotify client mod on Windows.
.DESCRIPTION
    All-in-one installer that handles everything from scratch:
    - Installs spicetify automatically if not present
    - Kills Spotify if running
    - Detects and fully removes any previous Clear installation
    - Downloads fresh theme files (user.css, color.ini, theme.js)
    - Builds/downloads the audio visualizer daemon (vis-capture)
    - Configures and applies the theme
    - Launches Spotify
.NOTES
    Run in PowerShell (admin not required unless Spotify is installed system-wide).
    Requires Spotify Desktop to already be installed and logged in.
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

function Exit-WithError {
    Write-Host ""
    Read-Host "Press Enter to close"
    exit 1
}

# ── 1. Ensure spicetify is installed ─────────────────────────────────────────
Write-Step "Checking spicetify installation"
$spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
if (-not $spicetifyCmd) {
    Write-Warn "spicetify not found — installing automatically..."
    try {
        # Disable strict mode — the spicetify installer uses uninitialized
        # variables ($v) that blow up under Set-StrictMode -Version Latest.
        Set-StrictMode -Off
        $installerContent = (Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/spicetify/cli/main/install.ps1").Content
        # Auto-accept Spicetify Marketplace installation (choice 0 = Yes)
        $installerContent = $installerContent -replace '\$choice\s*=\s*\$Host\.UI\.PromptForChoice\([^)]*Marketplace[^)]*\)', '$choice = 0'
        Invoke-Expression $installerContent
    } catch {
        Write-Host "`n   Failed to install spicetify: $_" -ForegroundColor Red
        Write-Host "   Install it manually: https://spicetify.app" -ForegroundColor Red
        Exit-WithError
    } finally {
        Set-StrictMode -Version Latest
    }

    # Refresh PATH so we can find the new binary
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "User") + ";" + [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
    $spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
    if (-not $spicetifyCmd) {
        # Try the default install location directly
        $defaultSpicetify = Join-Path $env:LOCALAPPDATA "spicetify\spicetify.exe"
        if (Test-Path $defaultSpicetify) {
            $env:PATH = (Split-Path $defaultSpicetify) + ";" + $env:PATH
            $spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
        }
    }
    if (-not $spicetifyCmd) {
        Write-Host "`n   spicetify installed but not found in PATH." -ForegroundColor Red
        Write-Host "   Close and reopen PowerShell, then run this script again." -ForegroundColor Yellow
        Exit-WithError
    }
    Write-Ok "spicetify installed successfully"
} else {
    Write-Ok "spicetify found at $($spicetifyCmd.Source)"
}

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
    # spicetify was just installed — run it once to generate config
    Write-Warn "Config directory not found — initializing spicetify..."
    try { & spicetify 2>$null } catch {}
    try {
        $pathOutput = & spicetify path -c 2>$null
        if ($pathOutput) { $spicetifyDir = Split-Path $pathOutput }
    } catch {}
    if (-not $spicetifyDir -or -not (Test-Path $spicetifyDir)) {
        Write-Host "`n   Could not find spicetify config directory." -ForegroundColor Red
        Write-Host "   Make sure Spotify Desktop is installed and has been opened at least once." -ForegroundColor Red
        Exit-WithError
    }
}
Write-Ok "Config directory: $spicetifyDir"

$themesDir = Join-Path $spicetifyDir "Themes"
$clearDir  = Join-Path $themesDir $themeName

# ── 4. Detect and fully remove previous installation ────────────────────────
Write-Step "Checking for existing Clear installation"

# Check if Clear was previously applied
$previousTheme = ""
try { $previousTheme = & spicetify config current_theme 2>$null } catch {}

$clearExists = (Test-Path $clearDir) -or ($previousTheme -match $themeName)
if ($clearExists) {
    Write-Warn "Existing Clear installation detected — removing completely"

    # Restore Spotify to vanilla (undoes any previous spicetify apply)
    try {
        & spicetify restore
        Write-Ok "Restored Spotify to vanilla state"
    } catch {
        Write-Warn "spicetify restore returned non-zero (may already be vanilla)"
    }

    # Nuke the entire theme directory
    if (Test-Path $clearDir) {
        Remove-Item -Recurse -Force $clearDir
        Write-Ok "Removed $clearDir"
    }

    # Reset spicetify config to defaults
    try { & spicetify config current_theme "" } catch {}
    try { & spicetify config inject_theme_js 0 } catch {}
    try { & spicetify config color_scheme "" } catch {}
    try { & spicetify config extensions "" } catch {}
    Write-Ok "Reset spicetify configuration"
} else {
    Write-Ok "No previous $themeName installation found"
}

# Make sure config exists
$configIni = Join-Path $spicetifyDir "config-xpui.ini"
if (-not (Test-Path $configIni)) {
    Write-Warn "No config-xpui.ini found — running spicetify to generate it"
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
        Exit-WithError
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
        Exit-WithError
    }
}

# ── 8. Build and install audio visualizer daemon ─────────────────────────────
Write-Step "Setting up audio visualizer daemon"

# Permanent install location (survives theme folder wipes)
$visDir = Join-Path $env:LOCALAPPDATA "ClearVis"
New-Item -ItemType Directory -Force -Path $visDir | Out-Null
$visBin = Join-Path $visDir "vis-capture.exe"

# Remove old scheduled task first so it can't restart the process
$taskName = "ClearVisCapture"
try {
    $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existingTask) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        Write-Ok "Removed old '$taskName' scheduled task"
    }
} catch {}

# Kill any existing vis-capture so we can replace the binary and free the port
$oldProcs = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
if ($oldProcs) {
    $oldProcs | Stop-Process -Force
    Start-Sleep -Seconds 2
    # Verify process actually exited
    $still = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
    if ($still) {
        Start-Sleep -Seconds 3
    }
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
        # Set up auto-start — try Task Scheduler first (hidden, no console flash),
        # fall back to startup shortcut if Task Scheduler fails.
        $autoStartOk = $false
        $startupDir = [System.IO.Path]::Combine($env:APPDATA, "Microsoft\Windows\Start Menu\Programs\Startup")

        try {
            $action  = New-ScheduledTaskAction -Execute $visBin
            $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
            $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
            $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

            Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Description "Clear Spotify Visualizer Audio Bridge" | Out-Null
            Write-Ok "Registered '$taskName' scheduled task (auto-starts on login)"
            $autoStartOk = $true

            # Remove legacy startup shortcut since we use Task Scheduler now
            $legacyShortcut = Join-Path $startupDir "ClearVis.lnk"
            if (Test-Path $legacyShortcut) {
                Remove-Item $legacyShortcut -Force
                Write-Ok "Removed legacy startup shortcut"
            }
        } catch {
            Write-Warn "Task Scheduler failed: $_ — falling back to startup shortcut"
        }

        # Fallback: create a startup shortcut if Task Scheduler failed
        if (-not $autoStartOk) {
            try {
                $shortcutPath = Join-Path $startupDir "ClearVis.lnk"
                $shell = New-Object -ComObject WScript.Shell
                $shortcut = $shell.CreateShortcut($shortcutPath)
                $shortcut.TargetPath = $visBin
                $shortcut.WorkingDirectory = $visDir
                $shortcut.WindowStyle = 7  # Minimized
                $shortcut.Description = "Clear Spotify Visualizer Audio Bridge"
                $shortcut.Save()
                Write-Ok "Created startup shortcut (auto-starts on login)"
            } catch {
                Write-Warn "Could not set up auto-start: $_"
                Write-Warn "You can run vis-capture.exe manually from: $visDir"
            }
        }

        # Start it right now
        Start-Process -FilePath $visBin -WindowStyle Hidden
        Start-Sleep -Seconds 1
        $running = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
        if ($running) {
            Write-Ok "Audio visualizer daemon is running"
        } else {
            Write-Warn "Daemon may not have started — try running vis-capture.exe manually from: $visDir"
        }
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

Write-Host "`n   Clear installed successfully!" -ForegroundColor Green
Write-Host "   Enjoy your clean Spotify experience." -ForegroundColor White
Write-Host ""
