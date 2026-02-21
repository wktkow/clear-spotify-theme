<#
.SYNOPSIS
    Installs the Clear Spotify client mod on Windows (version-locked).
.DESCRIPTION
    Complete installer that enforces specific locked versions:
    - Removes ALL existing Spotify installations (Desktop, Store)
    - Removes ALL existing spicetify installations
    - Installs locked Spotify version 1.2.74.477.g3be53afe
    - Blocks Spotify auto-updates
    - Installs locked spicetify v2.42.11
    - Downloads and applies the Clear theme
    - Builds/downloads the audio visualizer daemon (vis-capture)
    - Launches Spotify
.NOTES
    Run in PowerShell. Requires internet connection.
    Spotify Desktop (not Store) will be installed to %APPDATA%\Spotify.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── LOCKED VERSIONS — DO NOT CHANGE ─────────────────────────────────────────
$spicetifyVersion = "2.42.11"
$spotifyVersion = "1.2.74.477.g3be53afe"
$spicetifyZipUrl = "https://github.com/spicetify/cli/releases/download/v${spicetifyVersion}/spicetify-${spicetifyVersion}-windows-x64.zip"
$spotifyInstallerUrl = "https://github.com/wktkow/clear-spotify-client/raw/main/installers/spotify_installer-${spotifyVersion}.exe"
$spotifyInstallerUrlCdn = "https://upgrade.scdn.co/upgrade/client/win32-x86_64/spotify_installer-${spotifyVersion}-1297.exe"
$spotifyInstallerSha256 = "CA3CE1B29E601123E1F4501D8A76310A00C8578D3A94935D1C618B2BF6AFDB42"
# ─────────────────────────────────────────────────────────────────────────────

$repo = "wktkow/clear-spotify-client"
$branch = "main"
$baseUrl = "https://raw.githubusercontent.com/$repo/$branch"
$themeFiles = @("user.css", "color.ini", "theme.js")
$themeName = "Clear"

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "   $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "   $msg" -ForegroundColor Yellow }

function Exit-WithError {
    param([string]$msg)
    if ($msg) { Write-Host "`n   $msg" -ForegroundColor Red }
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
    Write-Ok "vis-capture stopped"
}

# ── 2. Remove ALL existing Spotify installations ────────────────────────────
Write-Step "Removing all existing Spotify installations"

# Remove Microsoft Store version
try {
    $storeApp = Get-AppxPackage -Name "*SpotifyAB*" -ErrorAction SilentlyContinue
    if ($storeApp) {
        Write-Warn "Microsoft Store Spotify found — removing"
        $storeApp | Remove-AppxPackage -ErrorAction SilentlyContinue
        Write-Ok "Store Spotify removed"
    }
} catch {
    Write-Warn "Could not check/remove Store Spotify: $_"
}

# Remove desktop version
$spotifyAppData = "$env:APPDATA\Spotify"
$spotifyLocalData = "$env:LOCALAPPDATA\Spotify"

if (Test-Path "$spotifyAppData\Spotify.exe") {
    Write-Warn "Desktop Spotify found — removing"
    try {
        Start-Process -FilePath "$spotifyAppData\Spotify.exe" -ArgumentList "/uninstall","/S" -Wait -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
    } catch {}
}

foreach ($dir in @($spotifyAppData, $spotifyLocalData)) {
    if (Test-Path $dir) {
        Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
        Write-Ok "Removed $dir"
    }
}

# Try winget uninstall as a catch-all
if (Get-Command winget -ErrorAction SilentlyContinue) {
    try {
        & winget uninstall --id Spotify.Spotify --silent --accept-source-agreements 2>$null
    } catch {}
}

Write-Ok "All existing Spotify installations removed"

# ── 3. Remove ALL existing spicetify ────────────────────────────────────────
Write-Step "Removing all existing spicetify installations"

# Remove ClearVisCapture scheduled task
$taskName = "ClearVisCapture"
try {
    $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existingTask) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        Write-Ok "Removed '$taskName' scheduled task"
    }
} catch {}

# Remove legacy startup shortcut
$legacyShortcut = Join-Path ([System.IO.Path]::Combine($env:APPDATA, "Microsoft\Windows\Start Menu\Programs\Startup")) "ClearVis.lnk"
if (Test-Path $legacyShortcut) {
    Remove-Item $legacyShortcut -Force
    Write-Ok "Removed legacy startup shortcut"
}

# Remove ClearVis daemon directory
$visDir = Join-Path $env:LOCALAPPDATA "ClearVis"
if (Test-Path $visDir) {
    Remove-Item -Recurse -Force $visDir -ErrorAction SilentlyContinue
    Write-Ok "Removed ClearVis directory"
}

