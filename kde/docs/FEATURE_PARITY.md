# GNOME to KDE feature parity

The KDE port is a Plasma 6 widget using Qt Quick, Kirigami and native KDE
backends. The current target was built with **Qt 6.12, KDE Frameworks 6.30 and
Plasma/KWin 6.7.5**. This inventory audits source behavior as well as all settings;
code-path presence and successful component tests do not establish complete
interactive acceptance of the assembled dock.

Visibility/pressure control requires the shipped native KWin Wayland companion
and a panel containing this dock alone. Full folder subtree and inactive-tab
isolation requires the optional, supplied Dolphin 26.08.2 patch. Stock Dolphin
retains exact-URL matching. Ordinary Plasma panels and other widgets are not
claimed by either bridge. X11 cannot provide this companion’s Wayland surface
ownership contract, so its complete visibility feature set is not supported.

The baseline is all **127 keys** in
[`org.gnome.shell.extensions.dash-to-dock.gschema.xml`](../../schemas/org.gnome.shell.extensions.dash-to-dock.gschema.xml).
Numbered shortcut families are grouped below; each range includes 1 through 10.
Settings with no corresponding KDE control are not silently imported or ignored.

Status terminology:

- **Implemented:** a corresponding code path exists; session testing may still be required.
- **Partial:** related behavior exists, but source semantics or controls differ.
- **Integration:** the complete code path requires the supplied optional platform integration; the stock-platform fallback has narrower semantics.
- **Gap:** no corresponding implementation has been established.
- **Platform:** GNOME-specific behavior requires a documented replacement or is inapplicable.

## Position, geometry and visibility

| GNOME key | Status | KDE implementation or remaining difference |
| --- | --- | --- |
| `dock-position` | Implemented | `dockPosition` drives PanelLayout through Plasma’s supported panel scripting API; only the identified dedicated dock panel is changed. |
| `animation-time` | Implemented | `animationTime` controls the KWin companion’s reveal/hide duration. KDE fades the panel rather than using GNOME’s slide, an intentional visual variation. |
| `show-delay` | Implemented | `showDelay` is the edge dwell delay without pressure, or the rolling pressure-accumulation interval with pressure, matching the source policy. |
| `hide-delay` | Implemented | `hideDelay` applies after hover/temporary holds end; new overlap starts hiding immediately, as in the source. |
| `manualhide` | Implemented | `manualHide` hides the dedicated panel on the desktop; native Overview can reveal it. Requires the KWin companion. |
| `intellihide` | Implemented | `intelliHide` uses actual KWin overlap on the panel output/current desktop/activity, combined with hover, popup, drag, shortcut and urgency holds. |
| `intellihide-mode` | Implemented | `intelliHideMode` preserves all four policies: all windows; focused/top application with always-above and paired half-tiled windows; maximized/fullscreen; always-on-top except fullscreen. |
| `autohide` | Implemented | `autoHide` enables edge reveal and hover retention independently of intelligent hiding through the KWin companion. |
| `require-pressure-to-show` | Implemented | `pressureToShow` selects raw outward relative-motion pressure or dwell. Absolute motion, warps and parallel motion do not accumulate pressure. |
| `pressure-threshold` | Implemented | `pressureThreshold` preserves capped motion accumulation and the source single-motion threshold. Internal-output barriers apply only to the managed hidden dock. |
| `autohide-in-fullscreen` | Implemented | `hideInFullscreen` stores the inverse source setting and suppresses edge reveal over fullscreen windows; explicit urgency/holds follow source priority. |
| `show-dock-urgent-notify` | Implemented | `unhideOnAttention` triggers the source three-second urgent reveal hold through DockController; urgent icon animation is independent. |
| `dock-fixed` | Implemented | `dockFixed` selects Plasma’s normal panel mode for work-area reservation; the controller restores the saved native mode when management ends. |
| `height-fraction` | Implemented | `intendedLengthFraction` sets a maximum length relative to the selected output; nonextended panels may fit shorter content. |
| `extend-height` | Implemented | `extendDock` fixes panel minimum/maximum length to the configured output fraction; the task/extras strip can fill that space. |
| `always-center-icons` | Implemented | `centerIcons` centers the strip in an extended panel; `appsAlwaysAtEdge` separately controls the application button’s position. |
| `preferred-monitor` | Platform | Deprecated GNOME monitor index, superseded in the source by connector identity. Migration retains it as an explained unmapped legacy value. |
| `preferred-monitor-by-connector` | Implemented | `preferredOutput` selects a connector or primary output. PanelLayout falls back when disconnected without discarding the preference and watches screen changes. |
| `multi-monitor` | Implemented | `allOutputs` creates dedicated copies, propagates General settings from either copy, and removes only helper-created dedicated copies. Supported scripting is used; physical hotplug/mixed-scale acceptance remains open. |
| `dash-max-icon-size` | Implemented | `iconSize` controls preferred size, constrained by available panel space. |
| `icon-size-fixed` | Implemented | `iconSizeFixed` keeps preferred icon size with a scrollable task viewport; otherwise icons shrink in source size steps. Panel thickness still bounds icon size. |
| `scroll-to-focused-application` | Implemented | `scrollToFocused` reveals the active task in the fixed-size overflow viewport; keyboard focus is also kept visible. |
| `disable-overview-on-startup` | Platform | Controls GNOME Shell startup overview; no equivalent KDE startup policy is changed. |
| `bolt-support` | Platform | Legacy GNOME-extension compatibility key; no consumer remains in the source runtime. |

