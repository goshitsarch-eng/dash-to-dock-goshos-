// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-
// Added by Gosh OS contributors on 2026-08-24 for Goshos Dock magnification.

/**
 * macOS-like dock magnification.
 *
 * The pointer position along the dock is mapped onto a "flat" (un-magnified)
 * coordinate space, and every dock item is scaled by a raised-cosine falloff
 * around that position. The scale is applied as a transform on the item's
 * child (so the item container's own show/hide animation is untouched), while
 * the item container inflates its *main axis* preferred size by the same
 * amount so that neighbours spread apart the way they do on macOS.
 *
 * On the *cross axis* every item reserves a constant slot big enough for the
 * largest possible magnified icon. That keeps the dash allocation stable while
 * magnifying: the dash never resizes under the pointer, so nothing is
 * recomputed mid-animation. The dock background is shrunk back to the resting
 * icon size so that only the icons themselves grow into the reserved space.
 *
 * The reserved head room — the difference between that slot and the resting
 * size — is handed to `docking.js`, which allocates it *outside* the dock
 * actor, over the screen rather than over the dock. The magnified icons rise
 * above the dock strip like they do on macOS, while the struts, the
 * intellihide target box and the desktop-icons usable area keep matching the
 * strip the user actually sees.
 */

import {
    Clutter,
    St,
} from './dependencies/gi.js';

import {
    Main,
} from './dependencies/shell/ui.js';

import {
    Docking,
    Utils,
} from './imports.js';

const RESET_ANIMATION_TIME = 160;

export const MIN_MAGNIFICATION = 1.0;
export const MAX_MAGNIFICATION = 3.0;
export const MIN_RANGE = 1.0;
export const MAX_RANGE = 6.0;

const Labels = Object.freeze({
    MAGNIFICATION: Symbol('magnification'),
});

/**
 * Adjust a dash item container preferred size for magnification.
 *
 * Called from the `vfunc_get_preferred_width`/`vfunc_get_preferred_height`
 * overrides of every magnifiable dock item. It is a no-op unless the item has
 * been enrolled by a `DockMagnifier`.
 *
 * @param {Clutter.Actor} item the dash item container
 * @param {number} minSize the minimum size reported by the parent class
 * @param {number} natSize the natural size reported by the parent class
 * @param {boolean} isWidthAxis true when adjusting the width
 * @returns {number[]} the adjusted `[minSize, natSize]` pair
 */
export function adjustPreferredSize(item, minSize, natSize, isWidthAxis) {
    const state = item._dockMagnification;
    if (!state)
        return [minSize, natSize];

    const {shared} = state;
    const isMainAxis = shared.horizontal === isWidthAxis;

    // The parent class already multiplied by the container scale, which is
    // animated from 0 to 1 while an item is being added: only sample the
    // natural size once the item is fully shown, or the cached baseline would
    // collapse to zero.
    const containerScale = isWidthAxis ? item.scale_x : item.scale_y;

    if (isMainAxis) {
        const scale = magnifiedScale(item);
        return [minSize, Math.ceil(natSize * scale)];
    }

    if (containerScale >= 1 && natSize > shared.crossBase) {
        shared.crossBase = natSize;
        shared.crossSlot = Math.ceil(natSize * shared.factor);
    }

    const slot = shared.crossSlot;
    if (slot <= natSize)
        return [minSize, natSize];

    return [slot, slot];
}

/**
 * The live magnification scale of an item, read from the transform that is
 * actually applied, so that eased transitions keep the layout in sync with
 * what is drawn.
 *
 * @param {Clutter.Actor} item the dash item container
 * @returns {number} the current scale factor
 */
function magnifiedScale(item) {
    const {child} = item;
    if (!child)
        return 1;
    return Math.max(1, child.scale_x, child.scale_y);
}

export class DockMagnifier {
    constructor(dash) {
        this._dash = dash;
        this._enabled = false;
        this._items = [];
        this._shared = {
            horizontal: true,
            factor: MIN_MAGNIFICATION,
            crossBase: 0,
            crossSlot: 0,
        };
        this._range = 2;
        this._frozen = 0;
        this._savedBackground = null;
        // No parent object: DockMagnifier is a plain class with no 'destroy'
        // signal, and the dash destroys it explicitly from its own _onDestroy.
        this._signalsHandler = new Utils.GlobalSignalsHandler();

        const {settings} = Docking.DockManager;
        this._signalsHandler.add([
            settings,
            'changed::magnification-enabled',
            () => this._sync(),
        ], [
            settings,
            'changed::magnification-factor',
            () => this._sync(),
        ], [
            settings,
            'changed::magnification-range',
            () => this._sync(),
        ]);

        this._sync();
    }

    destroy() {
        this._disable();
        this._signalsHandler?.destroy();
        this._signalsHandler = null;
        this._dash = null;
    }

