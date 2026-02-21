# Clear Spotify Client

Opinionated Spotify client reducing screen clutter and improving readability.

![Screenshot](images/screenshot1.png)

## On Hover search bar! :D

![On Hover Search Bar](images/onhover.gif)

## Embeded Audio Visualizer! :D

[![Watch the Embedded Audio Visualizer demo](images/vis-demo.jpg)](https://youtu.be/fwry72jtBTE)

### And it's configurable!

![Visualizer Settings](images/vis-settings.png)

## Installation

> **⚠️ Destructive install:** The installer **wipes every existing Spotify and Spicetify installation** on your machine
> (Desktop, Store, Snap, deb, Flatpak — all of them) and force-installs exact pinned versions:
>
> - **Spotify** `1.2.74.477.g3be53afe`
> - **Spicetify** `v2.42.11`
>
> Auto-updates are blocked so the versions never change. This is by design — Clear is tested
> against these specific builds and nothing else.

### On Windows:

0. The installer handles everything automatically — it removes any existing Spotify (Desktop or Store) and Spicetify, installs the locked versions, and blocks auto-updates.

1. Open PowerShell and paste:

```powershell
iwr -useb https://raw.githubusercontent.com/wktkow/clear-spotify-client/main/powershell/install.ps1 | iex
```

(Removes any previous Clear install, downloads fresh files, applies, and launches Spotify)

2. Enjoy!

### On Linux (Flatpak only):

> **Flatpak is required** — no other Spotify installation method is supported.
> The installer removes any existing Spotify (Snap, native deb, or Flatpak) and Spicetify,
> then installs the locked versions via Flatpak.

[click here](/docs/linux-install.md)

## Uninstall

### On Windows:

Open PowerShell and paste:

```powershell
iwr -useb https://raw.githubusercontent.com/wktkow/clear-spotify-client/main/powershell/uninstall.ps1 | iex
```

(Restores Spotify to vanilla, removes all Clear files and the visualizer daemon)

## Potential Roadmap

I will get to it when im bored again (on average takes about ~ 1 year). [Future Features](docs/todo.md)

## Initial Sources

Clear is based on code from various external sources. You can see the full list
[here](docs/sources.md).
