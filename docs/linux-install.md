## Linux Installation (Flatpak only)

> **Flatpak is required.** No other Spotify installation method (Snap, native deb) is supported.
>
> The installer removes any existing Spotify and Spicetify installations, then installs:
>
> - **Spotify** `1.2.74.477.g3be53afe` (Flatpak, pinned and masked against auto-updates)
> - **Spicetify** `v2.42.11` (from GitHub releases)

0. Make sure [Flatpak](https://flatpak.org/setup/) is installed on your system.

1. Run the installer:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/wktkow/clear-spotify-client/main/sh/install.sh | bash
   ```

   (Removes all existing Spotify/Spicetify, installs locked versions, downloads theme, applies, and launches Spotify)

2. Enjoy!
