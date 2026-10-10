# Validation record

The complete native applet and KWin companion build against **Plasma/KWin 6.7.5,
Qt 6.12 and KDE Frameworks 6.30**. The latest complete run passed **20/20 CTest
targets**. The compiled applet has also been exercised in a disposable Plasma
Wayland session with two logical outputs.

## Build and component checks

CTest targets and individual test cases are different counts. QtTest totals below
include initialization/cleanup and data rows. Private-bus providers, mock GIO
volumes and model fixtures exercise native interfaces without claiming physical
hardware coverage.

| Check | Executed result | Scope |
| --- | --- | --- |
| Full native build and SDK CTest | 20/20 targets passed | Compiled applet/QML resources, KWin companion, configuration and backend suites |
| Package/configuration validation | Passed | 116 settings and 36 embedded QML/JS sources; schema bindings and resource registration |
| Node behavior tests | 18 passed | Click/Overview policy, shortcuts, actual-window preview scaling, adaptive opacity, centering, stack preferences/sort/filter/limits |
| Migration | 12 passed | All 127 source keys accounted for, source defaults, URL/stack identities, explicit native-shortcut import flags, safe literal parsing and non-overwriting review output |
| StackBackend | 16 passed | Real KIO jobs, changes/cancellation, application discovery, stale-device protection, GIO/KDE merge, mount-success opening and failure suppression |
| Isolated native Trash lifecycle | 3 passed in a standalone run | Real KIO trash/empty jobs, exact fixture content/listing, empty→full→empty change signals and removal of the fixture/data record; includes initialization and cleanup |
| Independent stack preferences | 1 passed in an additional Node run | Independent stack keys retain their view/sort overrides; desktop restart persistence is separate |
| LocationBackend | 10 passed | Private D-Bus discovery, authenticated native identity, subtree/URL matching, exact fallback, urgency and native window requests |
| RemoteMounts | 13 passed with each Qt dispatcher | GMount/GVolume discovery, stable IDs across mounting, Qt credential/question dialogs, delayed replies, cancellation and destruction; Qt GLib and `QT_NO_GLIB=1` runs |
| Notify/Unity integration | 12 passed | Real private-bus Notify and LauncherEntry traffic, retained/resident counts, DND, progress/urgency/updating, aliases and an already-owned `/Unity` object |
| PanelLayout | 9 passed | Generated supported Plasma script, dedicated-panel validation, geometry, copying, typed configuration reads and ownership-preserving cleanup |
| Global shortcuts | 20 passed | Native-default/custom/disabled mappings, persisted native-conflict markers and None/tab-form bindings, keymap-normalized defaults, repeat suppression, compositor-authoritative output routing, asynchronous lifetime guards, primary/all-output behavior and restoration |
| Native shortcut normalization | 9 passed | Actual XKB US/GB/German maps, active-group changes, French AZERTY's absent unshifted-digit defaults, invalid inputs and preservation of the live modifier state |
| Compiled popup components | 8 passed | Loads the built module's resources for task/recent menus, context previews, grouped tasks, stacks and location previews through a real QQmlEngine |
| Qt geometry/viewport components | 18 passed | Magnification, indicator geometry, fixed/adaptive layout and focused-task scrolling |
| Native policy | 17 passed | Visibility priorities, pressure/dwell, reveal bands, timing and all edge geometries |
| Native actions/filter/quicklists/controller | 4 / 3 / 3 / 3 passed | GPU launch argv/environment and AppStream; parental filtering; live DBusMenu; panel recovery, layer/background state and ownership |

The offscreen `docktasksmodel` target reports **6 passed and 2 skipped** because
those two cases require real native windows. They are included in the separate
**8-passed Wayland run** below.

## Executed compositor and applet checks

These checks used real KWin/Plasma processes, native windows and the compiled
applet. The compositor used software rendering; its two outputs are controlled
logical displays.

