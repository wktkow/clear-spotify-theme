#!/bin/bash
# Clear Spotify Client — Linux installer (version-locked)
# Enforces Flatpak Spotify and pinned versions of both Spotify and Spicetify.
# Removes any existing Spotify/Spicetify installation, installs locked versions,
# downloads theme files, applies, builds the audio visualizer daemon, launches Spotify.
# Requires: flatpak, curl, g++ and libpulse-dev (optional, for visualizer)

set -euo pipefail

# ── LOCKED VERSIONS — DO NOT CHANGE ─────────────────────────────────────────
SPICETIFY_VERSION="2.42.11"
SPOTIFY_VERSION="1.2.74.477.g3be53afe"
MARKETPLACE_VERSION="1.0.8"
SPOTIFY_FLATPAK_COMMIT="d0881734d9f85a709418c4e671116b0c87baf24eebfa2a47a473d11fefe8f223"
SPICETIFY_TAR_URL="https://github.com/wktkow/clear-spotify-client/raw/main/installers/spicetify-${SPICETIFY_VERSION}-linux-amd64.tar.gz"
# ─────────────────────────────────────────────────────────────────────────────

REPO="wktkow/clear-spotify-client"
BRANCH="main"
BASE_URL="https://raw.githubusercontent.com/$REPO/$BRANCH"
THEME_FILES=("user.css" "color.ini" "theme.js")
NATIVE_FILES=("native/common/protocol.h" "native/common/fft.h" "native/common/ws_server.h" "native/linux/main.cpp" "native/linux/Makefile" "native/linux/clear-vis.service")
THEME_NAME="Clear"

cyan()   { printf '\033[1;36m>> %s\033[0m\n' "$*"; }
green()  { printf '\033[0;32m   %s\033[0m\n' "$*"; }
yellow() { printf '\033[0;33m   %s\033[0m\n' "$*"; }
red()    { printf '\033[0;31m   %s\033[0m\n' "$*"; }

# ── 1. Enforce Flatpak ─────────────────────────────────────────────────────
cyan "Checking Flatpak availability"
if ! command -v flatpak &>/dev/null; then
    red "Flatpak is NOT installed."
    red "Clear on Linux REQUIRES Flatpak — no other installation method is supported."
    red "Install Flatpak first: https://flatpak.org/setup/"
    exit 1
fi
green "Flatpak found at $(command -v flatpak)"

# Ensure flathub remote exists
if ! flatpak remotes --user 2>/dev/null | grep -q flathub && \
   ! flatpak remotes --system 2>/dev/null | grep -q flathub; then
    yellow "Flathub remote not found — adding it"
    flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    green "Flathub remote added"
fi

# ── 2. Kill Spotify ─────────────────────────────────────────────────────────
cyan "Stopping Spotify"
if pgrep -f "com.spotify.Client" &>/dev/null; then
    pkill -f "com.spotify.Client" 2>/dev/null || true
    sleep 2
    green "Flatpak Spotify stopped"
fi
if pgrep -x spotify &>/dev/null; then
    pkill -x spotify 2>/dev/null || true
    sleep 2
    green "Native Spotify stopped"
fi
green "Spotify is not running"

# ── 3. Remove ALL existing Spotify installations ────────────────────────────
cyan "Removing all existing Spotify installations"

# Remove snap Spotify
if command -v snap &>/dev/null && snap list 2>/dev/null | grep -q spotify; then
    yellow "Snap Spotify found — removing (Clear requires Flatpak only)"
    sudo snap remove spotify 2>/dev/null || yellow "Could not remove Snap Spotify (may need sudo)"
fi

# Remove native apt/deb Spotify
if dpkg -l spotify-client 2>/dev/null | grep -q "^ii"; then
    yellow "Native deb Spotify found — removing (Clear requires Flatpak only)"
    sudo apt remove --purge -y spotify-client 2>/dev/null || yellow "Could not remove native Spotify (may need sudo)"
fi

# Remove any Flatpak Spotify (user install — will reinstall locked version)
if flatpak list --user 2>/dev/null | grep -q com.spotify.Client; then
    # Unmask first in case it was masked from a previous run
    flatpak mask --user --remove com.spotify.Client 2>/dev/null || true
    yellow "User Flatpak Spotify found — removing for clean install"
    if flatpak uninstall --user -y --noninteractive com.spotify.Client 2>/dev/null; then
        green "User Flatpak Spotify removed"
    else
        yellow "Could not remove user Flatpak Spotify — will install over it"
    fi
