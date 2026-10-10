/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "globalshortcuts.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QPointer>
#include <QSignalSpy>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class PointerFixture : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gosh.GoshosDock.KWin1")
public:
    QRect output;
    bool delayed = false;
    bool pending = false;
    QDBusMessage request;
    QVariantList alternateKeys;

    QVariantMap geometry() const
    {
        return {{QStringLiteral("x"), output.x()}, {QStringLiteral("y"), output.y()},
            {QStringLiteral("width"), output.width()}, {QStringLiteral("height"), output.height()}};
    }

    void complete()
    {
        QDBusConnection::sessionBus().send(request.createReply(QVariantList{geometry()}));
        pending = false;
    }

public Q_SLOTS:
    QVariantList NormalizedAlternateShortcuts() const
    {
        return alternateKeys;
    }

    QVariantMap PointerOutputGeometry()
    {
        if (delayed) {
            setDelayedReply(true);
            request = message();
            pending = true;
            return {};
        }
        return geometry();
    }

Q_SIGNALS:
    void AlternateShortcutsChanged();
};

class GlobalShortcutsTest : public QObject
{
    Q_OBJECT
    PointerFixture pointerFixture;

    static QObject *registry()
    {
        return QCoreApplication::instance()->findChild<QObject *>(QStringLiteral("goshosdock-shared-shortcuts"));
    }

    static QAction *action(const QString &name)
    {
        return registry()->findChild<QAction *>(name);
    }

    static QRect pointerScreen()
    {
        return QRect(QCursor::pos() - QPoint(10, 10), QSize(20, 20));
    }

