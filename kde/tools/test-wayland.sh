#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Exercise real Wayland windows against an isolated KWin, never the user's session.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir=$(realpath "${1:-$script_dir/../build}")
for program in dbus-run-session timeout kwin_wayland plasmashell qdbus6 kbuildsycoca6; do
    command -v "$program" >/dev/null || { echo "Missing runtime dependency: $program" >&2; exit 1; }
done
for binary in tst_dockvisibility tst_docktasksmodel; do
    test -x "$build_dir/bin/$binary" || { echo "Build $binary with BUILD_TESTING=ON first." >&2; exit 1; }
done
state_dir=$(mktemp -d "${TMPDIR:-/tmp}/goshos-wayland.XXXXXXXX")
logs_dir=$(mktemp -d "$build_dir/wayland-tests.XXXXXXXX")
cleanup_outer() { rm -rf -- "$state_dir"; }
trap cleanup_outer EXIT
printf 'Wayland test logs: %s\n' "$logs_dir"
# The bus daemon must inherit the private profile too: activated services do not
# inherit environment changes made later inside its child shell.
export XDG_RUNTIME_DIR="$state_dir/runtime"
export XDG_CONFIG_HOME="$state_dir/config"
export XDG_DATA_HOME="$state_dir/data"
export XDG_CACHE_HOME="$state_dir/cache"
export XDG_STATE_HOME="$state_dir/state"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
# The private bus and unique socket are mandatory: the tests manipulate real
# windows and intentionally inject pointer motion inside this disposable session.
env -u DISPLAY -u WAYLAND_DISPLAY timeout --kill-after=10s 180s \
    dbus-run-session -- bash -s -- "$build_dir" "$state_dir" "$logs_dir" <<'SESSION'
