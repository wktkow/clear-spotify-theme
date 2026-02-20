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

## Initial Sources

Clear is based on code from various external sources. You can see the full list
[here](docs/sources.md).