# Try spicetify restore before nuking
$oldSpicetify = Get-Command spicetify -ErrorAction SilentlyContinue
if ($oldSpicetify) {
    try { & spicetify restore 2>$null } catch {}
}

# Nuke spicetify directories
$spicetifyDirs = @(
    "$env:LOCALAPPDATA\spicetify",
    "$env:APPDATA\spicetify",
    "$env:USERPROFILE\.spicetify"
)
foreach ($dir in $spicetifyDirs) {
    if (Test-Path $dir) {
        Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
        Write-Ok "Removed $dir"
    }
}

# Clean spicetify from user PATH
$userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath) {
    $cleanedPath = ($userPath -split ";" | Where-Object { $_ -and $_ -notmatch "spicetify" }) -join ";"
    [System.Environment]::SetEnvironmentVariable("PATH", $cleanedPath, "User")
}

Write-Ok "All existing spicetify installations removed"

# ── 4. Install locked Spotify version ────────────────────────────────────────
Write-Step "Installing Spotify $spotifyVersion"

$spotifyInstaller = Join-Path $env:TEMP "spotify_installer.exe"
$spotifyInstalled = $false

function Install-SpotifyFromFile {
    param([string]$InstallerPath)
    # Verify SHA256
    $hash = (Get-FileHash -Path $InstallerPath -Algorithm SHA256).Hash
    if ($hash -ne $spotifyInstallerSha256) {
        Write-Warn "SHA256 mismatch: expected $spotifyInstallerSha256 got $hash"
        return $false
    }
    Write-Ok "SHA256 verified"
    $proc = Start-Process -FilePath $InstallerPath -PassThru
    $proc | Wait-Process -Timeout 120 -ErrorAction SilentlyContinue
    if (-not $proc.HasExited) {
        Write-Warn "Installer timed out — killing orphan process"
        $proc | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
    Start-Sleep -Seconds 5
    Get-Process -Name "Spotify" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    return (Test-Path "$env:APPDATA\Spotify\Spotify.exe")
}

# Method 1: Download from our Git LFS (always available)
Write-Ok "Downloading Spotify installer from project repository..."
try {
    Invoke-WebRequest -Uri $spotifyInstallerUrl -OutFile $spotifyInstaller -UseBasicParsing
    if ((Test-Path $spotifyInstaller) -and (Install-SpotifyFromFile $spotifyInstaller)) {
        $spotifyInstalled = $true
        Write-Ok "Spotify installed from repository"
    }
} catch {
    Write-Warn "Repository download failed: $_"
}

# Method 2: Fallback to Spotify CDN
if (-not $spotifyInstalled) {
    Write-Warn "Trying Spotify CDN fallback..."
    try {
        Invoke-WebRequest -Uri $spotifyInstallerUrlCdn -OutFile $spotifyInstaller -UseBasicParsing
        if ((Test-Path $spotifyInstaller) -and (Install-SpotifyFromFile $spotifyInstaller)) {
            $spotifyInstalled = $true
            Write-Ok "Spotify installed from CDN"
        }
    } catch {
        Write-Warn "CDN download failed: $_"
    }
}

# Method 3: Try winget with exact version
if (-not $spotifyInstalled -and (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Warn "Trying winget install..."
    try {
        & winget install --id Spotify.Spotify --version $spotifyVersion --accept-source-agreements --accept-package-agreements --force --silent 2>$null
        Start-Sleep -Seconds 5
        Get-Process -Name "Spotify" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        if (Test-Path "$env:APPDATA\Spotify\Spotify.exe") {
            $spotifyInstalled = $true
            Write-Ok "Spotify installed via winget"
        }
    } catch {
        Write-Warn "winget install failed: $_"
    }
}

# Clean up installer
if (Test-Path $spotifyInstaller) { Remove-Item $spotifyInstaller -Force -ErrorAction SilentlyContinue }

if (-not $spotifyInstalled) {
    Exit-WithError "Could not install Spotify $spotifyVersion automatically. Please install version $spotifyVersion manually and re-run this script."
}

# Block Spotify auto-updates
Write-Step "Blocking Spotify auto-updates"

# Remove and lock the Update directory
$updateDir = "$env:LOCALAPPDATA\Spotify\Update"
if (Test-Path $updateDir) {
    Remove-Item -Recurse -Force $updateDir -ErrorAction SilentlyContinue
}
# Create a read-only file at the Update path to prevent directory recreation
New-Item -ItemType File -Path $updateDir -Force -ErrorAction SilentlyContinue | Out-Null
if (Test-Path $updateDir) {
    Set-ItemProperty -Path $updateDir -Name IsReadOnly -Value $true -ErrorAction SilentlyContinue
}

# Remove SpotifyMigrator.exe if present
$migrator = "$env:APPDATA\Spotify\SpotifyMigrator.exe"
if (Test-Path $migrator) {
    Remove-Item -Force $migrator -ErrorAction SilentlyContinue
    # Create read-only placeholder
    New-Item -ItemType File -Path $migrator -Force -ErrorAction SilentlyContinue | Out-Null
    Set-ItemProperty -Path $migrator -Name IsReadOnly -Value $true -ErrorAction SilentlyContinue
}

Write-Ok "Auto-updates blocked"

# ── 5. Install locked spicetify version ──────────────────────────────────────
Write-Step "Installing spicetify v$spicetifyVersion"

$spicetifyDir = "$env:LOCALAPPDATA\spicetify"
New-Item -ItemType Directory -Force -Path $spicetifyDir | Out-Null

$spicetifyZip = Join-Path $env:TEMP "spicetify.zip"
try {
    Invoke-WebRequest -Uri $spicetifyZipUrl -OutFile $spicetifyZip -UseBasicParsing
    Expand-Archive -Path $spicetifyZip -DestinationPath $spicetifyDir -Force
    Remove-Item $spicetifyZip -Force
    Write-Ok "spicetify v$spicetifyVersion extracted to $spicetifyDir"
} catch {
    Exit-WithError "Failed to download/extract spicetify v$spicetifyVersion from: $spicetifyZipUrl"
}

# Add to PATH for this session and permanently
$env:PATH = "$spicetifyDir;$env:PATH"
$userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
if (-not $userPath) { $userPath = "" }
if ($userPath -notmatch [Regex]::Escape($spicetifyDir)) {
    [System.Environment]::SetEnvironmentVariable("PATH", "$spicetifyDir;$userPath", "User")
    Write-Ok "Added spicetify to PATH"
}

# Verify
$spicetifyCmd = Get-Command spicetify -ErrorAction SilentlyContinue
if (-not $spicetifyCmd) {
    Exit-WithError "spicetify not found after installation"
}

$spiceVer = & spicetify --version 2>$null
Write-Ok "spicetify $spiceVer installed"

# ── 6. Initialize spicetify ─────────────────────────────────────────────────
Write-Step "Initializing spicetify"

# Need Spotify to have been launched at least once to create prefs
if (-not (Test-Path "$env:APPDATA\Spotify\prefs")) {
    Write-Warn "Launching Spotify once to create config files..."
    Start-Process "$env:APPDATA\Spotify\Spotify.exe"
    Start-Sleep -Seconds 10
    Get-Process -Name "Spotify" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Ok "Spotify initial launch complete"
}

# Generate spicetify config
try { & spicetify 2>$null } catch {}

# Locate spicetify config directory
$spicetifyConfigDir = $null
try {
    $pathOutput = & spicetify path -c 2>$null
    if ($pathOutput -and (Test-Path (Split-Path $pathOutput))) {
        $spicetifyConfigDir = Split-Path $pathOutput
    }
} catch {}

if (-not $spicetifyConfigDir) {
    # Fallback to common locations
    $candidates = @(
        "$env:APPDATA\spicetify",
        "$env:USERPROFILE\.spicetify"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $spicetifyConfigDir = $c; break }
    }
}

if (-not $spicetifyConfigDir -or -not (Test-Path $spicetifyConfigDir)) {
    Write-Warn "Config directory not found — initializing spicetify..."
    try { & spicetify 2>$null } catch {}
    try {
        $pathOutput = & spicetify path -c 2>$null
        if ($pathOutput) { $spicetifyConfigDir = Split-Path $pathOutput }
    } catch {}
    if (-not $spicetifyConfigDir -or -not (Test-Path $spicetifyConfigDir)) {
        Exit-WithError "Could not find spicetify config directory."
    }
}
Write-Ok "Config directory: $spicetifyConfigDir"

$themesDir = Join-Path $spicetifyConfigDir "Themes"
$clearDir  = Join-Path $themesDir $themeName

# ── 7. Download theme files ─────────────────────────────────────────────────
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
        Exit-WithError "Theme file download failed — check your internet connection."
    }
}

