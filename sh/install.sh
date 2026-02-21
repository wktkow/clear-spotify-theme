#!/bin/bash
# Clear Spotify Client — Linux installer
# All-in-one: detects existing installation, fully removes it, installs fresh.
# Kills Spotify, restores vanilla state, downloads theme files, applies,
# builds the audio visualizer daemon (vis-capture), and launches Spotify.
# Requires: spicetify, curl, g++, libpulse-dev (for visualizer)

set -euo pipefail

REPO="wktkow/clear-spotify-theme"
BRANCH="main"
BASE_URL="https://raw.githubusercontent.com/$REPO/$BRANCH"
THEME_FILES=("user.css" "color.ini" "theme.js")
NATIVE_FILES=("native/common/protocol.h" "native/common/fft.h" "native/common/ws_server.h" "native/linux/main.cpp" "native/linux/Makefile" "native/linux/clear-vis.service")
THEME_NAME="Clear"
DEFAULT_DIR="$HOME/.config/spicetify/Themes/$THEME_NAME"

cyan()   { printf '\033[1;36m>> %s\033[0m\n' "$*"; }
green()  { printf '\033[0;32m   %s\033[0m\n' "$*"; }
yellow() { printf '\033[0;33m   %s\033[0m\n' "$*"; }
red()    { printf '\033[0;31m   %s\033[0m\n' "$*"; }

# ── 1. Check spicetify is installed ─────────────────────────────────────────
cyan "Checking spicetify installation"
if ! command -v spicetify &>/dev/null; then
    red "spicetify is not installed or not in PATH."
    red "Install it first: https://spicetify.app"
    yellow "Run: curl -fsSL https://raw.githubusercontent.com/spicetify/cli/main/install.sh | sh"
    exit 1
fi
green "spicetify found at $(command -v spicetify)"

# ── 2. Kill Spotify ─────────────────────────────────────────────────────────
cyan "Stopping Spotify"
if pgrep -x spotify &>/dev/null; then
    pkill -x spotify 2>/dev/null || true
    sleep 2
    green "Spotify stopped"
else
    green "Spotify was not running"
fi

# Also kill flatpak Spotify if present
if pgrep -f "com.spotify.Client" &>/dev/null; then
    pkill -f "com.spotify.Client" 2>/dev/null || true
    sleep 1
    green "Flatpak Spotify stopped"
fi

# ── 3. Determine install directory ──────────────────────────────────────────
cyan "Determining install directory"

# Try spicetify's own config path first
SPICETIFY_DIR=""
if SPICE_CONFIG=$(spicetify path -c 2>/dev/null); then
    SPICETIFY_DIR="$(dirname "$SPICE_CONFIG")"
fi

# Fallback to common locations
if [[ -z "$SPICETIFY_DIR" ]]; then
    for candidate in "$HOME/.config/spicetify" "$HOME/.spicetify"; do
        if [[ -d "$candidate" ]]; then
            SPICETIFY_DIR="$candidate"
            break
        fi
    done
fi

if [[ -n "$SPICETIFY_DIR" ]]; then
    INSTALL_DIR="$SPICETIFY_DIR/Themes/$THEME_NAME"
else
    INSTALL_DIR="$DEFAULT_DIR"
fi

green "Default install directory: $INSTALL_DIR"

if [[ ! -d "$(dirname "$INSTALL_DIR")" ]]; then
    yellow "Themes directory does not exist yet: $(dirname "$INSTALL_DIR")"
    read -rp "   Create it? (y/n/custom path): " choice
    case "$choice" in
        y|Y|yes|YES)
            mkdir -p "$INSTALL_DIR"
            green "Created $INSTALL_DIR"
            ;;
        n|N|no|NO)
            red "Cannot continue without a valid directory."
            exit 1
            ;;
        *)
            # User entered a custom path
            CUSTOM_DIR="$choice"
            # Expand ~ if present
            CUSTOM_DIR="${CUSTOM_DIR/#\~/$HOME}"
            if [[ ! -d "$CUSTOM_DIR" ]]; then
                yellow "Directory '$CUSTOM_DIR' does not exist."
                read -rp "   Create it? (y/n): " confirm
                if [[ "$confirm" =~ ^[Yy] ]]; then
                    mkdir -p "$CUSTOM_DIR"
                    green "Created $CUSTOM_DIR"
                else
                    red "Cannot continue without a valid directory."
                    exit 1
                fi
            fi
            INSTALL_DIR="$CUSTOM_DIR"
            green "Using custom directory: $INSTALL_DIR"
            ;;
    esac