    get enabled() {
        return this._enabled;
    }

    /**
     * The room reserved on the cross axis for the magnified icons, on top of
     * the size the dock items rest at.
     *
     * @returns {number} the reserved head room, in pixels
     */
    get headRoom() {
        if (!this._enabled)
            return 0;

        const {crossBase, crossSlot} = this._shared;
        return Math.max(0, crossSlot - crossBase);
    }

    /**
     * Freeze magnification (and reset it) while something else owns the dock
     * geometry, e.g. a drag and drop operation.
     *
     * @param {boolean} frozen whether magnification must be suspended
     */
    setFrozen(frozen) {
        this._frozen = Math.max(0, this._frozen + (frozen ? 1 : -1));
        if (this._frozen)
            this.reset(false);
    }

    /**
     * Re-enroll any dock item that appeared since the last pass, and refresh
     * the reserved cross-axis slot. Cheap enough to be called from
     * `_redisplay()` and on icon size changes.
     */
    refresh() {
        if (!this._enabled)
            return;

        const items = this._dash.getMagnifiableItems();
        this._items.filter(item => !items.includes(item))
            .forEach(item => this._unenroll(item));

        this._items = items;
        this._items.forEach(item => this._enroll(item));
        this._remeasure(true);
    }

    reset(animate = true) {
        this._items.forEach(item => {
            const {child} = item;
            if (!child)
                return;

            child.remove_transition('scale-x');
            child.remove_transition('scale-y');

            if (animate && child.scale_x !== 1) {
                child.ease({
                    scale_x: 1,
                    scale_y: 1,
                    duration: RESET_ANIMATION_TIME,
                    mode: Clutter.AnimationMode.EASE_OUT_QUAD,
                });
            } else {
                child.set_scale(1, 1);
            }
            item.queue_relayout();
        });
    }

    _sync() {
        const {settings} = Docking.DockManager;
        const shouldEnable = !!settings.magnificationEnabled;

        this._shared.factor = Utils.clamp(settings.magnificationFactor,
            MIN_MAGNIFICATION, MAX_MAGNIFICATION);
        this._range = Utils.clamp(settings.magnificationRange, MIN_RANGE, MAX_RANGE);

        if (shouldEnable === this._enabled) {
            if (this._enabled)
                this._remeasure(true);
            return;
        }

        if (shouldEnable)
            this._enable();
        else
            this._disable();
    }

    _enable() {
        const dash = this._dash;
        const position = Utils.getPosition();

        this._enabled = true;
        this._shared.horizontal = position === St.Side.TOP || position === St.Side.BOTTOM;
        this._shared.crossBase = 0;
        this._shared.crossSlot = 0;
        this._position = position;

        this._wasReactive = dash._dashContainer.reactive;
        dash._dashContainer.reactive = true;

        this._signalsHandler.addWithLabel(Labels.MAGNIFICATION, [
            dash._dashContainer,
            'captured-event',
            (_actor, event) => this._onCapturedEvent(event),
        ], [
            dash._dashContainer,
            'leave-event',
            () => {
                this.reset(true);
                return Clutter.EVENT_PROPAGATE;
            },
        ], [
            dash._dashContainer,
            'notify::size',
            () => this._updateBackground(),
        ], [
            dash,
            'icon-size-changed',
            () => this._remeasure(true),
        ], [
            Main.overview,
            'hiding',
            () => this.reset(false),
        ]);

        this.refresh();
    }

    _disable() {
        this._enabled = false;
        this.reset(false);
        this._items.forEach(item => this._unenroll(item));
        this._items = [];
        this._shared.crossBase = 0;
        this._shared.crossSlot = 0;

        this._signalsHandler?.removeWithLabel(Labels.MAGNIFICATION);

        const dash = this._dash;
        if (dash) {
            if (this._wasReactive !== undefined)
                dash._dashContainer.reactive = this._wasReactive;
            this._restoreBackground();
            dash._dashContainer.queue_relayout();
        }
        this._wasReactive = undefined;
    }