## Application model, actions and previews

| GNOME key | Status | KDE implementation or remaining difference |
| --- | --- | --- |
| `show-running` | Implemented | `showRunning` controls running task delegates. |
| `show-favorites` | Implemented | `showFavorites` controls pinned delegates; launcher storage and pin/unpin use TasksModel. |
| `isolate-workspaces` | Implemented | `showOnlyCurrentDesktop` filters the native TasksModel. |
| `workspace-agnostic-urgent-windows` | Implemented | `workspaceAgnosticUrgent` is exposed through DockTasksModel to the native pre-grouping filter. Native Wayland model tests exercise enabled/disabled urgency exemptions. |
| `isolate-monitors` | Implemented | `showOnlyCurrentScreen` filters TasksModel against containment screen geometry. |
| `show-windows-preview` | Implemented | Native tooltip/group previews and PipeWire thumbnails; disabling previews retains a textual per-window context-menu list. KWin Window View provides the application-spread equivalent. |
| `default-windows-preview-to-open` | Implemented | `defaultPreviewsOpen` opens the native context-preview dialog first for running tasks, with access to the ordinary task menu; this replaces the source open preview submenu. |
| `preview-size-scale` | Implemented | `previewSizeScale` scales each preview from its actual window geometry, preserving aspect ratio within the output; zero keeps the theme default. |
| `hide-tooltip` | Implemented | `hideTooltip` independently suppresses hover tooltips while allowing explicit click-to-preview actions. |
| `minimize-shift` | Platform | Legacy schema boolean, unused by the current GNOME runtime; configure `shiftClickAction` instead. |
| `activate-single-window` | Platform | Legacy schema boolean, unused by the current GNOME runtime; actual source activation semantics are covered by the click actions. |
| `click-action` | Implemented | `clickAction` and `DockActions.resolve` map all twelve action names to native task, preview or KWin requests. |
| `shift-click-action` | Implemented | `shiftClickAction` uses the same named-action dispatcher. |
| `middle-click-action` | Implemented | `middleClickDockAction` uses the same dispatcher; the native task-manager enum remains separate. |
| `shift-middle-click-action` | Implemented | `shiftMiddleClickAction` uses the same named-action dispatcher. |
| `scroll-action` | Implemented | `dockScrollAction` selects none, application-window cycle or virtual-desktop switching. |
| `scroll-switch-workspace` | Platform | Legacy schema boolean, unused by the current GNOME runtime; its current behavior uses `scroll-action`, mapped to `dockScrollAction`. |
| `dance-urgent-applications` | Implemented | `danceUrgent` drives a repeated icon wiggle for native or LauncherEntry urgency. |
| `hot-keys` | Implemented | `hotKeys` enables applet shortcut dispatch and its shared KGlobalAccel registrations. |
| `app-hotkey-1` … `app-hotkey-10` | Implemented | All ten positions have configurable actions with Meta+number defaults. Existing Plasma defaults use a scoped native signal bridge without changing stored Plasma bindings; free/custom keys bind through `goshosdock-activate-N`. An existing KWin Meta+0 Zoom binding is preserved and reported, so the tenth default requires resolving that conflict in System Settings. Explicit custom and disabled bindings are retained; output routing uses authenticated compositor pointer geometry on Wayland. |
| `app-ctrl-hotkey-1` … `app-ctrl-hotkey-10` | Implemented | Shared KGlobalAccel Ctrl+Meta+number registrations request new instances; bindings can be changed in KDE System Settings. |
| `app-shift-hotkey-1` … `app-shift-hotkey-10` | Implemented | Shared KGlobalAccel Shift+Meta+number registrations invoke the configured alternate action. Default keys are normalized to the active KWin keymap where it provides unshifted digits; other layouts can capture an explicit binding in System Settings. Existing custom/disabled choices remain unchanged. |
| `hotkeys-show-dock` | Implemented | `hotkeysShowDock` requests panel attention during the shortcut timeout. |
| `hotkeys-overlay` | Implemented | `hotkeysOverlay` numbers the first ten eligible entries in displayed order: tasks, recent applications, devices and Trash; application/folder stacks are excluded. |
| `shortcut-text` | Platform | GNOME preference-editor helper; edit the corresponding KDE global shortcut in System Settings instead. |
| `shortcut` | Implemented | A shared KGlobalAccel action requests Meta+Q for showing the dock/number overlay and preserves user-modified bindings. An existing Plasma Activity Switcher Meta+Q assignment is preserved and reported; choose another dock key or reassign the conflicting native key in System Settings. |
| `shortcut-timeout` | Implemented | `shortcutTimeout` controls the reveal and number-overlay timer. |

