/* SPDX-FileCopyrightText: 2026 Gosh OS contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
.pragma library

// Keep input policy separate from compositor requests so every click mode can
// be exercised without a window manager. "skip" is the original focus action.
function resolve(action, state) {
    if (!state.running || state.control || action === "launch") return "launch";
    const single = state.count <= 1 || state.urgent;
    const plain = !state.modified && !state.middle;
    if (!state.spreadAvailable) {
        if (action === "focus-or-appspread") action = "focus-or-previews";
        if (action === "focus-minimize-or-appspread") action = "focus-minimize-or-previews";
    }
    switch (action) {
    case "minimize":
        if (state.overviewVisible && !state.modified) return "activate";
        return (state.active && !state.urgent) || !plain ? "minimize" : "activate";
    case "cycle-windows":
        if (state.overviewVisible) return "activate";
        return state.active && !state.urgent ? "cycle" : "activate";
    case "minimize-or-overview":
        return single && plain ? (state.active ? "minimize" : "activate") : "overview";
    case "previews":
        if (state.overviewVisible) return "activate";
        return single && plain ? "activate" : "previews";
    case "minimize-or-previews":
        if (state.overviewVisible) return "activate";
        return single && plain ? (state.active ? "minimize" : "activate") : "previews";
    case "focus-or-previews":
        return state.active && !state.urgent && (!single || !plain) ? "previews" : "activate";
    case "focus-minimize-or-previews":
        if (!state.active || state.urgent) return "activate";
        return !single || !plain ? "previews" : state.overviewVisible ? "none" : "minimize";
    case "focus-or-appspread":
        return state.active && !single && plain ? "spread" : "activate";
    case "focus-minimize-or-appspread":
        if (state.active && !single && plain) return "spread";
        return state.active ? "minimize" : "activate";
    case "quit": return "close";
    default: return "activate";
    }
}

function nextDesktop(ids, current, direction) {
    if (!ids.length) return null;
    const index = ids.indexOf(current);
    return ids[(Math.max(0, index) + direction + ids.length) % ids.length];
}