    _enroll(item) {
        if (item._dockMagnification) {
            item._dockMagnification.shared = this._shared;
            return;
        }

        const {child} = item;
        if (!child)
            return;

        item._dockMagnification = {
            shared: this._shared,
            destroyId: item.connect('destroy', () => this._onItemDestroyed(item)),
        };

        const {horizontal} = this._shared;
        const position = this._position;

        // Grow away from the screen edge, like the macOS dock does.
        let pivotX = 0.5;
        let pivotY = 0.5;
        if (position === St.Side.BOTTOM)
            pivotY = 1;
        else if (position === St.Side.TOP)
            pivotY = 0;
        else if (position === St.Side.RIGHT)
            pivotX = 1;
        else
            pivotX = 0;

        child.set_pivot_point(pivotX, pivotY);

        // Keep the resting icon glued to the screen edge inside the taller
        // slot, instead of being centred in the reserved head room.
        item._dockMagnification.childAlign = {
            xExpand: child.x_expand,
            yExpand: child.y_expand,
            xAlign: child.x_align,
            yAlign: child.y_align,
        };

        if (horizontal) {
            child.y_expand = false;
            child.y_align = position === St.Side.TOP
                ? Clutter.ActorAlign.START : Clutter.ActorAlign.END;
        } else {
            child.x_expand = false;
            child.x_align = position === St.Side.RIGHT
                ? Clutter.ActorAlign.END : Clutter.ActorAlign.START;
        }

        // A scale change must re-run size negotiation on the container so the
        // neighbours make room, including for every frame of an eased reset.
        child.connectObject('notify::scale-x', () => item.queue_relayout(), item);
        child.connectObject('notify::scale-y', () => item.queue_relayout(), item);
    }

    _onItemDestroyed(item) {
        const index = this._items.indexOf(item);
        if (index !== -1)
            this._items.splice(index, 1);

        delete item._dockMagnification;
    }

    _unenroll(item) {
        const state = item._dockMagnification;
        if (!state)
            return;

        delete item._dockMagnification;

        if (state.destroyId)
            item.disconnect(state.destroyId);

        const {child} = item;
        if (!child)
            return;

        child.remove_transition('scale-x');
        child.remove_transition('scale-y');
        child.set_scale(1, 1);
        child.set_pivot_point(0, 0);
        child.disconnectObject(item);

        const align = state.childAlign;
        if (align) {
            child.x_expand = align.xExpand;
            child.y_expand = align.yExpand;
            child.x_align = align.xAlign;
            child.y_align = align.yAlign;
        }
        item.queue_relayout();
    }

    /**
     * Recompute the reserved cross-axis slot from scratch. Sizes are sampled
     * synchronously so the dock never renders a frame with a stale slot.
     *
     * @param {boolean} full whether the cached baseline must be discarded
     */
    _remeasure(full = false) {
        if (!this._enabled)
            return;

        if (full) {
            this._shared.crossBase = 0;
            this._shared.crossSlot = 0;
        }

        const {horizontal} = this._shared;

        // First pass: drop the cached size requests and sample every item so
        // that the shared slot converges on the tallest one.
        this._items.forEach(item => {
            item.queue_relayout();
            if (horizontal)
                item.get_preferred_height(-1);
            else
                item.get_preferred_width(-1);
        });

        // Second pass: the slot is final, make every item request it.
        this._items.forEach(item => item.queue_relayout());

        this._updateBackground();
    }

    /**
     * Shrink the dock background back to the resting icon size so that the
     * reserved head room stays transparent, exactly like the macOS dock where
     * magnified icons rise above the dock strip.
     */
    _updateBackground() {
        const dash = this._dash;
        const background = dash?._background;
        if (!background || !this._enabled)
            return;

        const {horizontal} = this._shared;
        const {headRoom} = this;
        if (!headRoom)
            return;

        if (!this._savedBackground) {
            this._savedBackground = {
                xExpand: background.x_expand,
                yExpand: background.y_expand,
                xAlign: background.x_align,
                yAlign: background.y_align,
            };
        }

        const containerSize = horizontal
            ? dash._dashContainer.height : dash._dashContainer.width;
        const size = Math.max(0, Math.round(containerSize - headRoom));
        if (!size)
            return;

        if (horizontal) {
            background.y_expand = false;
            background.y_align = this._position === St.Side.TOP
                ? Clutter.ActorAlign.START : Clutter.ActorAlign.END;
            background.height = size;
        } else {
            background.x_expand = false;
            background.x_align = this._position === St.Side.RIGHT
                ? Clutter.ActorAlign.END : Clutter.ActorAlign.START;
            background.width = size;
        }
    }

    _restoreBackground() {
        const background = this._dash?._background;
        const saved = this._savedBackground;
        if (!background || !saved)
            return;

        background.x_expand = saved.xExpand;
        background.y_expand = saved.yExpand;
        background.x_align = saved.xAlign;
        background.y_align = saved.yAlign;
        background.set_size(-1, -1);
        this._savedBackground = null;
    }

    _onCapturedEvent(event) {
        if (!this._enabled || this._frozen)
            return Clutter.EVENT_PROPAGATE;

        switch (event.type()) {
        case Clutter.EventType.MOTION:
        case Clutter.EventType.ENTER:
            this._updateForEvent(event);
            break;
        case Clutter.EventType.LEAVE: {
            const related = event.get_related?.();
            if (!related || !this._dash._dashContainer.contains(related))
                this.reset(true);
            break;
        }
        }

        return Clutter.EVENT_PROPAGATE;
    }