The source action enum contains `skip`, `minimize`, `launch`, `cycle-windows`,
`minimize-or-overview`, `previews`, `minimize-or-previews`, `focus-or-previews`,
`focus-or-appspread`, `focus-minimize-or-previews`,
`focus-minimize-or-appspread` and `quit`. KDE's existing action enums must not be
treated as numerically interchangeable. GNOME Overview maps conceptually to KWin
Overview/Window View, but they are different desktop interactions. Test every
implemented action with no windows, one window and grouped windows, including a
minimized window and a window on another virtual desktop.

## Appearance and notifications

| GNOME key | Status | KDE implementation or remaining difference |
| --- | --- | --- |
| `apply-custom-theme` | Implemented | `visualStyle` provides Breeze, Glass, Minimal and Classic appearances; Shell CSS compatibility is not supplied. |
| `custom-theme-shrink` | Implemented | `compact` reduces task spacing. |
| `custom-background-color` | Implemented | `customBackgroundColor` independently enables the chosen background color; it is not inferred merely from choosing a preset. |
| `background-color` | Implemented | `customColor` supplies the custom dock background when enabled. |
| `transparency-mode` | Implemented | Theme, Fixed and Adaptive opacity are implemented. Adaptive uses actual dock position plus the source near-edge margin, full output span and desktop/activity/minimized/urgency filtering; Overview selects transparent alpha. Fade hiding keeps geometry stationary, so GNOME’s slide compensation is unnecessary. |
| `background-opacity` | Implemented | `backgroundOpacity` controls the widget background. Panel background is configured separately. |
| `customize-alphas` | Implemented | `customizeAlphas` selects explicit min/max alpha; otherwise the KDE appearance uses its default transparent/opaque treatment. |
| `min-alpha` | Implemented | `minAlpha` controls the free-floating adaptive opacity. |
| `max-alpha` | Implemented | `maxAlpha` controls adaptive opacity when the proximity model contains a relevant window. |
| `running-indicator-style` | Implemented | `indicatorStyle` offers Default, Dots, Squares, Dashes, Segmented, Solid, Ciliora, Metro, Binary and Dot; native theme colors are used. |
| `running-indicator-dominant-color` | Implemented | `dominantIndicatorColor` uses Kirigami ImageColors with a theme fallback. |
| `custom-theme-customize-running-dots` | Implemented | `customizeIndicators` enables explicit indicator colors and borders. |
| `custom-theme-running-dots-color` | Implemented | `indicatorColor` controls custom running indicators. |
| `custom-theme-running-dots-border-color` | Implemented | `indicatorBorderColor` controls the custom indicator border. |
| `custom-theme-running-dots-border-width` | Implemented | `indicatorBorderWidth` controls the border, bounded by rendered indicator thickness. |
| `force-straight-corner` | Implemented | Set `cornerRadius` to zero; panel shape remains a separate Plasma setting. |
| `unity-backlit-items` | Implemented | `unityBacklit` uses icon-derived color for per-icon backlighting. |
| `apply-glossy-effect` | Implemented | `glossyIcons` controls the backlit icon gloss treatment. |
| `show-icons-emblems` | Implemented | `showEmblems` controls native SmartLauncher count/progress overlays, including magnified icons. |
| `show-icons-notifications-counter` | Implemented | `showNotificationCounter` counts native notifications by desktop entry, excluding read resident notifications; application counters remain independently available. |
| `application-counter-overrides-notifications` | Implemented | `applicationCounterOverridesNotifications` selects a positive visible application count or the sum of application/notification counts. |