| Check | Observed result |
| --- | --- |
| Native KWin visibility fixture | 9 QtTest cases passed: actual hidden/opacity state, manual/autohide, animation, fullscreen, four-edge relative pointer pressure/dwell, ownership and non-dock rejection, including compositor pointer-output geometry and authenticated keymap-provider access. The managed owner received ten normalized shortcuts; a foreign caller received none. Input uses native EIS. |
| Native Wayland task model | 8 cases passed, including running-window filtering and restoration of a pinned launcher when its location window is excluded. |
| Clean compiled-applet startup | Passed after a fresh build/install in a private Plasma session: the applet loaded and its own surface registered with the real KWin companion. |
| Assembled applet visibility | Manual hide produced a hidden dock; fixed mode restored visibility and animated opacity. The controller recorded the native panel-state recovery information. |
| Four-edge applet sweep | Top, Right, Bottom and Left panel locations and rendered orientation verified with screenshots. |
| Two-output replication | A copy was created on the second output, settings propagated in both directions, disabling from the copy removed only the generated panel, and the unrelated standard Plasma panel remained unchanged. Original settings were restored. |
| Folder stacks | Fan displayed 10 of 12 entries with overflow; Grid/List displayed all 12. Hidden/backup entries were absent. Keyboard search/activation and a List mouse click opened the exact selected folder in real Dolphin processes. |
| Live location isolation | With patched Dolphin, a tab inside a tracked folder moved its native window to that folder’s actions; navigating outside restored the ordinary Dolphin task. Open Windows/Minimize/Close actions and location previews were displayed. |
| Dolphin discovery API | Patched stable Dolphin 26.08.2 published both split panes and an inactive tab, with a real PropertiesChanged signal and authenticated process identity. |
| Task context menu | Native application details, Places and pin actions rendered. Move to Desktop populated both choices after adding a temporary second desktop, which was removed afterward. |
| Pinning and reordering | Activity-aware menu pin/unpin changed stored launchers correctly. Three injected pointer drag reorders completed and updated the stored launcher order. |
| Restart persistence | After restarting the shell, the exact launcher order `[Dolphin, System Settings]`, Classic style, icon size 40 and fixed icon sizing were retained. This is a shell restart, not a complete logout/login sweep. |
| Shortcut restart persistence | Two consecutive shell restarts preserved the native-default marker; injected Meta+1 selected the configured Dolphin task after deliberately activating Konsole. All ten native Plasma bindings remained unchanged. |
| Recent-application context menu | A stopped recent application displayed Open, Pin and Application Details. Repeated opens, launch while the menu was open, and exit/reopen preserved the running shell with no QML errors. |
| Visual styles | Breeze, Glass, Minimal and Classic rendered. Minimal showed standalone icons without the native panel rectangle; the unrelated standard panel retained its background. Individual indicator presets and every custom color/alpha combination are separate checks. |
| Task windows and previews | Disabling previews displayed a Windows heading with titles. All Windows opened persistent previews; Application Actions returned to the native menu. Default-open previews, hover title/icon content and delayed fallback icons rendered. Closing the last window through an actual preview button dismissed both application and dialog for pinned and unpinned tasks. Software fallback images do not verify live thumbnail pixels. |
| Shortcut conflict notice | Both pointer and keyboard activation displayed the Meta+0 Zoom and Meta+Q Activity Switcher conflicts. The final notice, including its header, appeared below the panel without clipping. Existing native bindings remained unchanged. |

Screenshots include `runtime/reorder-fixed.png`, `virtualdesktop-submenu.png`,
`preview-icon-delayed.png`, `preview-close-after.png`, `preview-pinned-after.png`,
`conflicts-pointer-fixed.png` and `visual-{breeze,glass,minimal,classic}.png`.
Final restart evidence is in `runtime/shortcut-restart-regression.json` and
`pins-style-persistence.json`; original primary preferences were restored.
`final-shortcut-conflict-details.png` records the final notice placement.

The standalone Trash check used a fresh UID with private XDG directories and
D-Bus, after checking that no existing mount Trash belonged to that UID. It
trashed and emptied only its own fixture; the normal desktop profile was
untouched. This verifies the native backend lifecycle, not the GUI confirmation
dialog. Evidence is in `trash-acceptance-results.txt`; the separate stack
preference check is recorded in `stack-preferences-results.txt`.

The reusable setup workflow completed from a stopped SDK container through build,
component tests, installation, native Wayland fixtures and successful
compiled-applet registration, with no manual process cleanup. The final integration
run passed **20/20 CTest targets**, including native keymap and compiled-popup
tests. Its fresh compositor rerun passed **9 visibility + 8 task-model cases**
and again loaded and registered the actual compiled applet.

The final publication check reran the 9 visibility and 8 model cases and applet
registration after moving the private XDG profile setup before D-Bus startup.
It also verified the actual shortcut service process inherited the private
runtime, configuration, data, cache and state directories. This prevents
activated services from writing preferences into the caller's profile.