fi

# Remove system-wide Flatpak Spotify
if flatpak list --system 2>/dev/null | grep -q com.spotify.Client; then
    yellow "System Flatpak Spotify found — removing"
    sudo flatpak uninstall --system -y --noninteractive com.spotify.Client 2>/dev/null || yellow "Could not remove system Flatpak Spotify (may need sudo)"
fi

green "All existing Spotify installations removed"

# ── 4. Remove ALL existing spicetify ────────────────────────────────────────
cyan "Removing all existing spicetify installations"

# Stop visualizer daemon first
systemctl --user stop clear-vis.service 2>/dev/null || true
systemctl --user disable clear-vis.service 2>/dev/null || true
pkill -f vis-capture 2>/dev/null || true

# If spicetify is currently installed, try to restore Spotify first
if command -v spicetify &>/dev/null; then
    spicetify restore 2>/dev/null || true
fi

# Nuke spicetify directories
rm -rf "$HOME/.spicetify" 2>/dev/null || true
rm -rf "$HOME/.config/spicetify" 2>/dev/null || true
rm -f "$HOME/.local/bin/spicetify" 2>/dev/null || true

# Remove from system paths
for p in /usr/local/bin/spicetify /usr/bin/spicetify; do
    if [[ -f "$p" ]]; then
        sudo rm -f "$p" 2>/dev/null || true
    fi
done

green "All existing spicetify installations removed"

# ── 5. Install locked Spotify via Flatpak ───────────────────────────────────
cyan "Installing Spotify $SPOTIFY_VERSION via Flatpak"

flatpak install --user -y --noninteractive flathub com.spotify.Client
green "Spotify Flatpak base installed"

# Pin to the exact version commit
yellow "Pinning to version $SPOTIFY_VERSION (commit ${SPOTIFY_FLATPAK_COMMIT:0:12}...)"
if flatpak update --user -y --noninteractive --commit="$SPOTIFY_FLATPAK_COMMIT" com.spotify.Client; then
    green "Spotify pinned to version $SPOTIFY_VERSION"
else
    red "Failed to pin Spotify to commit $SPOTIFY_FLATPAK_COMMIT"
    red "The required version may no longer be available on Flathub."
    exit 1
fi

# Block future updates
if flatpak mask --user com.spotify.Client 2>/dev/null; then
    green "Spotify auto-updates blocked (flatpak mask)"
else
    yellow "Could not mask Spotify updates — run 'flatpak mask --user com.spotify.Client' manually"
fi

# Verify installed version
INSTALLED_VER=$(flatpak info --user com.spotify.Client 2>/dev/null | grep "Version:" | awk '{print $2}' || true)
if [[ "$INSTALLED_VER" == "$SPOTIFY_VERSION" ]]; then
    green "Verified: Spotify $INSTALLED_VER"
else
    red "Version mismatch! Expected $SPOTIFY_VERSION but got '$INSTALLED_VER'"
    exit 1
fi

# ── 6. Install locked spicetify ─────────────────────────────────────────────
cyan "Installing spicetify v$SPICETIFY_VERSION"

SPICETIFY_DIR="$HOME/.spicetify"
mkdir -p "$SPICETIFY_DIR"

TEMP_TAR=$(mktemp --suffix=.tar.gz)
if curl -fsSL "$SPICETIFY_TAR_URL" -o "$TEMP_TAR"; then
    tar -xzf "$TEMP_TAR" -C "$SPICETIFY_DIR"
    rm -f "$TEMP_TAR"
    chmod +x "$SPICETIFY_DIR/spicetify"
    green "spicetify v$SPICETIFY_VERSION extracted to $SPICETIFY_DIR"
else
    red "Failed to download spicetify v$SPICETIFY_VERSION from:"
    red "$SPICETIFY_TAR_URL"
    rm -f "$TEMP_TAR"
    exit 1
fi

# Make spicetify available in PATH
mkdir -p "$HOME/.local/bin"
ln -sf "$SPICETIFY_DIR/spicetify" "$HOME/.local/bin/spicetify"
export PATH="$HOME/.spicetify:$HOME/.local/bin:$PATH"

if ! command -v spicetify &>/dev/null; then
    red "spicetify not found in PATH after installation"
    red "Ensure \$HOME/.local/bin or \$HOME/.spicetify is in your PATH"
    exit 1