The native SmartLauncher backend observes KDE notification inhibition, groups
notification counts by desktop entry and consumes Unity LauncherEntry counters,
progress, urgency and updating state. The source’s acknowledged resident rule maps
to KDE’s resident/read roles. Actual private-bus Notify/LauncherEntry tests cover
retained/resident notifications, DND suppression and restoration, sender exit,
and coexistence with a stock Plasma Unity observer, including alias mappings.
Dynamic DBusMenu quicklists have their own subscription and action implementation.

## Places and macOS additions

| GNOME key | Status | KDE implementation or remaining difference |
| --- | --- | --- |
| `show-trash` | Implemented | `showTrash` adds a live empty/full trash item; KIO opens/empties it after UI confirmation. |
| `show-mounts` | Implemented | `showDevices` adds device entries from native KFilePlacesModel, including setup/unmount/eject actions. |
| `show-mounts-only-mounted` | Implemented | `devicesOnlyMounted` filters entries that require setup. |
| `show-mounts-network` | Implemented | `showNetworkDevices` merges live GIO mounts and mountable/ejectable network volumes with KDE remote Places, including unbookmarked and unmounted volumes. Stable identities survive mounting; native credential/question dialogs, cancellation, errors and disconnect/eject are supported. |
| `isolate-locations` | Integration | `isolateLocations` maps folder/device/Trash URLs to native Dolphin windows and excludes those windows from the ordinary task group. Full subtree/all-tab matching requires the shipped Dolphin patch; stock Dolphin supports exact URLs only. |
| `magnification-enabled` | Implemented | Task and extras icons use separate output-only magnification surfaces, preserving panel reservation; physical mixed-scale placement still needs validation. |
| `magnification-factor` | Implemented | `magnificationFactor` preserves the original 1–3 scale range and 1.5 default. |
| `magnification-range` | Implemented | `magnificationSpread` preserves the original 1–6 numeric range and default 2 for neighboring task icons. |
| `show-show-apps-button` | Implemented | `showApplications` adds the Applications button. |
| `show-apps-at-top` | Implemented | `showAppsAtStart` moves the application button to the leading section. |
| `show-apps-always-in-the-edge` | Implemented | `appsAlwaysAtEdge` keeps the Applications button at the outer edge while the extended panel’s task/places group can remain centered. |
| `show-apps-button-action` | Implemented | `showAppsAction` selects Plasma's launcher or the application stack; a launcher must be available in the Plasma session. |
| `show-applications-stack` | Implemented | `applicationsStack` creates a separate application stack from native KDE application services. |
| `show-documents-stack` | Implemented | `documentsStack` uses the standard Documents location. |
| `show-downloads-stack` | Implemented | `downloadsStack` uses the standard Downloads location. |
| `show-home-stack` | Implemented | `homeStack` uses the user's home directory. |
| `custom-stacks` | Implemented | `customFolders` keeps ordered folder URLs; KIO asynchronously lists local and remote locations. |
| `stack-view` | Implemented | `stackView` supports Automatic/Fan/Grid/List; Automatic chooses fan for small folders and grid for applications. Explicit Applications Fan remains available. |
| `stack-sort` | Implemented | `stackSort` supports Name, Modified and Kind, with name tie-breaking. |
| `stack-view-overrides` | Implemented | `stackOverrides` JSON stores each stack's view by stable stack key; context menu supports resetting to default. |
| `stack-sort-overrides` | Implemented | The same persistent per-stack map stores independent sort overrides. |
| `stack-max-items` | Implemented | `stackItemLimit` limits displayed entries and shows overflow count; fan view caps at ten. |
| `show-stacks-separator` | Implemented | `stacksDivider` adds a separator before stacks/places. |
| `show-recent-applications` | Implemented | `showRecentApps` uses frequency-ranked KActivities usage to match GNOME’s most-used selection, excluding pinned/running IDs and respecting parental controls and usage-recording privacy. Histories are platform-specific. |
| `recent-applications-limit` | Implemented | `recentAppsLimit` bounds the recent application section. |
| `launch-bounce-animation` | Implemented | `launchBounce` animates startup task icons on the overlay surface. |
| `dim-hidden-applications` | Implemented | `dimMinimized` dims minimized application icons; grouped tasks use the native all-minimized group role. |
| `hidden-applications-opacity` | Implemented | `minimizedOpacity` controls the dimmed icon opacity. |

