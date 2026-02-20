#!/usr/bin/env python3
"""
Build the Clear Spotify Theme into a Chrome extension.

Reads user.css, theme.js and color.ini from the repo root and packages
them into chrome-extension/build/ as a ready-to-load unpacked extension.

Usage:
    python3 chrome-extension/build.py
"""

import configparser
import json
import os
import re
import shutil
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")
DIST_DIR = os.path.join(SCRIPT_DIR, "dist")


def hex_to_rgb(hex_str: str) -> str:
    """Convert a hex colour (e.g. 'fafafa') to 'r,g,b' string."""
    h = hex_str.lstrip("#").strip()
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return f"{r},{g},{b}"


def parse_color_ini(path: str) -> dict[str, dict[str, str]]:
    """Parse color.ini → { scheme_name: { key: hex_value } }."""
    config = configparser.ConfigParser()
    config.read(path)
    return {section: dict(config[section]) for section in config.sections()}


def generate_colors_css(schemes: dict[str, dict[str, str]]) -> str:
    """Generate :root CSS custom properties from the first colour scheme.

    Produces both --spice-{key} (hex) and --spice-rgb-{key} (r,g,b)
    to match what Spicetify injects on the desktop app.
    """
    scheme_name = "dark" if "dark" in schemes else list(schemes.keys())[0]
    colors = schemes[scheme_name]

    lines = [f"/* Auto-generated from color.ini [{scheme_name}] */", ":root {"]
    for key, value in colors.items():
        lines.append(f"  --spice-{key}: #{value};")
        lines.append(f"  --spice-rgb-{key}: {hex_to_rgb(value)};")
    lines.append("}")
    return "\n".join(lines)


def generate_encore_overrides(colors: dict[str, str]) -> str:
    """Generate Encore design system variable overrides.

    The Spotify web player uses Encore theme tokens (--background-base,
    --text-base, etc.) instead of Spicetify's --spice-* variables.
    Overriding these makes Spotify's own CSS use our colour scheme.
    """
    c = colors
    rgb_text = hex_to_rgb(c.get("text", "fafafa"))
    return "\n".join([
        "",
        "/* ===== Encore design system overrides (web player) ===== */",
        ".encore-dark-theme,",
        ".encore-dark-theme .encore-base-set,",
        ".encore-dark-theme .encore-inverted-dark-set {",
        f"  --background-base: #{c.get('main', '0a0a0a')} !important;",
        f"  --background-highlight: #{c.get('highlight', '171717')} !important;",
        f"  --background-press: #{c.get('main', '0a0a0a')} !important;",
        f"  --background-elevated-base: #{c.get('main-elevated', '171717')} !important;",
        f"  --background-elevated-highlight: #{c.get('highlight-elevated', '171717')} !important;",
        f"  --background-elevated-press: #{c.get('highlight', '171717')} !important;",
        f"  --background-tinted-base: rgba({rgb_text}, 0.04) !important;",
        f"  --background-tinted-highlight: rgba({rgb_text}, 0.07) !important;",
        f"  --background-tinted-press: rgba({rgb_text}, 0.03) !important;",
        f"  --text-base: #{c.get('text', 'fafafa')} !important;",
        f"  --text-subdued: #{c.get('subtext', '737373')} !important;",
        f"  --text-bright-accent: #{c.get('accent', 'fafafa')} !important;",
        f"  --text-positive: #{c.get('accent', 'fafafa')} !important;",
        "  --text-negative: #f15e6c !important;",
        "  --text-warning: #ffa42b !important;",
        f"  --essential-base: #{c.get('text', 'fafafa')} !important;",
        f"  --essential-subdued: #{c.get('subtext', '737373')} !important;",
        f"  --essential-bright-accent: #{c.get('accent', 'fafafa')} !important;",
        "  --essential-negative: #e91429 !important;",
        f"  --essential-positive: #{c.get('accent', 'fafafa')} !important;",
        f"  --decorative-base: #{c.get('accent', 'fafafa')} !important;",
        f"  --decorative-subdued: #{c.get('subtext', '737373')} !important;",
        "}",
    ])