    static void nativePress(QAction *action)
    {
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(action, true);
        action->trigger();
        QVERIFY(QMetaObject::invokeMethod(registry(), "nativeShortcutPressed",
            Q_ARG(QString, action->property("componentName").toString()),
            Q_ARG(QString, action->objectName()), Q_ARG(qlonglong, 0)));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(QDBusConnection::sessionBus().registerService(QStringLiteral("org.gosh.GoshosDock.KWin")));
        QVERIFY(QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/gosh/GoshosDock"), &pointerFixture,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    }

    void init()
    {
        pointerFixture.output = {};
        pointerFixture.delayed = false;
        pointerFixture.pending = false;
        pointerFixture.alternateKeys.clear();
    }

    void normalizedAlternateDefaultsTrackCompositorLayout()
    {
        GlobalShortcuts dock;
        auto *first = action(QStringLiteral("goshosdock-alternate-1"));
        const QString symbols = QStringLiteral("!@#$%^&*()");
        for (const QChar symbol : symbols) pointerFixture.alternateKeys.append(int(Qt::MetaModifier) | symbol.unicode());
        Q_EMIT pointerFixture.AlternateShortcutsChanged();
        const QKeySequence exclamation(QKeyCombination(Qt::MetaModifier, Qt::Key_Exclam));
        QTRY_COMPARE(KGlobalAccel::self()->defaultShortcut(first), QList<QKeySequence>{exclamation});
        // A layout change refreshes the defaults asynchronously.
        const QKeySequence section(QKeyCombination(Qt::MetaModifier, Qt::Key_section));
        pointerFixture.alternateKeys[0] = section[0].toCombined();
        Q_EMIT pointerFixture.AlternateShortcutsChanged();
        QTRY_COMPARE(KGlobalAccel::self()->defaultShortcut(first), QList<QKeySequence>{section});
    }

    void registrationsAreSharedWithAllTenDefaultNumbers()
    {
        GlobalShortcuts first;
        const auto actions = registry()->findChildren<QAction *>();
        QCOMPARE(actions.size(), 31);
        GlobalShortcuts second;
        QCOMPARE(registry()->findChildren<QAction *>(), actions);

        for (int index = 0; index < 10; ++index) {
            const Qt::Key key = index == 9 ? Qt::Key_0 : static_cast<Qt::Key>(Qt::Key_1 + index);
            QCOMPARE(KGlobalAccel::self()->defaultShortcut(action(QStringLiteral("goshosdock-activate-%1").arg(index + 1))),
                     QList<QKeySequence>{QKeySequence(QKeyCombination(Qt::MetaModifier, key))});
        }
        const auto *overlay = action(QStringLiteral("goshosdock-show-overlay"));
        QVERIFY(overlay);
        QCOMPARE(KGlobalAccel::self()->defaultShortcut(overlay), QList<QKeySequence>{QKeySequence(QKeyCombination(Qt::MetaModifier, Qt::Key_Q))});
    }

    void launchGoesOnlyToPointerScreen()
    {
        GlobalShortcuts otherScreen;
        otherScreen.setScreenGeometry(QRect(QCursor::pos() + QPoint(10000, 10000), QSize(100, 100)));
        GlobalShortcuts currentScreen;
        currentScreen.setScreenGeometry(pointerScreen());
        QSignalSpy other(&otherScreen, &GlobalShortcuts::taskRequested);
        QSignalSpy current(&currentScreen, &GlobalShortcuts::taskRequested);

        action(QStringLiteral("goshosdock-launch-3"))->trigger();
        QCOMPARE(other.size(), 0);
        QTRY_COMPARE(current.size(), 1);
        QCOMPARE(current.first(), (QList<QVariant>{2, true, false}));
    }

    void compositorOutputOverridesStaleClientPointer()
    {
        GlobalShortcuts staleClientOutput;
        staleClientOutput.setScreenGeometry(pointerScreen());
        GlobalShortcuts actualOutput;
        pointerFixture.output = QRect(QCursor::pos() + QPoint(10000, 10000), QSize(100, 100));
        actualOutput.setScreenGeometry(pointerFixture.output);
        QSignalSpy stale(&staleClientOutput, &GlobalShortcuts::taskRequested);
        QSignalSpy actual(&actualOutput, &GlobalShortcuts::taskRequested);
        action(QStringLiteral("goshosdock-launch-1"))->trigger();
        QTRY_COMPARE(actual.size(), 1);
        QCOMPARE(stale.size(), 0);
    }

    void soleSecondaryDockWorksWithPointerOnPrimary()
    {
        GlobalShortcuts onlyDock;
        onlyDock.setScreenGeometry(QRect(QCursor::pos() + QPoint(10000, 10000), QSize(100, 100)));
        pointerFixture.output = QGuiApplication::primaryScreen()->geometry();
        QSignalSpy requested(&onlyDock, &GlobalShortcuts::taskRequested);
        QSignalSpy overlay(&onlyDock, &GlobalShortcuts::overlayRequested);
        action(QStringLiteral("goshosdock-activate-10"))->trigger();
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toInt(), 9);
        QTRY_COMPARE(overlay.size(), 1);
    }

    void replicatedDocksUsePrimaryAndBroadcastEveryOverlay()
    {
        GlobalShortcuts primary;
        primary.setScreenGeometry(QGuiApplication::primaryScreen()->geometry());
        primary.setAllOutputs(true);
        GlobalShortcuts replica;
        replica.setScreenGeometry(QRect(QCursor::pos() + QPoint(10000, 10000), QSize(100, 100)));
        replica.setAllOutputs(true);
        pointerFixture.output = replica.screenGeometry();
        QSignalSpy primaryTask(&primary, &GlobalShortcuts::taskRequested);
        QSignalSpy replicaTask(&replica, &GlobalShortcuts::taskRequested);
        QSignalSpy primaryOverlay(&primary, &GlobalShortcuts::overlayRequested);
        QSignalSpy replicaOverlay(&replica, &GlobalShortcuts::overlayRequested);
        action(QStringLiteral("goshosdock-activate-10"))->trigger();
        QTRY_COMPARE(primaryTask.size(), 1);
        QCOMPARE(replicaTask.size(), 0);
        QTRY_COMPARE(primaryOverlay.size(), 1);
        QTRY_COMPARE(replicaOverlay.size(), 1);
        action(QStringLiteral("goshosdock-show-overlay"))->trigger();
        QTRY_COMPARE(primaryOverlay.size(), 2);
        QTRY_COMPARE(replicaOverlay.size(), 2);
    }

    void pendingReplyIsDiscardedAfterDisableOrPanelRemoval()
    {
        pointerFixture.delayed = true;
        auto first = std::make_unique<GlobalShortcuts>();
        first->setScreenGeometry(pointerScreen());
        GlobalShortcuts second;
        second.setScreenGeometry(pointerScreen());
        QSignalSpy firstRequested(first.get(), &GlobalShortcuts::taskRequested);
        QSignalSpy secondRequested(&second, &GlobalShortcuts::taskRequested);
        action(QStringLiteral("goshosdock-launch-1"))->trigger();
        QTRY_VERIFY(pointerFixture.pending);
        first->setEnabled(false);
        pointerFixture.complete();
        QTRY_VERIFY(registry()->findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QCOMPARE(firstRequested.size(), 0);
        QCOMPARE(secondRequested.size(), 0);

        first->setEnabled(true);
        action(QStringLiteral("goshosdock-launch-1"))->trigger();
        QTRY_VERIFY(pointerFixture.pending);
        first.reset();
        pointerFixture.complete();
        QTRY_VERIFY(registry()->findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QCOMPARE(secondRequested.size(), 0);
    }

    void customActivationUsesNormalClickAction()
    {
        GlobalShortcuts receiver;
        receiver.setScreenGeometry(pointerScreen());
        QSignalSpy requested(&receiver, &GlobalShortcuts::taskRequested);
        action(QStringLiteral("goshosdock-activate-2"))->trigger();
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first(), (QList<QVariant>{1, false, false}));
    }

    void alternateActionUsesZeroForTenthKey()
    {
        GlobalShortcuts receiver;
        receiver.setScreenGeometry(pointerScreen());
        QSignalSpy requested(&receiver, &GlobalShortcuts::taskRequested);
        action(QStringLiteral("goshosdock-alternate-10"))->trigger();
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first(), (QList<QVariant>{9, false, true}));
        QCOMPARE(KGlobalAccel::self()->defaultShortcut(action(QStringLiteral("goshosdock-alternate-10"))),
                 QList<QKeySequence>{QKeySequence(QKeyCombination(Qt::ShiftModifier | Qt::MetaModifier, Qt::Key_0))});
    }

    void duplicateScreensReceiveOnlyOnce()
    {
        GlobalShortcuts first;
        GlobalShortcuts second;
        first.setScreenGeometry(pointerScreen());
        second.setScreenGeometry(pointerScreen());
        QSignalSpy firstOverlay(&first, &GlobalShortcuts::overlayRequested);
        QSignalSpy secondOverlay(&second, &GlobalShortcuts::overlayRequested);

        action(QStringLiteral("goshosdock-show-overlay"))->trigger();
        QTRY_COMPARE(firstOverlay.size(), 1);
        QCOMPARE(secondOverlay.size(), 0);

        first.setEnabled(false);
        action(QStringLiteral("goshosdock-show-overlay"))->trigger();
        QTRY_COMPARE(firstOverlay.size(), 1);
        QTRY_COMPARE(secondOverlay.size(), 1);
    }

    void nativeTaskShortcutsRouteOnceAndPreserveBindings()
    {
        GlobalShortcuts receiver;
        receiver.setScreenGeometry(pointerScreen());
        QSignalSpy requested(&receiver, &GlobalShortcuts::taskRequested);
        QAction native;
        native.setObjectName(QStringLiteral("activate task manager entry 2"));
        native.setProperty("componentName", QStringLiteral("plasmashell"));
        const QList<QKeySequence> keys{QKeySequence(Qt::META | Qt::ALT | Qt::Key_F8)};
        KGlobalAccel::self()->setShortcut(&native, keys, KGlobalAccel::NoAutoloading);
        const auto saved = KGlobalAccel::self()->shortcut(&native);
        QSignalSpy nativeTriggered(&native, &QAction::triggered);

        nativePress(&native);
        QVERIFY(native.signalsBlocked());
        QCOMPARE(nativeTriggered.size(), 0);
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first(), (QList<QVariant>{1, false, false}));
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&native, true);
        native.trigger();
        QTRY_COMPARE(requested.size(), 1);

