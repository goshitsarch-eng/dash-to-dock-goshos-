// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QVariantList>
#include <xkbcommon/xkbcommon.h>

namespace GoshosDock {
/** Actual KWin shortcut combinations for Shift+Meta on unshifted digits 1..0.
 * A zero entry means that digit has no unshifted candidate in this layout.
 * Uses a scratch XKB state, never changes the compositor's live modifiers.
 */
QVariantList normalizedAlternateShortcuts(xkb_keymap *keymap, xkb_layout_index_t layout);
}