## Behavior not represented by a schema key

| Feature | Status and implementation |
| --- | --- |
| Pin/unpin, manual launcher ordering and drag/drop | Implemented through native TaskManager requests, launcher persistence and external URL/file drops. Native menu pin/unpin, pointer drag reordering and shell-restart persistence passed. External drops, keyboard-only interaction and a full logout/login sweep remain acceptance checks. |
| Desktop-file actions and new instance | Implemented using native application launcher jobs and the task context menu; updating applications block launch paths. |
| Window close, move, resize, maximize and desktop assignment | Implemented through native task requests and context menus; window movement remains subject to compositor policy. |
| Dynamic Unity DBusMenu quicklists | Implemented by QuicklistBackend with live layout/submenu/toggle/action updates and sender cleanup; exercised against an actual private-bus provider. Desktop-file actions are separate. |
| Unity LauncherEntry `updating` | Implemented remote state, icon dimming and launch guards on task/shortcut/drop/context/stack/recent paths. KDE retains window-management menu actions instead of copying GNOME’s update-only menu layout. |
| Integrated/discrete GPU launch | Implemented through SwitcherooControl GPU discovery and native KIO launch jobs with the selected GPU environment. Private-bus/argv/environment tests pass; actual dual-GPU hardware launch remains untested. |
| Application details | KDE replacement: resolve installed AppStream metadata and open Discover; fall back to the desktop file’s Properties dialog. GNOME Software/Snap-specific integration is not copied. |
| File-manager location windows | Implemented native activate/minimize/cycle/close, preview and window-view requests for authenticated Dolphin window identities. Full discovery uses the supplied patch; ambiguous Wayland process/window matches are left in the ordinary task list. |
| Desktop-icons avoidance/work-area reservation | Fixed mode uses Plasma’s exclusive area. Magnification uses separate input-transparent surfaces so visual growth does not enlarge panel reservation. Desktop-icons behavior across real outputs still needs acceptance testing. |
| Parental-control visibility | Implemented shared libmalcontent/AccountsService application filter, used by running/pinned tasks, application stacks and usage results. Service metadata visibility remains respected separately. |
| Localization, keyboard and screen-reader access | Kirigami/native controls, accessible labels, stack keyboard navigation and Escape handling are present. Translation catalogs, RTL, touch and screen-reader interaction still need functional review. |
| Existing GNOME preferences | Implemented review-only JSON/GSettings-literal converter with all 127 keys accounted for, source defaults, favorites, custom folders, per-stack preferences, launcher aliases and three shortcut families. It does not silently alter a running desktop; see [migration instructions](MIGRATION.md). |