# ── 8. Configure spicetify ──────────────────────────────────────────────────
Write-Step "Configuring spicetify"

& spicetify config current_theme $themeName
if ($LASTEXITCODE -ne 0) { Write-Warn "Failed to set theme (exit code $LASTEXITCODE)" }
else { Write-Ok "Theme set to $themeName" }

& spicetify config inject_theme_js 1
if ($LASTEXITCODE -ne 0) { Write-Warn "Failed to enable JS injection (exit code $LASTEXITCODE)" }
else { Write-Ok "Theme JS injection enabled" }

& spicetify config color_scheme ""
if ($LASTEXITCODE -ne 0) { Write-Warn "Failed to reset color scheme (exit code $LASTEXITCODE)" }
else { Write-Ok "Color scheme reset to default" }

# ── 9. Apply ────────────────────────────────────────────────────────────────
Write-Step "Applying theme"
Write-Ok "This may take a minute — spicetify is processing Spotify files..."
& spicetify backup apply
if ($LASTEXITCODE -ne 0) {
    Write-Warn "spicetify backup apply failed (exit code $LASTEXITCODE), trying apply only..."
    & spicetify apply
    if ($LASTEXITCODE -ne 0) {
        Write-Host "   Failed to apply theme (exit code $LASTEXITCODE)" -ForegroundColor Red
        Write-Host "   Try running 'spicetify restore backup apply' manually." -ForegroundColor Yellow
        Exit-WithError "spicetify apply failed"
    }
}
Write-Ok "Theme applied successfully"

