// SPDX-License-Identifier: GPL-2.0-or-later
#include "shortcutnormalization.h"

#include <QKeyCombination>
#include <QtTest>
#include <memory>

class ShortcutNormalizationTest : public QObject
{
    Q_OBJECT
    using Context = std::unique_ptr<xkb_context, decltype(&xkb_context_unref)>;
    using Keymap = std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)>;

    static Keymap keymap(const char *layouts)
    {
        Context context(xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref);
        xkb_rule_names names{};
        names.rules = "evdev";
        names.model = "pc105";
        names.layout = layouts;
        return {xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS), xkb_keymap_unref};
    }

    static QVariantList expected(const QString &symbols)
    {
        QVariantList result;
        for (const QChar symbol : symbols)
            result.append(QKeyCombination(Qt::MetaModifier, Qt::Key(symbol.unicode())).toCombined());
        return result;
    }

private Q_SLOTS:
    void shiftedDigits_data()
    {
        QTest::addColumn<QByteArray>("layout");
        QTest::addColumn<QString>("symbols");
        QTest::newRow("US") << QByteArray("us") << QStringLiteral("!@#$%^&*()");
        QTest::newRow("GB") << QByteArray("gb") << QString::fromUtf8("!\"£$%^&*()");
        QTest::newRow("DE") << QByteArray("de") << QString::fromUtf8("!\"§$%&/()=");
    }
    void shiftedDigits()
    {
        QFETCH(QByteArray, layout);
        QFETCH(QString, symbols);
        auto map = keymap(layout.constData());
        QVERIFY(map);
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), 0), expected(symbols));
    }
    void activeGroupChangesSymbols()
    {
        auto map = keymap("us,de");
        QVERIFY(map);
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), 0), expected(QStringLiteral("!@#$%^&*()")));
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), 1), expected(QString::fromUtf8("!\"§$%&/()=")));
    }
    void frenchHasNoUnshiftedDigitDefaults()
    {
        auto map = keymap("fr");
        QVERIFY(map);
        // On AZERTY, Shift is already needed for the digits. Inventing US
        // punctuation would bind unrelated keys; preserve the user's bindings.
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), 0), QVariantList(10, 0));
    }
    void separateLiveStateIsUnchanged()
    {
        auto map = keymap("us,de");
        QVERIFY(map);
        std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> live(xkb_state_new(map.get()), xkb_state_unref);
        QVERIFY(live);
        const auto caps = xkb_keymap_mod_get_index(map.get(), XKB_MOD_NAME_CAPS);
        xkb_state_update_mask(live.get(), 0, 0, xkb_mod_mask_t(1) << caps, 0, 0, 1);
        const auto mods = xkb_state_serialize_mods(live.get(), XKB_STATE_MODS_EFFECTIVE);
        const auto layout = xkb_state_serialize_layout(live.get(), XKB_STATE_LAYOUT_EFFECTIVE);
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), layout), expected(QString::fromUtf8("!\"§$%&/()=")));
        QCOMPARE(xkb_state_serialize_mods(live.get(), XKB_STATE_MODS_EFFECTIVE), mods);
        QCOMPARE(xkb_state_serialize_layout(live.get(), XKB_STATE_LAYOUT_EFFECTIVE), layout);
    }
    void invalidMapAndGroup()
    {
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(nullptr, 0), QVariantList(10, 0));
        auto map = keymap("us");
        QVERIFY(map);
        QCOMPARE(GoshosDock::normalizedAlternateShortcuts(map.get(), 1), QVariantList(10, 0));
    }
};

QTEST_GUILESS_MAIN(ShortcutNormalizationTest)
#include "tst_shortcutnormalization.moc"