The KDE port also retains native task-manager capabilities such as Activities,
recent-document/Places actions, MPRIS media controls and per-application audio
mute. These additions are separate from the retained GNOME feature set.

Implementation entry points are [`main.qml`](../src/qml/main.qml),
[`Task.qml`](../src/qml/Task.qml),
[`DockActions.js`](../src/qml/code/DockActions.js),
[`DockExtras.qml`](../src/qml/DockExtras.qml),
[`StackPopup.qml`](../src/qml/StackPopup.qml),
[`StackBackend`](../src/stackbackend.cpp),
[`SmartLauncher`](../src/smartlauncherbackend.cpp),
[`QuicklistBackend`](../src/quicklistbackend.cpp),
[`GlobalShortcuts`](../src/globalshortcuts.cpp) and
[`main.xml`](../src/main.xml). Additional policy/integration entry points are
[DockController](../src/dockcontroller.cpp), [PanelLayout](../src/panellayout.cpp),
[DockTasksModel](../src/docktasksmodel.cpp), [LocationBackend](../src/locationbackend.cpp),
[RemoteMounts](../src/remotemounts.cpp), [ApplicationActions](../src/applicationactions.cpp),
[ApplicationFilter](../src/applicationfilter.cpp), and the
[KWin companion](../kwin/README.md).

## Evidence and remaining boundaries

The final SDK integration run passed **20/20 CTest targets**. This is a
build/component result, not twenty desktop scenarios. Targeted and real-session checks are
recorded separately; see [VALIDATION.md](VALIDATION.md) for their exact scope:

- The real KWin Wayland fixture passed **9 QtTest cases** including initialization
  and cleanup: manual hiding/restoration, autohide, animation, a real fullscreen
  window, raw EIS pressure on all four edges/dwell, foreign-owner rejection,
  non-dock rejection, compositor pointer-output geometry and authorized native
  keymap access with rejection of foreign callers. The deterministic policy suite separately covers all four
  hide modes and timing priorities. Physical input, native Overview activation,
  mixed scales and output hotplug are not established by these fixtures.
- A clean build/install/private-session run loaded the actual compiled applet
  and registered its panel surface with the real companion. The reusable setup
  completed from a stopped container without manual process cleanup. The final
  integration rerun passed **20/20 CTest targets**, **9 visibility + 8 model
  cases**, and actual applet registration.
- The shortcut component suite passed **20 cases**, including
  authoritative compositor output routing, keymap-normalized defaults and
  native-default/custom/disabled preference persistence. An isolated real KWin/Xvfb injected-input run passed
  Meta+1, Ctrl+Meta+1/0, Shift+Meta+1/0, Meta+Q, a custom alternate binding and an
  explicitly disabled binding using a controlled keymap provider. A real native
  `plasmashell` KGlobalAccel action also verified scoped Meta+1 routing, native
  restoration for custom/disabled dock bindings and unchanged stored native
  shortcuts.