    _updateForEvent(event) {
        const [stageX, stageY] = event.get_coords();
        // Clutter only fills in the event source for crossing events, so the
        // actor under the pointer has to come from the stage.
        const target = global.stage.get_event_actor(event);
        this._update(stageX, stageY, this._itemForActor(target));
    }

    _itemForActor(actor) {
        let candidate = actor;
        while (candidate) {
            if (candidate._dockMagnification && this._items.includes(candidate))
                return candidate;
            candidate = candidate.get_parent();
        }
        return null;
    }

    /**
     * The heart of the effect: map the pointer onto the flat layout and apply
     * a raised-cosine falloff around it.
     *
     * @param {number} stageX pointer x in stage coordinates
     * @param {number} stageY pointer y in stage coordinates
     * @param {Clutter.Actor} sourceItem the hovered item, when known
     */
    _update(stageX, stageY, sourceItem) {
        if (!this._enabled || this._frozen)
            return;

        if (!this._items.length || this._items.some(i => !i._dockMagnification || !i.get_stage()))
            this.refresh();

        const items = this._items;
        if (!items.length)
            return;

        const {factor} = this._shared;
        if (factor <= MIN_MAGNIFICATION) {
            this.reset(false);
            return;
        }

        // Flat (un-magnified) layout: every item keeps its resting extent.
        const spans = [];
        let offset = 0;
        let totalSize = 0;
        items.forEach(item => {
            const size = this._restingSize(item);
            spans.push({item, start: offset, size, center: offset + size / 2});
            offset += size;
            totalSize += size;
        });

        const cursor = this._flatPointer(spans, stageX, stageY, sourceItem);
        if (cursor === null) {
            this.reset(true);
            return;
        }

        const averageSize = totalSize / items.length || 1;
        const radius = Math.max(1, this._range * averageSize);

        spans.forEach(span => {
            const distance = Math.abs(cursor - span.center) / radius;
            const falloff = distance >= 1
                ? 0 : 0.5 * (1 + Math.cos(Math.PI * distance));
            this._setItemScale(span.item, 1 + (factor - 1) * falloff);
        });
    }

    /**
     * Project the pointer into the flat layout. When the pointer is over a
     * known item we use the fraction of the way across that item, which stays
     * stable no matter how much the layout has already been stretched.
     *
     * @param {object[]} spans the flat layout
     * @param {number} stageX pointer x in stage coordinates
     * @param {number} stageY pointer y in stage coordinates
     * @param {Clutter.Actor} sourceItem the hovered item, when known
     * @returns {?number} the flat coordinate, or null when off the dock
     */
    _flatPointer(spans, stageX, stageY, sourceItem) {
        const {horizontal} = this._shared;
        const pointer = horizontal ? stageX : stageY;

        const index = sourceItem
            ? spans.findIndex(span => span.item === sourceItem) : -1;

        if (index >= 0) {
            const span = spans[index];
            const {item} = span;
            const [itemX, itemY] = item.get_transformed_position();
            const origin = horizontal ? itemX : itemY;
            const extent = horizontal ? item.width : item.height;
            const fraction = extent > 0
                ? Utils.clampDouble((pointer - origin) / extent) : 0.5;
            return span.start + fraction * span.size;
        }

        // Not over an item (padding, separator, scrollbar): fall back to the
        // closest item by its current on screen position.
        let best = null;
        let bestDistance = Infinity;
        spans.forEach(span => {
            const [itemX, itemY] = span.item.get_transformed_position();
            const origin = horizontal ? itemX : itemY;
            const extent = horizontal ? span.item.width : span.item.height;
            const center = origin + extent / 2;
            const distance = Math.abs(pointer - center);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = span;
            }
        });

        if (!best)
            return null;

        const [bestX, bestY] = best.item.get_transformed_position();
        const origin = horizontal ? bestX : bestY;
        const extent = horizontal ? best.item.width : best.item.height;
        const fraction = extent > 0
            ? Utils.clampDouble((pointer - origin) / extent) : 0.5;
        return best.start + fraction * best.size;
    }

    _restingSize(item) {
        const {horizontal} = this._shared;
        const scale = magnifiedScale(item);
        const size = horizontal ? item.width : item.height;
        if (size > 0 && scale > 0)
            return size / scale;

        const [, natural] = horizontal
            ? item.get_preferred_width(-1) : item.get_preferred_height(-1);
        return natural / (scale || 1);
    }

    _setItemScale(item, scale) {
        const {child} = item;
        if (!child)
            return;

        if (Math.abs(child.scale_x - scale) < 0.002)
            return;

        child.remove_transition('scale-x');
        child.remove_transition('scale-y');
        child.set_scale(scale, scale);
        item.queue_relayout();
    }
}