# ── 10. Build and install audio visualizer daemon ─────────────────────────────
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
    $still = Get-Process -Name "vis-capture" -ErrorAction SilentlyContinue
    if ($still) { Start-Sleep -Seconds 3 }
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
    # Ensure C++ compiler is available (install Build Tools if needed)
    $clCmd = Get-Command cl -ErrorAction SilentlyContinue

    if (-not $clCmd) {
        # cl.exe is never in PATH normally — look for existing VS installation
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

        if (-not (Test-Path $vswhere)) {
            Write-Warn "C++ build tools not found — installing Visual Studio Build Tools..."
            Write-Warn "This is a one-time setup and may take 5-10 minutes..."
            $btInstalled = $false

            # Try winget first (built into Windows 10/11)
            if (Get-Command winget -ErrorAction SilentlyContinue) {
                try {
                    & winget install Microsoft.VisualStudio.2022.BuildTools `
                        --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" `
                        --accept-source-agreements --accept-package-agreements 2>$null
                    if ($LASTEXITCODE -eq 0) {
                        $btInstalled = $true
                        Write-Ok "Build Tools installed via winget"
                    }
                } catch {}
            }

            # Fallback: download installer directly from Microsoft
            if (-not $btInstalled) {
                if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
                    Write-Warn "winget not available — downloading Build Tools installer directly..."
                }
                $bootstrapper = Join-Path $env:TEMP "vs_buildtools.exe"
                try {
                    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $bootstrapper -UseBasicParsing
                    $proc = Start-Process -FilePath $bootstrapper `
                        -ArgumentList "--quiet","--wait","--add","Microsoft.VisualStudio.Workload.VCTools","--includeRecommended" `
                        -Wait -PassThru
                    if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
                        $btInstalled = $true
                        Write-Ok "Build Tools installed"
                        if ($proc.ExitCode -eq 3010) {
                            Write-Warn "A system restart is recommended after this install completes"
                        }
                    } else {
                        Write-Warn "Build Tools installer exited with code $($proc.ExitCode)"
                    }
                } catch {
                    Write-Warn "Failed to install Build Tools: $_"
                }
                if (Test-Path $bootstrapper) { Remove-Item $bootstrapper -Force -ErrorAction SilentlyContinue }
            }
        }

        # Import VC environment into this PowerShell session via vcvarsall.bat
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath 2>$null
            if ($vsPath) {
                $vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
                if (Test-Path $vcvarsall) {
                    Write-Ok "Importing VC environment from: $vsPath"
                    $vcCmd = "`"$vcvarsall`" x64 >nul 2>&1 && set"
                    cmd /c $vcCmd | ForEach-Object {
                        if ($_ -match "^([^=]+)=(.*)$") {
                            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
                        }
                    }
                    $clCmd = Get-Command cl -ErrorAction SilentlyContinue
                    if ($clCmd) { Write-Ok "cl.exe is now available" }
                    else { Write-Warn "Could not find cl.exe after importing VC environment" }
                }
            } else {
                Write-Warn "No VS installation with C++ tools found via vswhere"
            }
        }
    }

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
            Write-Warn "Try running this script from a VS Developer Command Prompt, or build manually with build.bat"
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

# ── 11. Launch Spotify ───────────────────────────────────────────────────────
Write-Step "Launching Spotify"

if (Test-Path "$env:APPDATA\Spotify\Spotify.exe") {
    Start-Process "$env:APPDATA\Spotify\Spotify.exe"
    Write-Ok "Spotify launched"
} else {
    try {
        Start-Process "spotify"
        Write-Ok "Spotify launched"
    } catch {
        Write-Warn "Could not auto-launch Spotify — please start it manually"
    }
}

Write-Host "`n   Clear installed successfully!" -ForegroundColor Green
Write-Host "   Spotify $spotifyVersion (version-locked)" -ForegroundColor White
Write-Host "   Spicetify v$spicetifyVersion (version-locked)" -ForegroundColor White
Write-Host "   Enjoy your clean Spotify experience." -ForegroundColor White
Write-Host ""