These results are recorded in the cloud run's `reusable-setup-verified.log`,
`ctest-release.log`, `wayland-release.log` and `wayland-pr.log`; disposable-session logs
are also retained under `kde/build-sdk/wayland-tests.*`.

The final installed applet passed an injected-input matrix using its native
active-keymap provider. Independent docks routed Meta+1 to Konsole on the primary
output and Dolphin on the secondary. Ctrl+Meta+1 opened a new primary Konsole
instance while the pointer was over another client; Shift+Meta+1 minimized it.
With shared all-output copies, Meta+1 targeted the primary dock even with the pointer
on the secondary; a sole enabled secondary dock also received activation.
A temporary custom Meta+Alt+Q overlay key displayed badges 1 and 2 on both copies
(`shortcut-replicas-custom-overlay.png`). All ten native Plasma bindings were
unchanged. The temporary dock overlay binding was restored afterward. This
verifies the bounded matrix through injected input, not physical keyboard feel,
every action combination or reveal timing.
The docks were fixed during this run, so timed autohide reveal was not exercised.
The observations are in `runtime/shortcut-final-acceptance.jsonl`; its initial
no-action Meta+1 entry came from an explicitly disabled fixture binding, before
the default-binding checks began.

A separate real KWin session on Xvfb received injected
Meta+1, Ctrl+Meta+1/0, Shift+Meta+1/0 and Meta+Q and dispatched their expected
activation, new-instance, alternate-action and overlay signals. A custom
Meta+Alt+F8 alternate binding survived a normalized-default change and delivered
the expected action; an explicitly disabled alternate binding remained disabled.
Using a real KGlobalAccel action in the native `plasmashell` component, Meta+1
dispatched only to the dock while bridged. Giving the dock a custom Meta+Alt+F9
binding restored native Meta+1 and delivered the custom dock key; explicitly
disabling the dock binding retained native Meta+1. Disabling dock hotkeys also
released the Ctrl/Shift families. The native binding stayed unchanged throughout.
This run used a controlled D-Bus keymap provider, as recorded in
`key-monitor/acceptance.json`; it complements the installed native-provider
matrix above.

The native provider is implemented and its standalone nine-case XKB suite passed.
It derives alternate defaults from KWin's active map/group using a scratch state,
without changing the compositor's pressed modifiers. On French AZERTY, which has
no unshifted logical digit defaults, it supplies no generated replacements;
existing/custom/disabled bindings are retained. Capture an explicit binding in
System Settings for that layout.

The tested profile assigns Meta+0 to KWin's **Zoom to Actual Size** and Meta+Q to
Plasma's **Activity Switcher**. The dock preserves both native assignments, leaves
its conflicting own bindings empty and reports the conflicts. In **System
Settings → Keyboard → Shortcuts**, choose different dock keys or change the native
bindings before assigning Meta+0 to the dock's tenth activation action and Meta+Q
to its overlay action. Ctrl+Meta+0 and Shift+Meta+0 passed the isolated input check;
the assembled overlay passed with temporary Meta+Alt+Q.

## Runtime prerequisites and limits

The tested SDK has GIO and **GVfs, gvfs-smb, gvfs-nfs and gvfs-dnssd 1.62.0-3**. GIO network
volume discovery and authentication require the relevant GVfs monitor/protocol
backend in the desktop session. KDE Places/KIO remain available independently.
The automated mount providers did not mount a real SMB/NFS server or eject a
physical device. The container has no UDisks hardware service or `/dev/fuse`, so
physical media and GVfs FUSE exposure were not exercised.

KWin reported the native Overview effect unsupported in this software-rendered
session, which has no DRM render device. The active Overview case is explicitly
not selected by the disposable runner in that environment; it is not recorded as
a passing visual test. Live GPU/PipeWire thumbnail streams, real dual-GPU launch,
physical pointer/keyboard feel, touch, screen readers, mixed fractional scales,
physical display hotplug, removable hardware/authentication providers and a full
logout/login acceptance sweep remain to be exercised on a supported desktop.

The shipped Dolphin integration is required for full subtree/inactive-tab
isolation; stock Dolphin is verified only for exact URLs. The KWin companion
requires matching native headers/libraries and Wayland. These dependencies and
remaining interaction checks are listed in [FEATURE_PARITY.md](FEATURE_PARITY.md).

Reproduce fast checks with `make kde-check`, component checks with CTest, and the
isolated native session with [`test-wayland.sh`](../tools/test-wayland.sh).
The disposable runner preserves its own logs and never manipulates the user’s
running compositor.