def generate_web_bridge_css(colors: dict[str, str]) -> str:
    """Generate CSS rules targeting web player elements by stable selectors.

    The web player uses different element structure and tag names than
    Spicetify on desktop.  These rules use IDs, data-testid attributes,
    and aria labels that are stable across Spotify deploys.

    Covers gaps where user.css selectors rely on Spicetify-only class
    names or desktop-only tag names (e.g. <footer> vs <aside>).
    """
    c = colors
    border = f"1px solid #{c.get('border', '262626')}"
    card = f"#{c.get('card', '171717')}"
    main = f"#{c.get('main', '0a0a0a')}"
    text = f"#{c.get('text', 'fafafa')}"
    accent = f"#{c.get('accent', 'fafafa')}"
    rgb_main = hex_to_rgb(c.get('main', '0a0a0a'))
    rgb_text = hex_to_rgb(c.get('text', 'fafafa'))
    rgb_card = hex_to_rgb(c.get('card', '171717'))
    rgb_accent = hex_to_rgb(c.get('accent', 'fafafa'))

    # Nyan Cat rainbow gradient (same as user.css desktop version)
    nyan_gradient = (
        "linear-gradient(to bottom,"
        "#ff0000 0%,#ff0000 16.5%,"
        "#ff9900 16.5%,#ff9900 33%,"
        "#ffff00 33%,#ffff00 50%,"
        "#33ff00 50%,#33ff00 66%,"
        "#0099ff 66%,#0099ff 83.5%,"
        "#6633ff 83.5%,#6633ff 100%)"
    )
    nyan_bg_gif = "url(\"data:image/gif;base64,R0lGODlhMAAMAIAAAAxBd////yH/C05FVFNDQVBFMi4wAwEAAAAh+QQECgAAACwAAAAAMAAMAAACJYSPqcvtD6MKstpLr24Z9A2GYvJ544mhXQmxoesElIyCcB3dRgEAIfkEBAoAAAAsAQACAC0ACgAAAiGEj6nLHG0enNQdWbPefOHYhSLydVhJoSYXPO04qrAmJwUAIfkEBAoAAAAsBQABACkACwAAAiGEj6nLwQ8jcC5ViW3evHt1GaE0flxpphn6BNTEqvI8dQUAIfkEBAoAAAAsAQABACoACwAAAiGEj6nLwQ+jcU5VidPNvPtvad0GfmSJeicUUECbxnK0RgUAIfkEBAoAAAAsAAAAACcADAAAAiCEj6mbwQ+ji5QGd6t+c/v2hZzYiVpXmuoKIikLm6hXAAAh+QQECgAAACwAAAAALQAMAAACI4SPqQvBD6NysloTXL480g4uX0iW1Wg21oem7ismLUy/LFwAACH5BAQKAAAALAkAAAAkAAwAAAIghI8Joe0Po0yBWTaz3g/z7UXhMX7kYmplmo0rC8cyUgAAIfkEBAoAAAAsBQAAACUACgAAAh2Ejwmh7Q+jbIFZNrPeEXPudU74IVa5kSiYqOtRAAAh+QQECgAAACwEAAAAIgAKAAACHISPELfpD6OcqTGKs4bWRp+B36YFi0mGaVmtWQEAIfkEBAoAAAAsAAAAACMACgAAAh2EjxC36Q+jnK8xirOW1kavgd+2BYtJhmnpiGtUAAAh+QQECgAAACwAAAAALgALAAACIYSPqcvtD+MKicqLn82c7e6BIhZQ5jem6oVKbfdqQLzKBQAh+QQECgAAACwCAAIALAAJAAACHQx+hsvtD2OStDplKc68r2CEm0eW5uSN6aqe1lgAADs=\")"
    nyan_slider_gif = "url(\"data:image/gif;base64,R0lGODlhIgAVAKIHAL3/9/+Zmf8zmf/MmZmZmf+Z/wAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh/wtYTVAgRGF0YVhNUDw/eHBhY2tldCBiZWdpbj0i77u/IiBpZD0iVzVNME1wQ2VoaUh6cmVTek5UY3prYzlkIj8+IDx4OnhtcG1ldGEgeG1sbnM6eD0iYWRvYmU6bnM6bWV0YS8iIHg6eG1wdGs9IkFkb2JlIFhNUCBDb3JlIDUuMy1jMDExIDY2LjE0NTY2MSwgMjAxMi8wMi8wNi0xNDo1NjoyNyAgICAgICAgIj4gPHJkZjpSREYgeG1sbnM6cmRmPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5LzAyLzIyLXJkZi1zeW50YXgtbnMjIj4gPHJkZjpEZXNjcmlwdGlvbiByZGY6YWJvdXQ9IiIgeG1sbnM6eG1wTU09Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9tbS8iIHhtbG5zOnN0UmVmPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvc1R5cGUvUmVzb3VyY2VSZWYjIiB4bWxuczp4bXA9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC8iIHhtcE1NOk9yaWdpbmFsRG9jdW1lbnRJRD0ieG1wLmRpZDpDMkJBNjY5RTU1NEJFMzExOUM4QUM2MDAwNDQzRERBQyIgeG1wTU06RG9jdW1lbnRJRD0ieG1wLmRpZDpCREIzOEIzMzRCN0IxMUUzODhEQjgwOTYzMTgyNTE0QiIgeG1wTU06SW5zdGFuY2VJRD0ieG1wLmlpZDpCREIzOEIzMjRCN0IxMUUzODhEQjgwOTYzMTgyNTE0QiIgeG1wOkNyZWF0b3JUb29sPSJBZG9iZSBQaG90b3Nob3AgQ1M2IChXaW5kb3dzKSI+IDx4bXBNTTpEZXJpdmVkRnJvbSBzdFJlZjppbnN0YW5jZUlEPSJ4bXAuaWlkOkM1QkE2NjlFNTU0QkUzMTE5QzhBQzYwMDA0NDNEREFDIiBzdFJlZjpkb2N1bWVudElEPSJ4bXAuZGlkOkMyQkE2NjlFNTU0QkUzMTE5QzhBQzYwMDA0NDNEREFDIi8+IDwvcmRmOkRlc2NyaXB0aW9uPiA8L3JkZjpSREY+IDwveDp4bXBtZXRhPiA8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\")"
    sonic_gif = "url(\"https://media.tenor.com/pWqGD2PHY3kAAAAj/fortnite-dance-sonic.gif\")"

    return "\n".join([
        "",
        "/* ===== Web player bridge CSS (stable selectors) ===== */",
        "",
        "/* Main content panel */",
        "#main-view {",
        f"  border: {border} !important;",
        "  border-radius: 0.5rem !important;",
        "}",
        "",
        "/* Library sidebar */",
        "#Desktop_LeftSidebar_Id {",
        f"  border: {border} !important;",
        "  border-radius: 0.5rem !important;",
        "}",
        "",
        "/* ── Now playing bar (web uses <aside>, desktop uses <footer>) ── */",
        '[data-testid="now-playing-bar"] {',
        "  gap: 0.25rem !important;",
        "  border: none !important;",
        "}",
        '[data-testid="now-playing-bar"] > div {',
        f"  border: {border} !important;",
        "  border-radius: 0.5rem !important;",
        "}",
        "",
        "/* Play/pause button – remove green circle, match desktop minimal style */",
        '[data-testid="player-controls"] button[data-testid="control-button-playpause"],',
        '[data-testid="player-controls"] button[data-testid="control-button-playpause"] > span {',
        "  background: none !important;",
        f"  color: {text} !important;",
        "}",
        "",
        "/* Top layout container background */",
        "[data-right-sidebar-hidden] {",
        f"  background-color: {main} !important;",
        "}",
        "",
        "/* Hide box-shadows globally */",
        '[data-testid="root"] * {',
        "  box-shadow: none !important;",
        "}",
        "",
        "/* ── Now Playing View (right sidebar panel) ── */",
        "",
        "/* Sidebar must clip content to prevent overflow into main view */",
        ".Root__right-sidebar {",
        "  overflow: hidden !important;",
        "}",
        "",
        "/* Override Spotify's absolute positioning + transforms that break */",
        "/* when the sidebar-swap snippet moves the panel to left-sidebar. */",
        ".Root__right-sidebar > div {",
        "  width: 100% !important;",
        "}",
        "/* Inner positioning wrappers – neutralize absolute + transform, fill width */",
        ".Root__right-sidebar > div > div {",
        "  position: unset !important;",
        "  width: 100% !important;",
        "  transform: none !important;",
        "}",
        "/* The panel-slot div has inline width: 420px – override it */",
        ".Root__right-sidebar > div > div > div {",
        "  width: 100% !important;",
        "  max-width: 100% !important;",
        "}",
        "",
        "/* Panel container + inner aside fills full width */",
        "#Desktop_PanelContainer_Id {",
        f"  background-color: rgba({rgb_card}, 0.5) !important;",
        f"  border: {border} !important;",
        "  border-radius: 0.5rem !important;",
        "}",
        "",
        "/* Now-playing-view header area – glass effect */",
        '#Desktop_PanelContainer_Id [data-testid="NPV_Panel_OpenDiv"] {',
        f"  background-color: rgba({rgb_card}, 0.75) !important;",
        "}",
        "",
        "/* NowPlayingView section background override (inline --section-background-base) */",
        ".NowPlayingView {",
        f"  --section-background-base: {card} !important;",
        "}",
        "",
        "/* ── Progress bar (web uses Encore classes, not x-progressBar-*) ── */",
        "",
        "/* Map Encore progress bar vars to theme colors */",
        '[data-testid="playback-progressbar"] {',
        f"  --progress-bar-color: rgba({rgb_text}, 1) !important;",
        "}",
        '[data-testid="progress-bar"] {',
        f"  --fg-color: rgba({rgb_accent}, 1) !important;",
        f"  --bg-color: rgba({rgb_accent}, 0.125) !important;",
        f"  --is-active-fg-color: rgba({rgb_accent}, 0.75) !important;",
        "}",
        "",
        "/* Volume slider theming */",
        '[data-testid="volume-bar"] [data-testid="progress-bar"] {',
        f"  --fg-color: rgba({rgb_accent}, 1) !important;",
        f"  --bg-color: rgba({rgb_accent}, 0.125) !important;",
        "}",
        "",
        "/* Progress bar handle */",
        '[data-testid="progress-bar-handle"] {',
        f"  background-color: {accent} !important;",
        "}",
        "",
        "/* ── Thick Progress Bars (web equivalent of x-progressBar-* rules) ── */",
        "",
        "/* When thick-bars is enabled, increase --progress-bar-height */",
        'body.clear-thick-bars [data-testid="now-playing-bar"] [data-testid="progress-bar"] {',
        "  --progress-bar-height: 100% !important;",
        "}",
        "/* Ensure container chain has explicit height for 100% to resolve */",
        'body.clear-thick-bars [data-testid="playback-progressbar"] {',
        "  height: 100% !important;",
        "}",
        'body.clear-thick-bars [data-testid="progress-bar-background"] {',
        "  height: 100% !important;",
        "  top: 0 !important;",
        "  transform: none !important;",
        "}",
        'body.clear-thick-bars [data-testid="volume-bar"] [data-testid="progress-bar"] {',
        "  --progress-bar-height: 100% !important;",
        "}",
        "",
        "/* ── Nyan Cat Progress Bar (web equivalent) ── */",
        "",
        "/* Rainbow gradient on the fill bar (3rd child = fill, 2nd = hover preview) */",
        'body.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar-background"] > div:nth-child(3) > div {',
        f"  background: {nyan_gradient} !important;",
        "  background-color: transparent !important;",
        "}",
        "",
        "/* Nyan Cat background on the bar background element */",
        'body.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar-background"] {',
        f"  background-image: {nyan_bg_gif} !important;",
        "  background-repeat: repeat !important;",
        "}",
        "",
        "/* Nyan Cat slider image on the handle */",
        'body.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar-handle"] {',
        f"  background-image: {nyan_slider_gif} !important;",
        "  background-color: transparent !important;",
        "  background-size: 34px 21px !important;",
        "  background-repeat: no-repeat !important;",
        "  width: 34px !important;",
        "  height: 21px !important;",
        "  border: none !important;",
        "  margin-left: -18px !important;",
        "  border-radius: 0 !important;",
        "  display: block !important;",
        "  transform: translateY(-50%) scale(0.8) !important;",
        "  box-shadow: none !important;",
        "  image-rendering: pixelated !important;",
        "}",
        "",
        "/* Nyan Cat bar height */",
        'body.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar"] {',
        "  --progress-bar-height: 8px !important;",
        "}",
        "",
        "/* ── Sonic Dancing (web equivalent) ── */",
        "",
        "/* Position relative on the progressbar container for absolute ::before */",
        'body.clear-sonic [data-testid="playback-progressbar"] {',
        "  position: relative !important;",
        "}",
        'body.clear-sonic [data-testid="playback-progressbar"]::before {',
        '  content: "" !important;',
        "  width: 32px !important;",
        "  height: 32px !important;",
        "  bottom: calc(100% - 7px) !important;",
        "  right: 10px !important;",
        "  position: absolute !important;",
        "  image-rendering: pixelated !important;",
        "  background-size: 32px 32px !important;",
        f"  background-image: {sonic_gif} !important;",
        "}",
        "",
        "/* ── Pause dim (web equivalent) ── */",
        "",
        'body.clear-paused.clear-pause-dim.clear-sonic [data-testid="playback-progressbar"]::before {',
        "  opacity: 0.5 !important;",
        "}",
        'body.clear-paused.clear-pause-dim.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar-background"],',
        'body.clear-paused.clear-pause-dim.clear-nyan-cat [data-testid="now-playing-bar"] [data-testid="progress-bar-handle"] {',
        "  opacity: 0.5 !important;",
        "}",
        "",
        "/* ── Library sidebar thin rows (web has no main-yourLibraryX-*) ── */",
        "",
        "/* Target library rows by encore data attribute + sidebar context */",
        '#Desktop_LeftSidebar_Id [data-encore-id="listRow"] {',
        "  min-block-size: 0 !important;",
        "  padding-block: 0 !important;",
        "}",
        '#Desktop_LeftSidebar_Id [data-encore-id="listRow"] [role="group"] {',
        "  min-block-size: 0 !important;",
        "}",
        "/* Compact cover art — shrink the image container AND the image */",
        '#Desktop_LeftSidebar_Id [data-encore-id="listRow"] div:has(> [data-testid="entity-image"]) {',
        "  width: 1.6em !important;",
        "  height: 1.6em !important;",
        "  min-width: 0 !important;",
        "  min-height: 0 !important;",
        "  flex-shrink: 0 !important;",
        "}",
        '#Desktop_LeftSidebar_Id [data-encore-id="listRow"] [data-testid="entity-image"] {',
        "  width: 100% !important;",
        "  height: 100% !important;",
        "}",
        "",
        "/* ── Home page cards & track lists (fallback theming) ── */",
        "",
        "/* Track list border */",
        '[data-testid="playlist-tracklist"],',
        '[data-testid="track-list"] {',
        f"  border: {border} !important;",
        "}",
        "",
        "/* Track list rows */",
        '[data-testid="tracklist-row"] {',
        "  padding: 0 1.5rem !important;",
        "}",
        "",
    ])


