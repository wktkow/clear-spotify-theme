# Clear Spotify Client

Opinionated Spotify client reducing screen clutter and improving readability.

![Screenshot](images/screenshot1.png)

## On Hover search bar! :D

![On Hover Search Bar](images/onhover.gif)

## Embeded Audio Visalizer! :D

![Embeded Audio Visualizer](images/visualizer.png)

## Installation

### On Windows:

0. Make sure you have Spotify installed and you are logged in.

1. Open PowerShell **_(as administrator)_** and paste:

```powershell
iwr -useb https://raw.githubusercontent.com/wktkow/clear-spotify-client/main/powershell/install.ps1 | iex
```

(Installs spicetify if needed, removes any previous Clear install, downloads fresh files, applies, and launches Spotify)

2. Enjoy!

### On Linux:

[click here](/docs/linux-install.md)

## Uninstall

### On Windows:

Open PowerShell **_(as administrator)_** and paste:

```powershell
iwr -useb https://raw.githubusercontent.com/wktkow/clear-spotify-client/main/powershell/uninstall.ps1 | iex
```

(Restores Spotify to vanilla, removes all Clear files and the visualizer daemon)

## Potential Roadmap

I will get to it when im bored again (on average takes about ~ 1 year). [Future Features](docs/todo.md)

## Initial Sources

Clear is based on code from various external sources. You can see the full list
[here](docs/sources.md).
