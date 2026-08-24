# goshos-dock
![screenshot](https://github.com/micheleg/dash-to-dock/raw/master/media/screenshot.jpg)

## A macOS-like dock for the GNOME Shell
goshos-dock enhances the dash, moving it out of the overview and transforming it into a dock for an easier launching of applications and a faster switching between windows and desktops without having to leave the desktop view.

It is a fork of [Dash to Dock](https://github.com/micheleg/dash-to-dock) that adds a set of optional macOS dock behaviours on top of it. Every new feature is **off by default**, so an existing Dash to Dock setup keeps behaving exactly as before until the features are turned on in *Preferences → macOS*.

### macOS features

| Feature | Setting |
| --- | --- |
| **Icon magnification** — the icon under the pointer, and its neighbours, grow smoothly; the magnification level and how many icons are affected are both configurable | *Magnify icons on hover* |
| **Show Applications as a stack** — the Show Applications button pops the installed applications up over the dock instead of opening the activities overview | *Show Applications opens a stack* |
| **Applications stack** — a dedicated dock item that pops up the installed applications | *Applications stack* |
| **Documents / Downloads / Home stacks** — dock items that pop up the contents of those folders | *Documents stack*, *Downloads stack*, *Home stack* |
| **Custom folder stack** — the same, for any folder you pick | *Custom folder stack* |
| **Fan / Grid / List stack views** — the three macOS stack presentations, plus *Automatic*, and sorting by name, date modified or kind (also reachable by right-clicking a stack) | *View content as*, *Sort by* |
| **Stacks divider** — the macOS style divider between the applications and the stacks | *Show a divider before the stacks* |
| **Recent applications** — append the most recently used applications that are neither pinned nor running | *Show recently used applications* |
| **Launch bounce** — the icon of a starting application hops out of the dock | *Bounce the icon of a starting application* |
| **Dim hidden applications** — applications whose windows are all minimized are drawn translucent | *Dim applications whose windows are all minimized* |

Magnification reserves the room the magnified icons need up front, so the dock allocation — and therefore the work area, the intellihide box and the desktop icons area — never changes while the pointer moves across the dock.

## Installation from source

The extension can be installed directly from source, either for the convenience of using git or to test the latest development version. Clone the desired branch with git

### Build Dependencies

To compile the stylesheet you'll need an implementation of SASS. goshos-dock supports `dart-sass` (`sass`), `sassc`, and `ruby-sass`. Every distro should have at least one of these implementations, we recommend using `dart-sass` (`sass`) or `sassc` over `ruby-sass` as `ruby-sass` is deprecated.

By default, goshos-dock will attempt to build with `sassc`. To change this behavior set the `SASS` environment variable to either `dart` or `ruby`.

```bash
export SASS=dart
# or...
export SASS=ruby
```

### Building

Clone the repository or download the branch from github. A simple Makefile is included.

Next use `make` to install the extension into your home directory. A Shell reload is required <kbd>Alt</kbd> + <kbd>F2</kbd> <kbd>r</kbd> <kbd>Enter</kbd> under Xorg or under Wayland you may have to logout and login. The extension has to be enabled  with *gnome-extensions-app* (GNOME Extensions) or with *dconf*.

```bash
make install
```

If `msgfmt` is not available on your system, you will see an error message like the following:

```bash
make: msgfmt: No such file or directory
```

In this case install the `gettext` package from your distribution's repository.

### A note on the extension identity

The rename to goshos-dock is deliberately cosmetic. The extension UUID
(`dash-to-dock@micxgx.gmail.com`), the GSettings schema id and path
(`org.gnome.shell.extensions.dash-to-dock`,
`/org/gnome/shell/extensions/dash-to-dock/`), the `dashtodock` gettext domain
and the `dashtodock*` actor names are all unchanged, so existing installs keep
their settings, their place in `enabled-extensions`, their translations and
their third-party shell themes.

## Bug Reporting

Bugs in the upstream dock should be reported to the Dash to Dock bug tracker [https://github.com/micheleg/dash-to-dock/issues](https://github.com/micheleg/dash-to-dock/issues).

## License
goshos-dock, like the Dash to Dock GNOME Shell extension it is based on, is distributed under the terms of the GNU General Public License,
version 2 or later. See the COPYING file for details.