def create_manifest() -> dict:
    """Return a Chrome MV3 extension manifest."""
    return {
        "manifest_version": 3,
        "name": "Clear Spotify Theme",
        "version": "1.0.0",
        "description": "Opinionated Spotify theme reducing screen clutter and improving readability",
        "icons": {},
        "content_scripts": [
            {
                "matches": ["https://open.spotify.com/*"],
                "css": ["theme.css"],
                "js": ["guard.js", "theme.js"],
                "run_at": "document_start",
            }
        ],
    }


def add_important(css: str) -> str:
    """Add !important to every CSS declaration that doesn't already have it.

    Chrome injects manifest CSS before Spotify's own stylesheets, so our
    rules lose the cascade battle at equal specificity.  Adding !important
    ensures our declarations always win, regardless of load order.

    On the desktop Spicetify side the original user.css (without !important
    everywhere) is used directly — this transform only affects the Chrome
    extension build output.
    """
    lines = css.split("\n")
    result: list[str] = []
    in_comment = False

    for line in lines:
        stripped = line.strip()

        # ── multi-line comment tracking ──────────────────────────────
        if in_comment:
            if "*/" in stripped:
                in_comment = False
            result.append(line)
            continue
        if "/*" in stripped and "*/" not in stripped:
            in_comment = True
            result.append(line)
            continue

        # ── skip non-declaration lines ───────────────────────────────
        if (
            not stripped
            or stripped.startswith("/*")       # single-line comment
            or stripped.startswith("*")        # doc-comment continuation
            or stripped.startswith("//")       # non-standard comment
            or stripped.endswith("{")           # selector / @-rule opening
            or stripped == "}"                 # closing brace
            or stripped.startswith("@")        # @media, @keyframes, etc.
            or "!important" in stripped        # already has it
            or ";" not in stripped             # not a declaration
            or ":" not in stripped             # not a declaration
        ):
            result.append(line)
            continue

        # ── add !important before the last semicolon ─────────────────
        idx = line.rindex(";")
        line = line[:idx] + " !important;" + line[idx + 1 :]
        result.append(line)

    return "\n".join(result)