fi

SPICE_VER=$(spicetify --version 2>/dev/null || true)
green "spicetify $SPICE_VER installed"

# ── 7. Initialize spicetify for Flatpak Spotify ─────────────────────────────
cyan "Initializing spicetify"

# Spotify needs to run at least once so it creates its config directories
SPOTIFY_DATA="$HOME/.var/app/com.spotify.Client/config/spotify"
if [[ ! -d "$SPOTIFY_DATA" ]]; then
    yellow "Launching Spotify once to create config directories..."
    flatpak run com.spotify.Client &>/dev/null &
    SPOTIFY_PID=$!
    sleep 10
    kill "$SPOTIFY_PID" 2>/dev/null || true
    pkill -f "com.spotify.Client" 2>/dev/null || true
    sleep 2
    green "Spotify initial launch complete"
fi

# Generate spicetify config
spicetify 2>/dev/null || true

# Set paths explicitly for Flatpak Spotify
SPOTIFY_APP_PATH="$HOME/.local/share/flatpak/app/com.spotify.Client/current/active/files/extra/share/spotify"
SPOTIFY_PREFS_PATH="$HOME/.var/app/com.spotify.Client/config/spotify/prefs"

if [[ -d "$SPOTIFY_APP_PATH" ]]; then
    spicetify config spotify_path "$SPOTIFY_APP_PATH"
    green "Spotify path set (user flatpak)"
else
    # Try system flatpak path
    SYS_SPOTIFY_PATH="/var/lib/flatpak/app/com.spotify.Client/current/active/files/extra/share/spotify"
    if [[ -d "$SYS_SPOTIFY_PATH" ]]; then
        spicetify config spotify_path "$SYS_SPOTIFY_PATH"
        green "Spotify path set (system flatpak)"
    else
        yellow "Could not find Spotify app path — relying on spicetify auto-detection"
    fi
fi

if [[ -f "$SPOTIFY_PREFS_PATH" ]]; then
    spicetify config prefs_path "$SPOTIFY_PREFS_PATH"
    green "Prefs path set"
fi

# ── 8. Download theme files ─────────────────────────────────────────────────
cyan "Setting up theme directory"

SPICETIFY_CONFIG_DIR=""
if SPICE_CONFIG=$(spicetify path -c 2>/dev/null); then
    SPICETIFY_CONFIG_DIR="$(dirname "$SPICE_CONFIG")"
fi

if [[ -z "$SPICETIFY_CONFIG_DIR" ]]; then
    SPICETIFY_CONFIG_DIR="$HOME/.config/spicetify"
fi

INSTALL_DIR="$SPICETIFY_CONFIG_DIR/Themes/$THEME_NAME"
mkdir -p "$INSTALL_DIR"
green "Theme directory: $INSTALL_DIR"

cyan "Downloading $THEME_NAME theme files"
for file in "${THEME_FILES[@]}"; do
    url="$BASE_URL/$file"
    dest="$INSTALL_DIR/$file"
    if curl -fsSL "$url" -o "$dest"; then
        green "$file downloaded"
    else
        red "Failed to download $file from $url"
        exit 1
    fi
done

# ── 9. Configure and apply ──────────────────────────────────────────────────
cyan "Configuring spicetify"

spicetify config current_theme "$THEME_NAME"
green "Theme set to $THEME_NAME"

spicetify config inject_theme_js 1
green "Theme JS injection enabled"

spicetify config color_scheme ""
green "Color scheme reset to default"

# ── 9b. Install Spicetify Marketplace v${MARKETPLACE_VERSION} (pinned) ──────────────────────
cyan "Installing Spicetify Marketplace v$MARKETPLACE_VERSION"

SPICETIFY_CONFIG_DIR=""
if SPICE_CONFIG=$(spicetify path -c 2>/dev/null); then
    SPICETIFY_CONFIG_DIR="$(dirname "$SPICE_CONFIG")"
fi
if [[ -z "$SPICETIFY_CONFIG_DIR" ]]; then
    SPICETIFY_CONFIG_DIR="$HOME/.config/spicetify"
fi

MARKET_DIR="$SPICETIFY_CONFIG_DIR/CustomApps/marketplace"
rm -rf "$MARKET_DIR" 2>/dev/null || true
mkdir -p "$MARKET_DIR"

