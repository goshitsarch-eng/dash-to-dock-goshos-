// SPDX-License-Identifier: GPL-2.0-or-later
#include "shortcutnormalization.h"

#include <QChar>
#include <QKeyCombination>
#include <QtGui/private/qxkbcommon_p.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <array>
#include <memory>

namespace GoshosDock {
namespace {
struct Modifier {
    const char *name;
    Qt::KeyboardModifier qt;
};
constexpr std::array<Modifier, 4> modifiers{{
    {XKB_MOD_NAME_SHIFT, Qt::ShiftModifier}, {XKB_MOD_NAME_ALT, Qt::AltModifier},
    {XKB_MOD_NAME_CTRL, Qt::ControlModifier}, {XKB_MOD_NAME_LOGO, Qt::MetaModifier},
}};

Qt::Key qtKey(xkb_keysym_t symbol, xkb_state *state, xkb_keycode_t code, Qt::KeyboardModifiers mods)
{
    // Match KWin::Xkb::toQtKey, including its Latin-1 Qt conversion fix.
    auto key = Qt::Key(QXkbCommon::keysymToQtKey(symbol, mods, state, code));
    if (key > 0xff && symbol <= 0xff)
        key = Qt::Key(symbol);
    return key;
}
}

QVariantList normalizedAlternateShortcuts(xkb_keymap *keymap, xkb_layout_index_t layout)
{
    QVariantList result;
    for (int index = 0; index < 10; ++index)
        result.append(0);
    if (!keymap || layout >= xkb_keymap_num_layouts(keymap))
        return result;
    const xkb_mod_index_t shift = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    const xkb_mod_index_t meta = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
    if (shift == XKB_MOD_INVALID || meta == XKB_MOD_INVALID || shift >= 32 || meta >= 32)
        return result;
    std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> state(xkb_state_new(keymap), xkb_state_unref);
    if (!state)
        return result;
    xkb_state_update_mask(state.get(), (xkb_mod_mask_t(1) << shift) | (xkb_mod_mask_t(1) << meta),
                          0, 0, 0, 0, layout);

    for (int index = 0; index < 10; ++index) {
        const xkb_keysym_t digit = index == 9 ? XKB_KEY_0 : XKB_KEY_1 + index;
        for (xkb_keycode_t code = xkb_keymap_min_keycode(keymap); code <= xkb_keymap_max_keycode(keymap); ++code) {
            const xkb_keysym_t *baseSymbols = nullptr;
            const int count = xkb_keymap_key_get_syms_by_level(keymap, code, layout, 0, &baseSymbols);
            bool digitCandidate = false;
            for (int symbol = 0; symbol < count; ++symbol)
                digitCandidate |= baseSymbols[symbol] == digit;
            if (!digitCandidate)
                continue;
            const xkb_keysym_t symbol = xkb_state_key_get_one_sym(state.get(), code);
            if (symbol == XKB_KEY_NoSymbol)
                continue;

            Qt::KeyboardModifiers active;
            Qt::KeyboardModifiers consumed;
            for (const auto &modifier : modifiers) {
                const auto mod = xkb_keymap_mod_get_index(keymap, modifier.name);
                if (mod == XKB_MOD_INVALID)
                    continue;
                if (xkb_state_mod_index_is_active(state.get(), mod, XKB_STATE_MODS_EFFECTIVE) == 1)
                    active |= modifier.qt;
                if (xkb_state_mod_index_is_consumed2(state.get(), code, mod, XKB_CONSUMED_MODE_GTK) == 1)
                    consumed |= modifier.qt;
            }
            if (symbol >= XKB_KEY_KP_Space && symbol <= XKB_KEY_KP_Equal)
                active |= Qt::KeypadModifier;
            // KWin preserves explicit Shift for letters, but consumes it for
            // punctuation such as US !, GB £ and German §.
            if (active.testFlag(Qt::ShiftModifier) && consumed == Qt::ShiftModifier
                && QChar::isLetter(qtKey(symbol, state.get(), code, Qt::ControlModifier)))
                consumed = {};
            const Qt::KeyboardModifiers relevant = active & ~consumed;
            const Qt::Key key = qtKey(symbol, state.get(), code, relevant ? Qt::ControlModifier : Qt::KeyboardModifiers());
            if (key != Qt::Key_unknown)
                result[index] = QKeyCombination(relevant, key).toCombined();
            break;
        }
    }
    return result;
}
}
