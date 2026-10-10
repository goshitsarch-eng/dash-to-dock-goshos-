# Native dock visibility companion

This KWin plugin gives a dedicated Gosho's Dock panel its own visibility policy.
Plasma's public panel scripting API provides four preset hiding modes, but does
not expose per-panel pressure, show/hide delays or all the original intelligent
hiding modes. A KWin script cannot set a window's hidden state or read raw pointer
motion. The companion therefore uses KWin's native plugin interface; it does not
change global screen-edge thresholds or unrelated panels.

## Build and installation

Build and install the complete `kde/` project using its normal CMake commands.
The additional requirements are the **development files for the installed KWin
release**, LayerShellQt, Qt Gui private headers, libxkbcommon and Wayland client development
files. The isolated compositor tests also use Qt WaylandClient private headers
and libei development files. CMake installs `goshosdockvisibility.so` in
`kwin/plugins` under the distribution's Qt plugin directory.

KWin's binary plugin ABI is versioned. **Rebuild this module after every KWin
update**, using matching headers and libraries. KWin's versioned factory IID
rejects a plugin built for another ABI. Distributors should rebuild the applet
and this module together. Log out and back in after installing the module; the
normal applet installation does not restart the compositor.

The implementation targets Plasma 6.7/KWin Wayland. It was compiled and exercised
with KWin 6.7.5, Qt 6.12.0 and Frameworks 6.30. X11 sessions do not provide this
companion's Wayland surface ownership contract.

## Ownership and recovery

`DockController` resolves the actual Plasma applet and containment. It accepts
only a panel containing the `org.gosh.goshosdock` applet alone. The controller
sends that panel's actual Wayland surface ID to KWin; KWin matches both the surface
ID and the caller's D-Bus process ID and requires a dock window. Registration
tokens are bound to the caller's unique D-Bus connection. Another process cannot
claim, query, update or unregister that panel by guessing its surface ID or token.

Only the matched panel's native hiding mode is changed through Plasma's supported
`evaluateScript` interface. Fixed mode uses Plasma's normal panel mode to reserve
work area. Other policies use windows-go-below mode, leaving work area available.
The original `panelVisibility` value is backed up in the applet's
`NativeDockController` configuration group and restored when management ends.
The backup is retained until Plasma acknowledges restoration, including across
shell restarts. Removing the applet or losing the compositor service restores
native panel behavior.

While managed, the controller temporarily places the panel in LayerShellQt's
overlay layer, so a requested reveal can appear above a fullscreen application.
It restores the original layer when management ends. The compositor restores
the window's original opacity and hidden state when the owner disconnects or
unregisters. No panel is created or claimed by the compositor plugin itself.

## Behavior

- Manual hiding, fixed visibility, hover autohide and intelligent hiding retain
  the source priority rules. The native Overview effect reveals the dock,
  including a manually hidden dock; screen locking suppresses it.
- Intelligent hiding checks actual KWin windows on the current desktop and
  activity: all windows; focused/top application plus always-above and paired
  half-tiled windows; maximized/fullscreen windows; or always-on-top except
  fullscreen. Native popups retain their parent dock while open.
- The source's `showDelay` is a dwell delay when pressure is disabled. With
  pressure enabled, it is the rolling pressure accumulation interval. Parallel
  motion, absolute input and pointer warps do not count as pressure. Outward
  samples are capped as in GNOME's pressure barrier; a single motion over the
  threshold can trigger immediately.
- A hidden managed dock may hold relative pointer motion at its own internal
  monitor edge until pressure reveals it. Revealing releases that barrier.
  Corners, buttons held down, drag-and-drop, locked sessions and fullscreen
  suppression bypass it. No other input is consumed.
- Hover departures honor `hideDelay`; new window overlap begins hiding
  immediately. `animationTime` controls a compositor opacity animation, an
  intentional KDE visual variation from GNOME's slide animation. Manual hiding
  and locking hide immediately.
- The applet supplies popup/drag/shortcut holds and the source's three-second
  urgent-notification reveal timer. Fullscreen configuration controls edge
  reveal; explicit urgency can still reveal the dock as in the source.

