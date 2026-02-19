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
                "js": ["guard.js", "theme.js"],
                "run_at": "document_start",
            }
        ],
    }


def build() -> None:
    # Clean previous builds
    for d in (BUILD_DIR, DIST_DIR):
        if os.path.exists(d):
            shutil.rmtree(d)
        os.makedirs(d)

    # 1. Parse color.ini → colors.css
    color_ini_path = os.path.join(REPO_ROOT, "color.ini")
    schemes = parse_color_ini(color_ini_path)
    colors_css = generate_colors_css(schemes)
    with open(os.path.join(BUILD_DIR, "colors.css"), "w") as f:
        f.write(colors_css)
    print(f"  ✓ colors.css  ({len(schemes)} scheme(s) found, using first)")

    # 2. Copy user.css
    shutil.copy(os.path.join(REPO_ROOT, "user.css"), BUILD_DIR)
    print("  ✓ user.css")

    # 3. Copy theme.js
    shutil.copy(os.path.join(REPO_ROOT, "theme.js"), BUILD_DIR)
    print("  ✓ theme.js")

    # 4. Copy guard.js (extension-only login guard)
    shutil.copy(os.path.join(SCRIPT_DIR, "guard.js"), BUILD_DIR)
    print("  ✓ guard.js")

    # 5. Write manifest.json
    manifest = create_manifest()
    with open(os.path.join(BUILD_DIR, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    print("  ✓ manifest.json")

    print(f"\n  → Unpacked: {BUILD_DIR}")

    # 6. Pack into .zip for Chrome Web Store / easy sharing
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