- The final installed applet passed the native-provider injected-input matrix:
  independent output-specific Meta+1 activation, Ctrl+Meta+1 new-instance routing
  while pointing over another client, Shift+Meta+1 minimization, primary targeting
  for shared all-output copies and a sole-secondary fallback. A temporary custom
  Meta+Alt+Q displayed badges on both copies. All ten native Plasma bindings were
  unchanged; existing Meta+0 Zoom and Meta+Q Activity Switcher assignments were
  preserved and reported. Physical input and reveal timing remain outside this
  bounded check.
- The implemented native keymap provider passed **9 actual-XKB cases** covering
  US/GB/German punctuation, active-group changes, French AZERTY's lack of
  unshifted-digit defaults, invalid inputs and preservation of live modifiers.
  Layouts without generated defaults retain existing bindings and allow explicit
  shortcut capture in System Settings.
- The compiled-popup suite passed **8 cases**, loading the actual module's task
  and recent context menus, preview dialogs, grouped tasks and stack/location
  popups through QQmlEngine. This checks dynamic component readiness separately
  from rendered interaction.
- The real Wayland native task-model suite passed **8 cases**, including location
  exclusion before grouping and launcher restoration.
- The patched **Dolphin 26.08.2** process published both split-pane URLs, then all
  three URLs after opening an inactive tab, with a real PropertiesChanged signal
  on a private bus. Dock-side location tests also exercise stock exact-URL fallback,
  authenticated identity, native action routing and stale/ambiguous replies.
- RemoteMounts passed **13 cases with Qt’s GLib dispatcher and 13 with QT_NO_GLIB=1**,
  using actual GVolumeMonitor/GMount interfaces and asynchronous GTask providers.
  These exercise live mount/volume discovery, stable identity across mounting,
  connect/disconnect/eject routing, errors, native credential/question dialogs and
  cancellation/destruction without changing the host’s mounts.
- After the live Plasma coexistence findings, PanelLayout’s typed-config-read
  regression suite passed **9 cases**, and the Notify/Unity suite passed **12**,
  including an already-owned `/Unity` object and launcher aliases. These focused
  fixes were included in the following assembled-app rebuild/reload.

- The assembled applet’s **two-output replication** test created one copy per
  output, propagated settings in both directions, disabled replication from a
  copy, removed only that generated copy, and preserved the unrelated standard
  Plasma panel. Source settings were restored afterward. A separate live sweep
  verified panel location and rendered orientation on all four edges. These are
  controlled logical outputs, not physical hotplug or mixed-scale verification.

QtTest counts include initialization, cleanup and any data rows. The assembled
applet was exercised separately; a successful component or compositor test
never marks the broad manual checklist below complete. Full location parity is
conditional on installing the Dolphin integration; complete dock policy is
conditional on the matching KWin companion. Source review also corrected urgent
grouped-window selection, modifier double-click minimization, Overview click
policy, adaptive-opacity geometry and textual window lists when previews are off.
Active native Overview rendering/dispatch remains untested here: the software
compositor reports that effect unsupported without a DRM render device.

## Completed session checks

These are bounded checks in the disposable Plasma Wayland session, not a claim
that every interaction on physical hardware has been covered.

- [x] Build/install from the reusable SDK workflow, load the actual applet in a
  fresh private Plasma session and register its panel with the real KWin companion.
- [x] Observe real KWin manual-hide/fixed visibility on the assembled dock.
- [x] Change the panel among all four edges and inspect rendered orientation.
- [x] Create two-output copies, propagate settings both ways, disable from a copy
  and preserve the unrelated standard panel.
- [x] Exercise Fan/Grid/List folder stacks, hidden/backup filtering, overflow,
  keyboard search/activation and mouse activation against real Dolphin.
- [x] Move a real Dolphin tab into/out of a tracked folder and observe native
  location actions and restoration of the ordinary application task.