        // Component.invokeShortcut emits another Pressed without Released.
        nativePress(&native);
        QTRY_COMPARE(requested.size(), 2);

        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&native, false);
        QVERIFY(!native.signalsBlocked());
        native.trigger();
        QCOMPARE(nativeTriggered.size(), 1);
        QCOMPARE(KGlobalAccel::self()->shortcut(&native), saved);
        KGlobalAccel::self()->removeAllShortcuts(&native);
    }

    void customOrDisabledBaseBindingDoesNotUseNativeBridge()
    {
        GlobalShortcuts receiver;
        receiver.setScreenGeometry(pointerScreen());
        QAction native;
        native.setObjectName(QStringLiteral("activate task manager entry 4"));
        native.setProperty("componentName", QStringLiteral("plasmashell"));
        QSignalSpy requested(&receiver, &GlobalShortcuts::taskRequested);
        QSignalSpy nativeTriggered(&native, &QAction::triggered);
        auto *activate = action(QStringLiteral("goshosdock-activate-4"));
        const QKeySequence custom(Qt::META | Qt::ALT | Qt::Key_F9);
        KGlobalAccel::self()->setShortcut(activate, {custom}, KGlobalAccel::NoAutoloading);
        Q_EMIT KGlobalAccel::self()->globalShortcutChanged(activate, custom);
        QVERIFY(!receiver.nativeActivationEnabled(3));
        nativePress(&native);
        QCOMPARE(requested.size(), 0);
        QCOMPARE(nativeTriggered.size(), 1);
        activate->trigger();
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toInt(), 3);

        KGlobalAccel::self()->setShortcut(activate, {}, KGlobalAccel::NoAutoloading);
        Q_EMIT KGlobalAccel::self()->globalShortcutChanged(activate, {});
        QVERIFY(!receiver.nativeActivationEnabled(3));
        nativePress(&native);
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(nativeTriggered.size(), 2);
        const QKeySequence initial(Qt::META | Qt::Key_4);
        KGlobalAccel::self()->setShortcut(activate, {initial}, KGlobalAccel::NoAutoloading);
        Q_EMIT KGlobalAccel::self()->globalShortcutChanged(activate, initial);
        QVERIFY(receiver.nativeActivationEnabled(3));
    }

    void unrelatedActionsAndExistingSignalBlockRemainUntouched()
    {
        GlobalShortcuts receiver;
        receiver.setScreenGeometry(pointerScreen());
        QSignalSpy requested(&receiver, &GlobalShortcuts::taskRequested);
        QAction unrelated;
        unrelated.setObjectName(QStringLiteral("activate task manager entry 1"));
        unrelated.setProperty("componentName", QStringLiteral("another-application"));
        QSignalSpy nativeTriggered(&unrelated, &QAction::triggered);
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&unrelated, true);
        unrelated.trigger();
        QCOMPARE(nativeTriggered.size(), 1);
        QCOMPARE(requested.size(), 0);
        unrelated.setProperty("componentName", QStringLiteral("plasmashell"));
        unrelated.blockSignals(true);
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&unrelated, true);
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&unrelated, false);
        QVERIFY(unrelated.signalsBlocked());
        QCOMPARE(requested.size(), 0);
    }

    void storedCustomAndDisabledMappingsSurviveRecreation()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("kglobalshortcutsrc"), KConfig::NoGlobals);
        KConfigGroup group(config, QStringLiteral("org.gosh.goshosdock"));
        group.writeEntry(QStringLiteral("goshosdock-activate-9"), QStringList{QStringLiteral("Meta+Alt+F9"), QStringLiteral("Meta+9"), QStringLiteral("Ninth")});
        group.writeEntry(QStringLiteral("goshosdock-activate-10"), QStringList{QStringLiteral("none"), QStringLiteral("Meta+0"), QStringLiteral("Tenth")});
        group.sync();
        {
            GlobalShortcuts receiver;
            QVERIFY(!receiver.nativeActivationEnabled(8));
            QVERIFY(!receiver.nativeActivationEnabled(9));
        }
        {
            GlobalShortcuts receiver;
            QVERIFY(!receiver.nativeActivationEnabled(8));
            QVERIFY(!receiver.nativeActivationEnabled(9));
        }
        group.deleteEntry(QStringLiteral("goshosdock-activate-9"));
        group.deleteEntry(QStringLiteral("goshosdock-activate-10"));
        group.sync();
    }

    void savedNativeCollisionSurvivesStartupBeforeNativeRegistration()
    {
        // A fresh Plasma process can construct the dock before registering
        // its own task actions. The daemon therefore has no native owner yet,
        // while the previous process saved an empty own key for that collision.
        const auto config = KSharedConfig::openConfig(QStringLiteral("kglobalshortcutsrc"), KConfig::NoGlobals);
        KConfigGroup group(config, QStringLiteral("org.gosh.goshosdock"));
        KConfigGroup state(KSharedConfig::openConfig(QStringLiteral("goshosdock-shortcutsrc")), QStringLiteral("NativeDefaults"));
        group.writeEntry(QStringLiteral("goshosdock-activate-1"), QStringList{QStringLiteral("none"), QStringLiteral("Meta+1"), QStringLiteral("First")});
        group.sync();
        state.writeEntry(QStringLiteral("1"), true);
        state.sync();
        for (int restart = 0; restart < 2; ++restart) {
            GlobalShortcuts receiver;
            QVERIFY(receiver.nativeActivationEnabled(0));
            QCoreApplication::processEvents();
            QVERIFY(state.readEntry(QStringLiteral("1"), false));
        }
        {
            GlobalShortcuts receiver;
            // A later, explicit user disable must still win over that marker.
            Q_EMIT KGlobalAccel::self()->globalShortcutChanged(action(QStringLiteral("goshosdock-activate-1")), QKeySequence());
            QVERIFY(!receiver.nativeActivationEnabled(0));
            QVERIFY(!state.readEntry(QStringLiteral("1"), true));
        }
        {
            GlobalShortcuts receiver;
            QVERIFY(!receiver.nativeActivationEnabled(0));
        }
        group.deleteEntry(QStringLiteral("goshosdock-activate-1"));
        group.sync();
        state.deleteEntry(QStringLiteral("1"));
        state.sync();
    }

    void disablingAndDestroyingDockRestoreHeldNativeAction()
    {
        QAction native;
        native.setObjectName(QStringLiteral("activate task manager entry 1"));
        native.setProperty("componentName", QStringLiteral("plasmashell"));
        auto receiver = std::make_unique<GlobalShortcuts>();
        receiver->setScreenGeometry(pointerScreen());
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&native, true);
        QVERIFY(native.signalsBlocked());
        receiver->setEnabled(false);
        QVERIFY(!native.signalsBlocked());
        receiver->setEnabled(true);
        Q_EMIT KGlobalAccel::self()->globalShortcutActiveChanged(&native, true);
        QVERIFY(native.signalsBlocked());
        receiver.reset();
        QVERIFY(!native.signalsBlocked());
    }

    void allDisabledReleasesAndReenablingRestoresRegistrations()
    {
        GlobalShortcuts first;
        GlobalShortcuts second;
        QPointer<QAction> previous = action(QStringLiteral("goshosdock-launch-1"));
        first.setEnabled(false);
        QVERIFY(previous);
        second.setEnabled(false);
        QVERIFY(previous.isNull());
        QVERIFY(registry()->findChildren<QAction *>().isEmpty());

        second.setEnabled(true);
        QCOMPARE(registry()->findChildren<QAction *>().size(), 31);
    }

    void destroyingLastProxyRemovesSharedRegistry()
    {
        auto first = std::make_unique<GlobalShortcuts>();
        auto second = std::make_unique<GlobalShortcuts>();
        QPointer<QObject> shared = registry();
        QPointer<QAction> registered = action(QStringLiteral("goshosdock-launch-1"));
        first.reset();
        QVERIFY(shared);
        QVERIFY(registered);
        second.reset();
        QVERIFY(shared.isNull());
        QVERIFY(registered.isNull());
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir profile;
    if (!profile.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", profile.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", profile.filePath(QStringLiteral("cache")).toUtf8());
    qputenv("XDG_DATA_HOME", profile.filePath(QStringLiteral("data")).toUtf8());
    QDir().mkpath(profile.filePath(QStringLiteral("config")));
    QDir().mkpath(profile.filePath(QStringLiteral("cache")));
    QDir().mkpath(profile.filePath(QStringLiteral("data")));
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("goshosdock-shortcuts-test"));
    KLocalizedString::setApplicationDomain("goshosdock-shortcuts-test");
    GlobalShortcutsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_globalshortcuts.moc"
