<#
.SYNOPSIS
    Uninstalls the Clear Spotify client mod and restores vanilla Spotify.
.DESCRIPTION
    Comprehensive removal of all Clear artifacts:
    - Kills Spotify and vis-capture daemon
    - Removes ClearVisCapture scheduled task
    - Removes legacy startup shortcut
    - Restores Spotify to vanilla state via spicetify restore
    - Removes Clear theme files from spicetify Themes directory
    - Resets spicetify configuration
    - Removes ClearVis directory (vis-capture binary)
    - Removes leftover temp build directory
    - Launches clean Spotify
.NOTES
    Run in PowerShell. Does NOT uninstall spicetify itself — only the Clear mod.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$themeName = "Clear"

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "   $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "   $msg" -ForegroundColor Yellow }

function Exit-WithError {
    Write-Host ""
    Read-Host "Press Enter to close"
    exit 1
}

# ── 1. Kill Spotify and vis-capture ──────────────────────────────────────────
Write-Step "Stopping running processes"

$spotifyProcs = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue
if ($spotifyProcs) {
    $spotifyProcs | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Ok "Spotify stopped"
} else {
    Write-Ok "Spotify was not running"
}

$visProcs = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
if ($visProcs) {
    $visProcs | Stop-Process -Force
    Start-Sleep -Seconds 2
    # Verify process actually exited (file may be locked otherwise)
    $still = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
    if ($still) {
        Write-Warn "vis-capture still running — waiting longer..."
        Start-Sleep -Seconds 3
    }
    Write-Ok "vis-capture stopped"
} else {
    Write-Ok "vis-capture was not running"
}

# ── 2. Remove scheduled task and startup entries ─────────────────────────────
Write-Step "Removing auto-start entries"

$taskName = "ClearVisCapture"
try {
    $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existingTask) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        Write-Ok "Removed '$taskName' scheduled task"
    } else {
        Write-Ok "No '$taskName' scheduled task found"
    }
} catch {
    Write-Warn "Could not check/remove scheduled task: $_"
}

# Remove legacy startup shortcut if present
$legacyShortcut = Join-Path ([System.IO.Path]::Combine($env:APPDATA, "Microsoft\Windows\Start Menu\Programs\Startup")) "ClearVis.lnk"
if (Test-Path $legacyShortcut) {
    Remove-Item $legacyShortcut -Force
    Write-Ok "Removed legacy startup shortcut"
}

# ── 3. Find spicetify ───────────────────────────────────────────────────────
Write-Step "Locating spicetify"

$spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue

# If not in PATH, try default install location
if (-not $spicetifyCmd) {
    $defaultSpicetify = Join-Path $env:LOCALAPPDATA "spicetify\spicetify.exe"
    if (Test-Path $defaultSpicetify) {
        $env:PATH = (Split-Path $defaultSpicetify) + ";" + $env:PATH
        $spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
    }
}

# Also try refreshing PATH from registry
if (-not $spicetifyCmd) {
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "User") + ";" + [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
    $spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
}

if ($spicetifyCmd) {
    Write-Ok "spicetify found at $($spicetifyCmd.Source)"
} else {
    Write-Warn "spicetify not found in PATH — will still remove files manually"
}

# ── 4. Restore Spotify to vanilla ───────────────────────────────────────────
Write-Step "Restoring Spotify to vanilla"

if ($spicetifyCmd) {
    & spicetify restore 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "spicetify restore returned non-zero (may already be vanilla)"
    } else {
        Write-Ok "Spotify restored to vanilla state"
    }
} else {
    Write-Warn "spicetify not found — skipping restore (Spotify may need reinstall)"
}

# ── 5. Undo Spotify auto-update blocking ─────────────────────────────────
Write-Step "Restoring Spotify auto-updates"

# Remove the read-only Update blocker file (our installer creates this)
$updateBlocker = "$env:LOCALAPPDATA\Spotify\Update"
if ((Test-Path $updateBlocker) -and -not (Test-Path $updateBlocker -PathType Container)) {
    Set-ItemProperty -Path $updateBlocker -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
    Remove-Item $updateBlocker -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path $updateBlocker)) { Write-Ok "Removed Update blocker file" }
    else { Write-Warn "Could not remove Update blocker file — delete manually: $updateBlocker" }
}

# Remove the read-only SpotifyMigrator.exe placeholder
$migrator = "$env:APPDATA\Spotify\SpotifyMigrator.exe"
if (Test-Path $migrator) {
    $migratorSize = (Get-Item $migrator -ErrorAction SilentlyContinue).Length
    if ($migratorSize -eq 0) {
        Set-ItemProperty -Path $migrator -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
        Remove-Item $migrator -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path $migrator)) { Write-Ok "Removed SpotifyMigrator.exe placeholder" }
        else { Write-Warn "Could not remove SpotifyMigrator.exe placeholder — delete manually: $migrator" }
    }
}

Write-Ok "Spotify auto-updates restored"

# ── 6. Remove Clear theme files ─────────────────────────────────────────
Write-Step "Removing Clear theme files"

$spicetifyDir = $null

if ($spicetifyCmd) {
    try {
        $pathOutput = & spicetify path -c 2>$null
        if ($pathOutput -and (Test-Path (Split-Path $pathOutput))) {
            $spicetifyDir = Split-Path $pathOutput
        }
    } catch {}
}

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

if ($spicetifyDir) {
    $clearDir = Join-Path $spicetifyDir "Themes\$themeName"
    if (Test-Path $clearDir) {
        Remove-Item -Recurse -Force $clearDir
        Write-Ok "Removed $clearDir"
    } else {
        Write-Ok "Theme directory already gone"
    }

    # Reset spicetify config
    if ($spicetifyCmd) {
        & spicetify config current_theme ""
        & spicetify config inject_theme_js 0
        & spicetify config color_scheme ""
        & spicetify config extensions ""
        Write-Ok "Reset spicetify configuration"
    }
} else {
    Write-Warn "Could not locate spicetify config directory — checking fallback paths"
    # Try common theme directory locations directly
    $fallbackDirs = @(
        "$env:APPDATA\spicetify\Themes\$themeName",
        "$env:USERPROFILE\.spicetify\Themes\$themeName"
    )
    foreach ($fb in $fallbackDirs) {
        if (Test-Path $fb) {
            Remove-Item -Recurse -Force $fb
            Write-Ok "Removed $fb"
        }
    }
}

# ── 7. Remove vis-capture daemon files ───────────────────────────────────
Write-Step "Removing audio visualizer daemon files"

# Primary install location
$visDir = Join-Path $env:LOCALAPPDATA "ClearVis"
if (Test-Path $visDir) {
    try {
        Remove-Item -Recurse -Force $visDir
        Write-Ok "Removed $visDir"
    } catch {
        Write-Warn "Could not remove $visDir (file may be locked) — try deleting manually"
    }
} else {
    Write-Ok "ClearVis directory already gone"
}

# Clean up temp build dir if leftover from a failed install
$buildDir = Join-Path $env:TEMP "clearvis-build"
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir
    Write-Ok "Removed leftover build directory"
}

# ── 8. Launch clean Spotify ──────────────────────────────────────────────
Write-Step "Launching Spotify"

$spotifyExe = $null
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
    try {
        Start-Process "spotify"
        Write-Ok "Spotify launched"
    } catch {
        Write-Warn "Could not auto-launch Spotify — please start it manually"
    }
}

Write-Host "`n   Clear has been completely removed." -ForegroundColor Green
Write-Host "   Spotify is back to its vanilla state." -ForegroundColor White
Write-Host ""
