# Goshos Dock modifications

## 2026-08-26

- Stop the stack popup from inheriting the shell app-menu maximum width for the grid and fan views, which clipped every column past the third.
- Size the stack grid from the tile width the theme really gives, scale its spacing on HiDPI, and keep the number of columns within the work area.
- Keep the fan arc inside the work area, overlapping its entries instead of running off the screen.

## 2026-08-25

- Initialize Do Not Disturb state before deriving notification-monitor enablement.
- Emit and consume a dedicated Do Not Disturb change signal so application emblems and progress indicators refresh even when notification-counter enablement does not change.
- Skip the old-Dash destroy signal hook in dummy-overview session modes where no overview Dash object exists.

Goshos Dock is a modified version of [Dash to Dock](https://github.com/micheleg/dash-to-dock), distributed under GPL-2.0-or-later. The complete corresponding source is this repository.

## Gosh OS changes

Changes began on 2026-08-24 from upstream commit `ef2e761a1a2da69400ec5202d1f383992a0d0404` and include:

- Goshos Dock naming and branding
- macOS-like icon magnification
- Applications, Documents, Downloads, and custom-folder stacks
- per-stack fan, grid, and list layouts
- stack sorting and popup placement behavior
- dimming and click/gesture preferences
- GNOME Shell 45 through 51 compatibility fixes for stack actors, magnification, pointer handling, and asynchronous icon lifecycles

The fork intentionally retains UUID `dash-to-dock@micxgx.gmail.com`, the `dashtodock` gettext domain, and the existing GSettings schema path so users keep compatible settings across an upgrade from Dash to Dock.

Individual modified source files also carry a dated Gosh OS modification notice. The original license is preserved in [`COPYING`](COPYING).
