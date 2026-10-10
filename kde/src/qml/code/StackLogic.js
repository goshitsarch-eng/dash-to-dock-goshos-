/* SPDX-License-Identifier: GPL-2.0-or-later */
.pragma library

// Keep persistent state keyed by the stack URL, rather than its position in the dock.
function parseOverrides(serialized) {
    try {
        const value = JSON.parse(serialized || "{}");
        return value && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (error) {
        return {};
    }
}

function preference(serialized, key, field, fallback) {
    const entry = parseOverrides(serialized)[key];
    const value = entry && entry[field];
    const maximum = field === "view" ? 3 : 2;
    return Number.isInteger(value) && value >= 0 && value <= maximum ? value : fallback;
}

function setPreference(serialized, key, field, value) {
    const preferences = parseOverrides(serialized);
    const previous = preferences[key];
    const entry = previous && typeof previous === "object" ? Object.assign({}, previous) : {};
    if (value < 0) {
        delete entry[field];
    } else {
        entry[field] = value;
    }
    if (Object.keys(entry).length > 0) {
        preferences[key] = entry;
    } else {
        delete preferences[key];
    }
    return JSON.stringify(preferences);
}

function withoutStack(serialized, key) {
    const preferences = parseOverrides(serialized);
    delete preferences[key];
    return JSON.stringify(preferences);
}

function nameForUrl(url) {
    const clean = String(url || "").replace(/\/+$/, "");
    const name = clean.slice(clean.lastIndexOf("/") + 1);
    try {
        return decodeURIComponent(name) || clean;
    } catch (error) {
        return name || clean;
    }
}

function applicationId(value) {
    const identifier = String(value || "").replace(/^applications:/, "").replace(/^file:\/\//, "").split("/").pop();
    try {
        return decodeURIComponent(identifier).replace(/\.desktop$/, "");
    } catch (error) {
        return identifier.replace(/\.desktop$/, "");
    }
}

// Hidden running tasks still own the native window actions of a recent app.
function applicationTask(tasks, value) {
    const id = applicationId(value);
    if (!id) return null;
    const matches = Array.from(tasks || []).filter(task => task && task.model
        && [task.model.AppId, task.model.LauncherUrlWithoutIcon, task.model.LauncherUrl]
            .some(candidate => candidate && applicationId(candidate) === id));
    return matches.find(task => task.model.IsWindow || task.model.IsGroupParent) || matches[0] || null;
}

function shortcutTargets(tasks, extras) {
    return Array.from(tasks || []).filter(task => task && task.visible && typeof task.modelIndex === "function")
        .concat(Array.from(extras || []).filter(item => item && item.visible && item.entry
            && ["recent", "trash", "device"].includes(item.entry.kind)));
}

function shortcutNumber(targets, target) {
    const index = Array.from(targets || []).indexOf(target);
    return index >= 0 && index < 10 ? (index + 1) % 10 : -1;
}

function recentApplications(entries, exclusions, limit) {
    const excluded = new Set(Array.from(exclusions || []).map(applicationId));
    const seen = new Set();
    return Array.from(entries || []).filter(entry => {
        const id = applicationId(entry.desktopId || entry.url);
        if (!id || excluded.has(id) || seen.has(id)) {
            return false;
        }
        seen.add(id);
        return true;
    }).slice(0, Math.max(0, Number(limit) || 0));
}

function visibleEntries(entries, sort, search, applications) {
    const needle = String(search || "").trim().toLocaleLowerCase();
    const filtered = Array.from(entries || []).filter(entry => {
        const name = String(entry.name || "");
        return name && (applications || (!entry.hidden && !name.startsWith(".") && !name.endsWith("~")))
            && (!needle || name.toLocaleLowerCase().includes(needle));
    });
    // Never reorder a model owned by the backend.
    return filtered.sort((a, b) => {
        if (!applications && sort === 1) {
            const delta = (Number(b.modified) || 0) - (Number(a.modified) || 0);
            if (delta !== 0) {
                return delta;
            }
        } else if (!applications && sort === 2) {
            const kind = String(a.mimeType || "").localeCompare(String(b.mimeType || ""));
            if (kind !== 0) {
                return kind;
            }
        } else if (!applications && sort === 0 && !!a.isDir !== !!b.isDir) {
            return a.isDir ? -1 : 1;
        }
        return String(a.name).localeCompare(String(b.name));
    });
}

function effectiveView(requested, count, applications) {
    if (applications) {
        return requested === 0 ? 2 : requested;
    }
    return requested === 0 ? (count <= 10 ? 1 : 2) : requested;
}

function itemLimit(configured, view) {
    const bounded = Math.max(1, Math.min(500, Number(configured) || 500));
    return view === 1 ? Math.min(10, bounded) : bounded;
}
