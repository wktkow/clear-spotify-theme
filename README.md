# Clear

Opinionated spicetify theme reducing screen clutter and improving readability

![Screenshot](images/screenshot1.png)

## Installation

1. Copy `user.css`, `color.ini`, and `theme.js` into your Spicetify Themes directory:
   ```bash
   mkdir -p ~/.config/spicetify/Themes/Clear
   cp user.css color.ini theme.js ~/.config/spicetify/Themes/Clear/
   ```
2. Apply the theme:
   ```bash
   spicetify config current_theme Clear
   spicetify config inject_theme_js 1
   spicetify apply
   ```

## Clear is using code from:

### Base Theme

- **ShadeX** by Sanoojes — A Minimal Spicetify theme inspired by Shadcn/ui Design
  - https://github.com/sanoojes/Spicetify-ShadeX

### Snippets (from [Spicetify Marketplace](https://github.com/spicetify/marketplace))

All snippets sourced from [resources/snippets.json](https://github.com/spicetify/marketplace/blob/main/resources/snippets.json):

- **Dynamic Search Bar** — Make the search bar dynamic, so it only shows when you hover over it
- **Switch Sidebars** — Move the navigation panel to the right and the information sidebar to the left
- **Thin Library Sidebar Rows** — Single-line rows in the library sidebar, like the pre-2023 UI
- **Queue Top Side Panel** — Moves the "Next in queue" section to the top of the Now Playing view
- **Remove the Artists and Credits sections from the Sidebar** — Only show the album cover and the next song
- **Thicker Bars** — Makes the song progress and volume bar thicker
- **Smooth Progress/Volume Bar** — Makes the Progress/Volume bar glide
- **Nyan Cat Progress Bar** — Changes your playback progress bar to Nyan cat flying through space on a rainbow
- **Sonic Dancing** — Sonic dancing on your playback bar
- **Fix 'Liked' Icon** — Fix the colours of the Liked icon in sidebar
- **Hide profile username** — Hides your username next to your profile picture
- **Hide Friend Activity Button** — Hides the Friend Activity button
- **Hide What's New Button** — Hides the What's New button
- **Hide Mini Player Button** — Hides the Mini Player button
- **Hide download button** — Hide download button in EPs and albums
- **Hide 'Scroll through previews'** — Hides the 'Scroll through previews of tracks' button from albums/playlists
- **Hide play count** — Hides the play count on songs/albums
- **Disable Homepage Recommendations** — Removes all recommendations from the homepage
- **Remove Popular sections from homepage** — Removes popular/trending sections
- **Remove recently played from homepage** — Removes the recently played shelf from the home page