- [x] Render the task context menu and location preview fallback.
- [x] Pin/unpin through the activity-aware native menu and perform three injected
  pointer drag reorders, verifying the stored launcher list.
- [x] Restart the shell and retain exact launcher order `[Dolphin, System Settings]`,
  Classic style, icon size 40 and fixed icon sizing.
- [x] Restart the shell twice and retain the native-default shortcut marker and
  Meta+1 dock routing from an independently activated window, with all ten native
  Plasma bindings unchanged. Original primary preferences were restored.
- [x] Populate Move to Desktop with two actual desktops and remove the temporary
  desktop after the check.
- [x] Render textual window entries with previews disabled, persistent All Windows
  previews, the return to Application Actions, immediate default previews and
  hover title/icon content. Live GPU thumbnail pixels remain unverified.
- [x] Close the final window through its preview button for pinned and unpinned
  tasks, dismissing both application and preview, and render delayed fallback icons.
- [x] Open the stopped recent-application menu repeatedly, launch while it is
  open, then exit/reopen the application without a shell crash or QML errors.
- [x] Render all four styles: Breeze, Glass, Minimal and Classic. Verify Minimal
  removes the dock's native rectangle while preserving the standard panel background.
- [x] Inject default, alternate, new-instance, overlay, custom and disabled
  shortcut cases in an isolated KWin session with the scope described above.
- [x] Exercise native-provider shortcuts on the installed applet across both
  independent and shared-output docks, including new instances, minimization,
  sole-secondary fallback and a custom overlay on both copies.
- [x] Display both native shortcut conflicts using pointer and keyboard activation
  of the notice, preserving native assignments and showing the complete notice
  header below the panel.
- [x] Run an isolated real KIO Trash backend lifecycle with empty/full change
  signals, exact fixture content/listing and successful emptying, using a fresh
  UID/private profile. The GUI confirmation dialog is not covered by this check.
- [x] Run actual compositor fullscreen/autohide/pressure and native model fixtures;
  see the evidence above for exactly which cases executed.

## Remaining desktop acceptance

Record Plasma/Frameworks/Qt versions, output scales and results. The tests above
remain complete; these checks extend their coverage.

- [ ] Complete a full logout/login persistence sweep and external file/URL
  drag/drop interactions, including keyboard operation, extending the verified
  shell restart and launcher reordering.
- [ ] Exercise every click/modifier/scroll action across one/multiple/minimized,
  urgent and other-desktop windows with the assembled UI.
- [ ] Extend the completed shortcut matrix to physical keyboard layouts, every
  action combination and reveal timing. Existing Meta+0 Zoom and Meta+Q Activity
  Switcher assignments can be changed in System Settings before assigning those
  keys to the dock; alternative dock bindings also work.
- [ ] Validate native Overview/Window View and live PipeWire/GPU thumbnails with
  working graphics acceleration, stream quality and disabled effects.
- [ ] Test physical output unplug/replug, primary changes, negative coordinates,
  100%/150%/200% mixed scaling and popup placement on every edge.
- [ ] Check physical pointer/touch feel, every magnification range, work-area and
  desktop-icon reservation, compositor restart and companion removal recovery.
- [ ] Test actual removable hardware and network servers, unavailable volumes,
  credential cancellation, mount/unmount/eject and error presentation.
- [ ] Exercise the Trash confirmation dialog, remote/inaccessible/very large
  folders and desktop restart persistence for independent stack preferences.
- [ ] Verify all indicator presets, custom alpha/colors, light/dark themes and
  contrast, extending the completed four-style rendering check.
- [ ] Exercise screen readers, RTL/localized names, touch and full keyboard
  navigation in settings, stacks, previews and menus.

Complete acceptance remains conditional on the documented companion integrations
and the unperformed interaction checks above. GNOME startup-overview policy and
unused legacy keys are platform or dead-source settings, not unimplemented live
KDE controls.