MARKET_ZIP_URL="$BASE_URL/installers/marketplace-v${MARKETPLACE_VERSION}.zip"
MARKET_ZIP=$(mktemp /tmp/marketplace-XXXXXX.zip)
if curl -fsSL "$MARKET_ZIP_URL" -o "$MARKET_ZIP"; then
    unzip -q -o "$MARKET_ZIP" -d "$MARKET_DIR"
    # The zip contains a marketplace-dist folder — move its contents up
    if [[ -d "$MARKET_DIR/marketplace-dist" ]]; then
        mv "$MARKET_DIR/marketplace-dist"/* "$MARKET_DIR/" 2>/dev/null || true
        rm -rf "$MARKET_DIR/marketplace-dist"
    fi
    rm -f "$MARKET_ZIP"
    # Remove old custom app name if exists, add new one
    spicetify config custom_apps spicetify-marketplace- 2>/dev/null || true
    spicetify config custom_apps marketplace
    green "Marketplace v$MARKETPLACE_VERSION installed"
else
    yellow "Could not download Marketplace — you can install it later"
    rm -f "$MARKET_ZIP"
fi

cyan "Applying theme"
if spicetify backup apply 2>/dev/null; then
    green "Theme applied successfully"
else
    yellow "spicetify backup apply failed, trying apply only..."
    if spicetify apply; then
        green "Theme applied successfully"
    else
        red "Failed to apply theme."
        yellow "Try running 'spicetify restore backup apply' manually."
        exit 1
    fi
fi

# ── 10. Build and install audio visualizer daemon ────────────────────────────
cyan "Setting up audio visualizer daemon"

if ! command -v g++ &>/dev/null; then
    yellow "g++ not found — skipping visualizer daemon build"
    yellow "Install with: sudo apt install g++  (or equivalent)"
elif ! pkg-config --exists libpulse-simple 2>/dev/null; then
    yellow "libpulse-dev not found — skipping visualizer daemon build"
    yellow "Install with: sudo apt install libpulse-dev  (or equivalent)"
else
    # Stop old daemon if present (frees port 7700 for new one)
    systemctl --user stop clear-vis.service 2>/dev/null || true
    pkill -f vis-capture 2>/dev/null || true
    sleep 0.3

    # Download native source files
    BUILD_DIR=$(mktemp -d)
    mkdir -p "$BUILD_DIR/native/common" "$BUILD_DIR/native/linux"

    native_ok=true
    for nf in "${NATIVE_FILES[@]}"; do
        url="$BASE_URL/$nf"
        dest="$BUILD_DIR/$nf"
        if ! curl -fsSL "$url" -o "$dest"; then
            red "Failed to download $nf"
            native_ok=false
            break
        fi
    done

    if $native_ok; then
        # Build
        if (cd "$BUILD_DIR/native/linux" && make -j"$(nproc)"); then
            green "vis-capture built successfully"

            # Install binary
            mkdir -p "$HOME/.local/bin"
            cp "$BUILD_DIR/native/linux/vis-capture" "$HOME/.local/bin/vis-capture"
            chmod +x "$HOME/.local/bin/vis-capture"
            green "Installed vis-capture to ~/.local/bin/"

            # Install and enable systemd user service
            mkdir -p "$HOME/.config/systemd/user"
            cp "$BUILD_DIR/native/linux/clear-vis.service" "$HOME/.config/systemd/user/"
            systemctl --user daemon-reload || true
            systemctl --user enable clear-vis.service || true
            systemctl --user start clear-vis.service || true
            sleep 1
            if systemctl --user is-active --quiet clear-vis.service; then
                green "Audio visualizer daemon is running (auto-starts on login)"
            else
                yellow "Daemon enabled but may not have started — check: systemctl --user status clear-vis.service"
            fi
        else
            red "vis-capture build failed — see errors above"
            yellow "You can build manually: cd native/linux && make"
        fi
    fi

    rm -rf "$BUILD_DIR"
fi

# ── 11. Launch Spotify ──────────────────────────────────────────────────────
cyan "Launching Spotify"
flatpak run com.spotify.Client &>/dev/null &
green "Spotify launched (Flatpak)"

echo ""
green "Clear installed successfully!"
green "Spotify  $SPOTIFY_VERSION (Flatpak, version-locked)"
green "Spicetify v$SPICETIFY_VERSION (version-locked)"
green "Enjoy your clean Spotify experience."
echo ""