Window overlap and policy timeouts are sampled every 50 ms. Animation runs on
Qt's animation driver. Geometry comes from KWin in logical coordinates, including
negative output origins; no physical-pixel conversion is guessed.

## Tests

`ctest -R dockpolicy` runs the deterministic policy suite without a compositor.
It checks priorities, dwell/pressure windows, hover delay cancellation, urgent
and popup holds, fullscreen suppression, all edges and malformed settings.

`ctest -R shortcutnormalization` checks the actual XKB US, GB and German keymaps,
active US/German layout groups, French AZERTY's missing unshifted digit defaults,
invalid maps and preservation of a separate live modifier state. The helper uses
a scratch XKB state and KWin's consumed-modifier and Qt key conversion rules.

`tst_dockvisibility` runs against a real, already running **disposable** KWin
Wayland session with the plugin installed. It is deliberately excluded from
ordinary CTest, because it creates real windows and emulates pointer input.
Run it with the isolated session's D-Bus, Wayland and runtime environment:

```sh
GOSHOS_DISPOSABLE_KWIN_TEST=1 QT_QPA_PLATFORM=wayland \
  ./kde/build/kwin/tst_dockvisibility
```

The fixture owns a real layer-shell dock and real fullscreen client. It verifies
actual compositor hidden/opacity state, popup holds, raw pointer pressure on all
four edges, dwell, internal-output barrier holding/release, fullscreen edge
suppression/opt-in, and cross-process
surface/token rejection. Input uses KWin's native EIS remote-desktop interface
and libei, with no simulated compositor state or test methods in the plugin.
Keep the disposable session unlocked; locking correctly prevents the fixture
from revealing its dock. Two side-by-side outputs exercise internal-edge tests.

The active native Overview case requires an OpenGL compositor that supports that
effect. The cloud SDK's QPainter sessions explicitly select the other cases;
they verify inactive Overview D-Bus behavior and the pure policy, but cannot
validate activation/rendering of KDE's GPU-dependent Overview. The source includes
the full native test for execution on a supported desktop.

The native fixture does not substitute for the complete applet acceptance test:
verify its settings/error presentation, work-area restoration after shell/crash
and plugin removal, native Overview interaction, touch input, fractional/mixed
DPI, output hotplug, physical pointer feel, and coexistence with other edge
actions. The project's validation record distinguishes those checks from
automated policy and compositor tests.

## D-Bus interface

Service `org.gosh.GoshosDock.KWin`, object `/org/gosh/GoshosDock`, interface
`org.gosh.GoshosDock.KWin1` exposes `RegisterDock`, `UpdateDock`, `UnregisterDock`
and `QueryDock`. Global `OverviewVisible`, `OverviewChanged`, `OverviewAvailable`,
`OverviewAvailableChanged`, `ToggleOverview` and guarded `HideOverview` expose
the real native effect state and capability to the applet's click actions.
Toggle loads the native effect only when supported and needed, then invokes its
public activate/deactivate slots; it never toggles plugin loading as a UI action.
Closing an inactive Overview never opens it. `QueryDock` returns real compositor state only to the token's
owner; it is also used by the integration fixture. `DockController` is the
production client. Visibility, input filtering and animations reside in KWin;
panel configuration and applet identity validation stay in Plasma.

`PointerOutputGeometry` returns the real compositor pointer's output geometry in
logical coordinates to a D-Bus sender owning a registered managed dock. Shortcut
routing uses this because a Wayland client's `QCursor::pos()` can be stale while
the pointer is over a different client's window. Other callers receive an empty
map; the EIS fixture verifies both output changes and this ownership restriction.

`NormalizedAlternateShortcuts` returns ten combined Qt keys for Shift+Meta plus
the active layout's unshifted digits 1–9,0, using KWin's actual keyboard map and
layout. `AlternateShortcutsChanged` signals layout switches and keymap
reconfiguration. The same managed-owner restriction applies. This accounts for
KWin consuming Shift when it produces punctuation: US Shift+Meta+1 arrives as
Meta+!, while British and German layouts have different shifted symbols.
Layouts without unshifted digit keys, including French AZERTY, return zero for
those defaults; the client retains existing bindings so the user can capture an
unambiguous shortcut in System Settings. Explicit custom and disabled bindings
are retained when the layout changes.
