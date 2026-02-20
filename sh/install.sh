#!/bin/bash
# Clear Spotify Theme — Linux installer
# Kills Spotify, clears previous theme/snippets, downloads files, applies, launches.
# Requires: spicetify, curl

set -euo pipefail

REPO="wktkow/clear-spotify-theme"
BRANCH="main"
BASE_URL="https://raw.githubusercontent.com/$REPO/$BRANCH"
THEME_FILES=("user.css" "color.ini" "theme.js")
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

# ── 4. Clear previous theme and code snippets ───────────────────────────────
cyan "Cleaning previous installation"

if [[ -d "$INSTALL_DIR" ]] && ls "$INSTALL_DIR"/* &>/dev/null 2>&1; then
    rm -rf "${INSTALL_DIR:?}"/*
    green "Cleared old $THEME_NAME theme files"
else
    green "No previous $THEME_NAME files found"
fi

# Reset spicetify config to clear stale snippet references
if [[ -n "$SPICETIFY_DIR" ]]; then
    CONFIG_INI="$SPICETIFY_DIR/config-xpui.ini"
    if [[ -f "$CONFIG_INI" ]]; then
        green "Config file found, will be updated by spicetify config"
    else
        yellow "No config-xpui.ini found — running spicetify to generate it"
        spicetify &>/dev/null || true
    fi
fi

# ── 5. Download theme files ─────────────────────────────────────────────────
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

# ── 8. Launch Spotify ────────────────────────────────────────────────────────
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
green "Clear theme installed successfully!"
green "Enjoy your clean Spotify experience."
echo ""