def build() -> None:
    # Clean previous builds
    for d in (BUILD_DIR, DIST_DIR):
        if os.path.exists(d):
            shutil.rmtree(d)
        os.makedirs(d)

    # 1. Parse color.ini → colors CSS string
    color_ini_path = os.path.join(REPO_ROOT, "color.ini")
    schemes = parse_color_ini(color_ini_path)
    colors_css = generate_colors_css(schemes)
    print(f"  ✓ colors.css  ({len(schemes)} scheme(s) found, using first)")

    # 2. Read user.css
    with open(os.path.join(REPO_ROOT, "user.css")) as f:
        user_css = f.read()
    print("  ✓ user.css")

    # 3. Generate web-player-specific CSS sections
    scheme_name = "dark" if "dark" in schemes else list(schemes.keys())[0]
    scheme_colors = schemes[scheme_name]
    encore_css = generate_encore_overrides(scheme_colors)
    bridge_css = generate_web_bridge_css(scheme_colors)
    print("  ✓ encore overrides + web bridge CSS")

    # 4. Combine everything into one theme.css file.
    #    Chrome silently drops the second file when manifest has two CSS
    #    entries, so we merge them.  add_important() ensures our rules
    #    beat Spotify's regardless of load order.
    marker = "/* clear-theme-marker */\nhtml { --clear-ext-loaded: 1 !important; }\n\n"
    combined = (
        marker
        + add_important(colors_css) + "\n\n"
        + add_important(user_css) + "\n\n"
        + encore_css + "\n\n"
        + bridge_css
    )
    with open(os.path.join(BUILD_DIR, "theme.css"), "w") as f:
        f.write(combined)
    important_count = combined.count('!important')
    print(f"  ✓ theme.css (combined, {important_count} declarations, {len(combined)} bytes)")

    # 5. Copy theme.js
    shutil.copy(os.path.join(REPO_ROOT, "theme.js"), BUILD_DIR)
    print("  ✓ theme.js")

    # 6. Copy guard.js (extension-only login guard)
    shutil.copy(os.path.join(SCRIPT_DIR, "guard.js"), BUILD_DIR)
    print("  ✓ guard.js")

    # 7. Write manifest.json
    manifest = create_manifest()
    with open(os.path.join(BUILD_DIR, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print("  ✓ manifest.json")

    print(f"\n  → Unpacked: {BUILD_DIR}")

    # 8. Pack into .zip for Chrome Web Store / easy sharing
    zip_name = f"clear-spotify-theme-v{manifest['version']}.zip"
    zip_path = os.path.join(DIST_DIR, zip_name)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _dirs, files in os.walk(BUILD_DIR):
            for file in files:
                abs_path = os.path.join(root, file)
                arc_name = os.path.relpath(abs_path, BUILD_DIR)
                zf.write(abs_path, arc_name)
    print(f"  → Packed:   {zip_path}")

    print("\nLoad unpacked from build/ or upload the .zip to Chrome Web Store")


if __name__ == "__main__":
    print("Building Clear Spotify Theme Chrome extension...\n")
    build()
