// SPDX-FileCopyrightText: 2026 Goshos Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library

function bounded(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, Number.isFinite(value) ? value : minimum));
}

// A configured scale is a fraction of the window, not of its output. Automatic
// previews fit the source dock's 250 x 150 box without enlarging small windows.
function previewSize(width, height, scale, availableWidth, availableHeight) {
    const w = Number.isFinite(width) && width > 0 ? width : 250;
    const h = Number.isFinite(height) && height > 0 ? height : 150;
    const requested = scale > 0 ? bounded(scale, 0, 1) : Math.min(1, 250 / w, 150 / h);
    const factor = Math.min(requested, Math.max(1, availableWidth) / w,
        Math.max(1, availableHeight) / h);
    return {width: Math.max(1, Math.round(w * factor)), height: Math.max(1, Math.round(h * factor))};
}

// Use the same size steps as the GNOME dock, plus the user's exact maximum.
// Fixed icons may still be capped by panel thickness, but never by its length.
function chooseIconSize(requestedSize, count, availableLength, thickness, alongPadding, crossPadding, fixed) {
    const maximum = bounded(requestedSize, 16, 128);
    const crossLimit = Math.max(0, Math.min(maximum, thickness - Math.max(0, crossPadding)));
    if (fixed || count <= 0) {
        return crossLimit;
    }
    const limit = Math.min(crossLimit, Math.max(0, availableLength) / count - Math.max(0, alongPadding));
    const candidates = [16, 22, 24, 32, 48, 64, 96, 128].filter(size => size < maximum);
    candidates.push(maximum);
    let result = Math.min(16, crossLimit);
    for (const size of candidates) {
        if (size <= limit) {
            result = size;
        }
    }
    return result;
}

function dockLayout(count, availableLength, thickness, requestedSize, fixed, alongPadding, crossPadding, centered) {
    const items = Math.max(0, Math.floor(count));
    const viewport = Math.max(0, availableLength);
    const iconSize = chooseIconSize(requestedSize, items, viewport, thickness, alongPadding, crossPadding, fixed);
    const cellSize = iconSize + Math.max(0, alongPadding);
    const contentLength = items * cellSize;
    return {
        iconSize: iconSize,
        cellSize: cellSize,
        thickness: Math.max(0, thickness),
        contentLength: contentLength,
        preferredLength: items * (bounded(requestedSize, 16, 128) + Math.max(0, alongPadding)),
        offset: centered ? Math.max(0, (viewport - contentLength) / 2) : 0,
        overflow: contentLength > viewport
    };
}

// Return the smallest scroll that fully reveals an item, including an optional
// breathing margin. Oversized items align at their beginning without oscillation.
function revealOffset(start, size, offset, viewportLength, contentLength, margin) {
    const maximum = Math.max(0, contentLength - viewportLength);
    let result = bounded(offset, 0, maximum);
    const padding = Math.max(0, margin || 0);
    if (size + 2 * padding >= viewportLength || start - padding < result) {
        result = start - padding;
    } else if (start + size + padding > result + viewportLength) {
        result = start + size + padding - viewportLength;
    }
    return bounded(result, 0, maximum);
}

// Pointer and center are measured in the same, unmagnified dock coordinates.
// A cosine falloff keeps the nearest icon largest without changing panel size.
function magnificationScale(pointer, center, spacing, factor, spread, enabled) {
    if (!enabled || !Number.isFinite(pointer) || !Number.isFinite(center) || spacing <= 0) {
        return 1;
    }
    const radius = spacing * bounded(spread, 1, 6);
    const distance = Math.abs(pointer - center);
    if (distance >= radius) {
        return 1;
    }
    return 1 + (bounded(factor, 1, 3) - 1) * (1 + Math.cos(Math.PI * distance / radius)) / 2;
}

// The canvas is a separate, input-transparent surface. Its screen-facing side
// stays aligned to the resting icon; growth and bounce go toward the desktop.
function overlayPosition(x, y, baseSize, canvasSize, edge) {
    const centered = (baseSize - canvasSize) / 2;
    return {
        x: x + (edge === "left" ? 0 : edge === "right" ? baseSize - canvasSize : centered),
        y: y + (edge === "top" ? 0 : edge === "bottom" ? baseSize - canvasSize : centered)
    };
}

function iconPosition(size, canvasSize, edge, bounce) {
    const centered = (canvasSize - size) / 2;
    return {
        x: edge === "left" ? bounce : edge === "right" ? canvasSize - size - bounce : centered,
        y: edge === "top" ? bounce : edge === "bottom" ? canvasSize - size - bounce : centered
    };
}