else
    mkdir -p "$INSTALL_DIR"
fi

# Verify the directory exists now
if [[ ! -d "$INSTALL_DIR" ]]; then
    red "Install directory does not exist: $INSTALL_DIR"
    exit 1
fi

# ── 4. Detect and fully remove previous installation ────────────────────────
cyan "Checking for existing Clear installation"

# Check if Clear was previously applied
PREVIOUS_THEME=$(spicetify config current_theme 2>/dev/null || true)
if [[ "$PREVIOUS_THEME" == *"$THEME_NAME"* ]] || [[ -d "$INSTALL_DIR" ]]; then
    yellow "Existing Clear installation detected — removing completely"

    # Restore Spotify to vanilla (undoes any previous spicetify apply)
    if spicetify restore 2>/dev/null; then
        green "Restored Spotify to vanilla state"
    else
        yellow "spicetify restore returned non-zero (may already be vanilla)"
    fi

    # Nuke the entire theme directory
    if [[ -d "$INSTALL_DIR" ]]; then
        rm -rf "${INSTALL_DIR:?}"
        green "Removed $INSTALL_DIR"
    fi

    # Reset spicetify config to defaults
    spicetify config current_theme "" 2>/dev/null || true
    spicetify config inject_theme_js 0 2>/dev/null || true
    spicetify config color_scheme "" 2>/dev/null || true
    spicetify config extensions "" 2>/dev/null || true
    green "Reset spicetify configuration"
else
    green "No previous $THEME_NAME installation found"
fi

# Make sure config file exists
if [[ -n "$SPICETIFY_DIR" ]]; then
    CONFIG_INI="$SPICETIFY_DIR/config-xpui.ini"
    if [[ ! -f "$CONFIG_INI" ]]; then
        yellow "No config-xpui.ini found — running spicetify to generate it"
        spicetify &>/dev/null || true
    fi
fi

# ── 5. Download theme files ─────────────────────────────────────────────────
cyan "Downloading $THEME_NAME theme files"
mkdir -p "$INSTALL_DIR"

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

# ── 6. Configure spicetify ──────────────────────────────────────────────────
cyan "Configuring spicetify"

spicetify config current_theme "$THEME_NAME"
green "Theme set to $THEME_NAME"

spicetify config inject_theme_js 1
green "Theme JS injection enabled"

spicetify config color_scheme ""
green "Color scheme reset to default"

# ── 7. Apply ────────────────────────────────────────────────────────────────
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

# ── 8. Build and install audio visualizer daemon ─────────────────────────────
cyan "Setting up audio visualizer daemon"

# Check for g++ and libpulse
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
            systemctl --user daemon-reload
            systemctl --user enable clear-vis.service
            systemctl --user start clear-vis.service
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

# ── 9. Launch Spotify ────────────────────────────────────────────────────────
cyan "Launching Spotify"

if command -v flatpak &>/dev/null && flatpak list 2>/dev/null | grep -q com.spotify.Client; then
    flatpak run com.spotify.Client &>/dev/null &
    green "Spotify launched (flatpak)"
elif command -v spotify &>/dev/null; then
    spotify &>/dev/null &
    green "Spotify launched"
elif command -v snap &>/dev/null && snap list 2>/dev/null | grep -q spotify; then
    snap run spotify &>/dev/null &
    green "Spotify launched (snap)"
else
    yellow "Could not auto-launch Spotify — please start it manually"
fi

echo ""
green "Clear installed successfully!"
green "Enjoy your clean Spotify experience."
echo ""