set -euo pipefail
build_dir=$1
state_dir=$2
logs_dir=$3
printf '[Daemon]\nAutolock=false\nLockOnResume=false\n' > "$XDG_CONFIG_HOME/kscreenlockerrc"
# KWin grants privileged window-management protocols by the executable named in
# a desktop file. Register only this fixture, only in this temporary data dir.
export XDG_MENU_PREFIX=plasma-
export XDG_CURRENT_DESKTOP=KDE
mkdir -p "$XDG_DATA_HOME/applications"
cat > "$XDG_DATA_HOME/applications/org.gosh.goshosdock-task-tests.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Goshos Dock task integration test
Exec=$build_dir/bin/tst_docktasksmodel
Icon=application-x-executable
NoDisplay=true
Categories=Utility;
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_plasma_virtual_desktop_management
DESKTOP
kbuildsycoca6 --noincremental > "$logs_dir/kbuildsycoca.log" 2>&1
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export WAYLAND_DISPLAY=goshos-test-wayland
export QT_QUICK_BACKEND=software
export QT_FORCE_STDERR_LOGGING=1
export LIBGL_ALWAYS_SOFTWARE=1
export QT_PLUGIN_PATH="$build_dir/bin:$build_dir/lib${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
children=()
cleanup_session() {
    if ((${#children[@]})); then
        kill "${children[@]}" 2>/dev/null || true
        wait "${children[@]}" 2>/dev/null || true
    fi
}
trap cleanup_session EXIT
kwin_wayland --virtual --width 1440 --height 900 --output-count 2 \
    --socket "$WAYLAND_DISPLAY" > "$logs_dir/kwin.log" 2>&1 &
kwin_pid=$!
children+=("$kwin_pid")
ready=false
for ((attempt=0; attempt<100; attempt++)); do
    if ! kill -0 "$kwin_pid" 2>/dev/null; then cat "$logs_dir/kwin.log" >&2; exit 1; fi
    if [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && \
        qdbus6 org.gosh.GoshosDock.KWin /org/gosh/GoshosDock \
            org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 0.1
done
if [[ $ready != true ]]; then
    echo 'KWin did not expose the dock plugin within ten seconds.' >&2
    cat "$logs_dir/kwin.log" >&2
    exit 1
fi
export QT_QPA_PLATFORM=wayland
if command -v dbus-update-activation-environment >/dev/null; then
    dbus-update-activation-environment WAYLAND_DISPLAY XDG_RUNTIME_DIR \
        XDG_CURRENT_DESKTOP XDG_SESSION_TYPE QT_QPA_PLATFORM
fi
activity_daemon=$(command -v kactivitymanagerd || true)
if [[ -z $activity_daemon && -x /usr/lib/kactivitymanagerd ]]; then
    activity_daemon=/usr/lib/kactivitymanagerd
fi
if [[ -n $activity_daemon ]]; then
    "$activity_daemon" > "$logs_dir/activities.log" 2>&1 &
    children+=("$!")
fi
export GOSHOS_DISPOSABLE_KWIN_TEST=1
export GOSHOS_DOCK_INTEGRATION=1
for binary in tst_dockvisibility tst_docktasksmodel; do
    selected_functions=()
    if [[ $binary == tst_dockvisibility ]]; then
        while IFS= read -r function; do
            function=${function%()}
            if [[ $function == nativeOverviewRevealsManualHiddenDock ]] && \
                [[ $(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectSupported overview) != true ]]; then
                echo 'Not exercised: native Overview visual effect requires an OpenGL compositor/DRM render device.' | tee "$logs_dir/unsupported-overview.txt"
                continue
            fi
            [[ -n $function ]] && selected_functions+=("$function")
        done < <("$build_dir/bin/$binary" -functions)
    fi
    if ! timeout --kill-after=5s 60s "$build_dir/bin/$binary" "${selected_functions[@]}" -o "$logs_dir/$binary.txt",txt; then
        cat "$logs_dir/$binary.txt"
        exit 1
    fi
    cat "$logs_dir/$binary.txt"
    if grep -q '^SKIP' "$logs_dir/$binary.txt"; then
        echo "Unexpected skipped integration case: $binary" >&2
        exit 1
    fi
done
# Loading the actual compiled applet catches missing QRC imports and runtime QML
# errors that C++/qmlcachegen alone cannot detect. A controller token is written
# only after the real KWin bridge has acknowledged this applet's own surface.
plasmashell --no-respawn > "$logs_dir/plasmashell.log" 2>&1 &
plasma_pid=$!
children+=("$plasma_pid")
shell_ready=false
for ((attempt=0; attempt<100; attempt++)); do
    if ! kill -0 "$plasma_pid" 2>/dev/null; then cat "$logs_dir/plasmashell.log" >&2; exit 1; fi
    if qdbus6 org.kde.plasmashell /PlasmaShell org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
        shell_ready=true
        break
    fi
    sleep 0.1
done
[[ $shell_ready == true ]] || { cat "$logs_dir/plasmashell.log" >&2; exit 1; }
qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
    var panel = new Panel;
    panel.location = "bottom";
    panel.height = 64;
    var applet = panel.addWidget("org.gosh.goshosdock");
    applet.currentConfigGroup = ["General"];
    applet.writeConfig("dockFixed", true);
    applet.writeConfig("allOutputs", false);
    applet.writeConfig("manualHide", false);
    applet.reloadConfig();
    print("Created panel " + panel.id + ", applet " + applet.id);
' > "$logs_dir/applet-created.txt"
applet_ready=false
for ((attempt=0; attempt<100; attempt++)); do
    ready=$(qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
        var found = false;
        var ps = panels();
        for (var i = 0; i < ps.length; ++i) {
            var ws = ps[i].widgets();
            for (var j = 0; j < ws.length; ++j) {
                if (ws[j].type !== "org.gosh.goshosdock") continue;
                ws[j].currentConfigGroup = ["NativeDockController"];
                if (!!ws[j].readConfig("controllerToken", "")) found = true;
            }
        }
        print(found);
    ')
    if [[ $ready == true ]]; then applet_ready=true; break; fi
    sleep 0.1
done
if [[ $applet_ready != true ]]; then
    echo 'The applet failed to load and register its real Wayland panel.' >&2
    qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
        var ps = panels();
        for (var i = 0; i < ps.length; ++i) {
            var ws = ps[i].widgets();
            for (var j = 0; j < ws.length; ++j) {
                ws[j].currentConfigGroup = ["NativeDockController"];
                print(JSON.stringify({panel:ps[i].id,widget:ws[j].id,type:ws[j].type,keys:ws[j].configKeys,managed:!!ws[j].readConfig("controllerToken", "")}));
            }
        }
    ' >&2
    cat "$logs_dir/plasmashell.log" >&2
    exit 1
fi
if grep -E 'error when loading applet "org.gosh.goshosdock"|qrc:/qt/qml/plasma/applet/org/gosh/goshosdock/.*(TypeError|ReferenceError|Cannot assign|unavailable)' "$logs_dir/plasmashell.log"; then
    echo 'The installed applet reported a QML runtime error.' >&2
    exit 1
fi
shortcut_pid=$(qdbus6 org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus.GetConnectionUnixProcessID org.kde.kglobalaccel)
for variable in XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME; do
    if ! tr '\0' '\n' < "/proc/$shortcut_pid/environ" | grep -Fx "$variable=${!variable}" > /dev/null; then
        echo "Activated shortcut service did not inherit private $variable." >&2
        exit 1
    fi
done
printf 'PASS: D-Bus-activated shortcut service inherited the private XDG profile.\n'
printf 'PASS: compiled applet loaded in plasmashell and registered with the real KWin plugin.\n'
SESSION