// Rectangles in a horizontal strip, rotated by DockIndicator for other edges.
function indicatorSegments(style, windowCount, length, thickness, active) {
    if (style <= 0 || style > 9 || windowCount <= 0 || length <= 0 || thickness <= 0) {
        return [];
    }
    const count = Math.min(4, Math.floor(windowCount));
    const dot = Math.min(thickness, Math.max(2, length / 11));
    const line = Math.min(thickness, Math.max(2, length / 20));
    const gap = Math.max(2, Math.ceil(length / 18));
    const result = [];
    function append(x, width, height, round, alpha) {
        result.push({x: x, width: width, height: height, round: round, alpha: alpha ?? 1});
    }
    if (style === 5) { // Solid
        append(0, length, line, false);
    } else if (style === 6) { // Ciliora: a bar followed by a square per extra window.
        const width = length - (count - 1) * (line + gap);
        append(0, width, line, false);
        for (let i = 1; i < count; ++i) {
            append(width + gap + (i - 1) * (line + gap), line, line, false);
        }
    } else if (style === 7) { // Metro: the final portion distinguishes grouped tasks.
        const tail = count > 1 ? length * (active ? 2 : 10) / 48 : 0;
        const divider = tail ? length / 48 : 0;
        append(0, length - tail - divider, line, false);
        if (tail) {
            append(length - tail - divider, divider, line, false, 0.3);
            append(length - tail, tail, line, false, 0.7);
        }
    } else if (style === 8) { // Four binary digits; zero is a dash, one is a dot.
        const value = Math.min(15, Math.floor(windowCount));
        const offset = (length - 4 * dot - 3 * gap) / 2;
        for (let i = 0; i < 4; ++i) {
            const bit = (value >> (3 - i)) & 1;
            append(offset + i * (dot + gap), dot, bit ? dot : Math.max(1, dot / 3), !!bit);
        }
    } else {
        const n = style === 9 ? 1 : count;
        const width = style === 4 ? (length - (n - 1) * gap) / n
            : style === 3 ? Math.max(2, length / 4 - gap) : dot;
        const height = style === 3 || style === 4 ? line : dot;
        const offset = (length - n * width - (n - 1) * gap) / 2;
        for (let i = 0; i < n; ++i) {
            append(offset + i * (width + gap), width, height, style === 1 || style === 9);
        }
    }
    return result;
}

// Arrange a single strip. The Applications button can remain at the outer edge
// while the tasks and places center independently, or join their centered group.
function dockSlots(length, tasksLength, extrasLength, appsLength, appsAtStart, edgeApps, centered) {
    const total = Math.max(0, length);
    const apps = bounded(appsLength, 0, total);
    const extras = bounded(extrasLength, 0, total - apps);
    const tasks = bounded(tasksLength, 0, total - apps - extras);
    const groupSpace = total - (edgeApps ? apps : 0);
    const groupLength = tasks + extras + (edgeApps ? 0 : apps);
    const start = (edgeApps && appsAtStart ? apps : 0)
        + (centered ? Math.max(0, (groupSpace - groupLength) / 2) : 0);
    const taskStart = start + (!edgeApps && appsAtStart ? apps : 0);
    return {taskStart: taskStart, taskLength: tasks, extrasStart: taskStart + tasks,
        extrasLength: extras, appsLength: apps,
        appsStart: edgeApps ? (appsAtStart ? 0 : total - apps)
            : (appsAtStart ? start : taskStart + tasks + extras)};
}

// Source transparency uses the entire output's edge band, including a five
// logical-pixel near-window margin, independently of the dock's short length.
function edgeRegion(screen, dock, edge) {
    const right = screen.x + screen.width;
    const bottom = screen.y + screen.height;
    if (edge === "left") return {x: screen.x, y: screen.y,
        width: Math.max(0, dock.x + dock.width + 5 - screen.x), height: screen.height};
    if (edge === "right") {
        const threshold = dock.x - 5;
        return {x: threshold, y: screen.y, width: Math.max(0, right - threshold), height: screen.height};
    }
    if (edge === "top") return {x: screen.x, y: screen.y,
        width: screen.width, height: Math.max(0, dock.y + dock.height + 5 - screen.y)};
    const threshold = dock.y - 5;
    return {x: screen.x, y: threshold, width: screen.width, height: Math.max(0, bottom - threshold)};
}
