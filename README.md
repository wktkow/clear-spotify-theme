# Clear

Opinionated spicetify theme reducing screen clutter and improving readability

![Screenshot](images/screenshot1.png)

## On Hover search bar! :D

![On Hover Search Bar](images/onhover.gif)

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

## Clear is based on:

### Base Theme

- **ShadeX**
  - https://github.com/sanoojes/Spicetify-ShadeX

### Snippets (from [Spicetify Marketplace](https://github.com/spicetify/marketplace))

- **Dynamic Search Bar**
- **Switch Sidebars**
- **Thin Library Sidebar Rows**
- **Queue Top Side Panel**
- **Remove the Artists and Credits sections from the Sidebar**
- **Thicker Bars**
- **Smooth Progress/Volume Bar**
- **Nyan Cat Progress Bar**
- **Sonic Dancing**
- **Fix 'Liked' Icon**
- **Hide profile username**
- **Hide Friend Activity Button**
- **Hide What's New Button**
- **Hide Mini Player Button**
- **Hide download button**
- **Hide 'Scroll through previews'**
- **Hide play count**
- **Disable Homepage Recommendations**
- **Remove Popular sections from homepage**
- **Remove recently played from homepage**

All snippets are sourced from [resources/snippets.json](https://github.com/spicetify/marketplace/blob/main/resources/snippets.json)
