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
    """
    c = colors
    border = f"1px solid #{c.get('border', '262626')}"
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
        "/* Now playing bar container (desktop uses footer, web uses aside) */",
        '[data-testid="now-playing-bar"] {',
        "  gap: 0.25rem !important;",
        "  border: none !important;",
        "}",
        "",
        "/* Now playing bar inner wrapper gets the border */",
        '[data-testid="now-playing-bar"] > div {',
        f"  border: {border} !important;",
        "  border-radius: 0.5rem !important;",
        "}",
        "",
        "/* Player controls play/pause button – match desktop style */",
        '[data-testid="player-controls"] button[data-testid="control-button-playpause"] {',
        "  background: none !important;",
        f"  color: #{c.get('text', 'fafafa')} !important;",
        "}",
        "",
        "/* Top layout container background */",
        "[data-right-sidebar-hidden] {",
        f"  background-color: #{c.get('main', '0a0a0a')} !important;",
        "}",
        "",
        "/* Hide box-shadows globally (matches desktop * rule) */",
        "[data-testid=\"root\"] * {",
        "  box-shadow: none !important;",
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
