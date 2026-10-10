/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "dockcontroller.h"

#include <KSharedConfig>
#include <KPluginMetaData>
#include <LayerShellQt/Window>
#include <Plasma/Containment>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

class FakeDockService : public QDBusVirtualObject
{
public:
    QStringList scripts;
    QStringList unregistered;
    int registrations = 0;
    bool failRestore = false;
    bool overview = true;
    int overviewHides = 0;
    int overviewToggles = 0;
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == QStringLiteral("OverviewVisible")) {
            return connection.send(message.createReply(overview));
        }
        if (message.member() == QStringLiteral("OverviewAvailable")) {
            return connection.send(message.createReply(true));
        }
        if (message.member() == QStringLiteral("ToggleOverview")) {
            ++overviewToggles;
            overview = !overview;
            auto signal = QDBusMessage::createSignal(QStringLiteral("/org/gosh/GoshosDock"),
                QStringLiteral("org.gosh.GoshosDock.KWin1"), QStringLiteral("OverviewChanged"));
            signal.setArguments({overview});
            connection.send(signal);
            return connection.send(message.createReply(true));
        }
        if (message.member() == QStringLiteral("HideOverview")) {
            ++overviewHides;
            overview = false;
            auto signal = QDBusMessage::createSignal(QStringLiteral("/org/gosh/GoshosDock"),
                QStringLiteral("org.gosh.GoshosDock.KWin1"), QStringLiteral("OverviewChanged"));
            signal.setArguments({false});
            connection.send(signal);
            return connection.send(message.createReply(true));
        }
        if (message.member() == QStringLiteral("evaluateScript")) {
            scripts.append(message.arguments().first().toString());
            return connection.send(failRestore
                ? message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("Test restore failure"))
                : message.createReply(QStringLiteral("goshosdock-mode-applied")));
        }
        if (message.member() == QStringLiteral("RegisterDock")) {
            ++registrations;
            QTimer::singleShot(20, this, [connection, message] {
                connection.send(message.createReply(QStringLiteral("late-token")));
            });
            return true;
        }
        if (message.member() == QStringLiteral("UnregisterDock")) {
            unregistered.append(message.arguments().first().toString());
            return connection.send(message.createReply(true));
        }
        return false;
    }
};

class FakePanelWindow : public QQuickWindow
{
    Q_OBJECT
    Q_PROPERTY(int visibilityMode READ visibilityMode WRITE setVisibilityMode NOTIFY visibilityModeChanged)
public:
    int visibilityMode() const { return m_visibility; }
    void setVisibilityMode(int mode) { m_visibility = mode; Q_EMIT visibilityModeChanged(); }
Q_SIGNALS:
    void visibilityModeChanged();
private:
    int m_visibility = 0;
};

class DockControllerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void ownershipAndRestoration()
    {
        auto connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("dock-test-server"));
        QVERIFY(connection.isConnected());
        QVERIFY(connection.registerService(QStringLiteral("org.kde.plasmashell")));
        QVERIFY(connection.registerService(QStringLiteral("org.gosh.GoshosDock.KWin")));
        // Plasma::Containment constructs an activities consumer. This test
        // exercises its background hints only; reserve the service name so
        // that a real daemon cannot auto-start and inherit the test's output
        // pipe after dbus-run-session (and the test itself) have exited.
        QVERIFY(connection.registerService(QStringLiteral("org.kde.ActivityManager")));
        FakeDockService service;
        QVERIFY(connection.registerVirtualObject(QStringLiteral("/PlasmaShell"), &service));
        QVERIFY(connection.registerVirtualObject(QStringLiteral("/org/gosh/GoshosDock"), &service));

        // A generic item cannot request control of its window or another panel.
        QQuickItem foreignItem;
        DockController foreign;
        QSignalSpy overviewChanges(&foreign, &DockController::overviewVisibleChanged);
        QTRY_VERIFY(foreign.overviewVisible());
        foreign.hideOverview();
        QTRY_VERIFY(!foreign.overviewVisible());
        QCOMPARE(service.overviewHides, 1);
        foreign.hideOverview();
        QCoreApplication::processEvents();
        QCOMPARE(service.overviewHides, 1);
        QCOMPARE(overviewChanges.size(), 2);
        QTRY_VERIFY(foreign.overviewAvailable());
        foreign.toggleOverview();
        QTRY_VERIFY(foreign.overviewVisible());
        QCOMPARE(service.overviewToggles, 1);
        auto availableSignal = QDBusMessage::createSignal(QStringLiteral("/org/gosh/GoshosDock"),
            QStringLiteral("org.gosh.GoshosDock.KWin1"), QStringLiteral("OverviewAvailableChanged"));
        availableSignal.setArguments({false});
        QVERIFY(connection.send(availableSignal));
        QTRY_VERIFY(!foreign.overviewAvailable());
        foreign.toggleOverview();
        QCoreApplication::processEvents();
        QCOMPARE(service.overviewToggles, 1);
        foreign.setTarget(&foreignItem);
        foreign.setEnabled(true);
        QTRY_VERIFY(!foreign.error().isEmpty());
        QVERIFY(!foreign.active());
        QCOMPARE(foreign.panelId(), 0u);
        QCOMPARE(service.registrations, 0);
        QVERIFY(service.scripts.isEmpty());
        foreign.setEnabled(false);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = KSharedConfig::openConfig(directory.filePath(QStringLiteral("recoveryrc")), KConfig::SimpleConfig);
        DockController controller;
        controller.m_backup = KConfigGroup(config, QStringLiteral("NativeDockController"));
        controller.m_backup.writeEntry("originalPanelVisibility", 3);
        controller.m_backup.sync();
        controller.m_savedModeValid = true;
        controller.m_savedPanelId = 42;
        controller.m_savedMode = 3;
        controller.m_token = QStringLiteral("registered-token");
        controller.deactivate();
        QTRY_VERIFY(service.unregistered.contains(QStringLiteral("registered-token")));
        QTRY_COMPARE(service.scripts.size(), 1);
        QVERIFY(service.scripts.first().contains(QStringLiteral("panelById(42)")));
        QVERIFY(service.scripts.first().contains(QStringLiteral("hiding='windowsgobelow'")));
        QTRY_VERIFY(!config->hasGroup(QStringLiteral("NativeDockController")));

        // Failed restoration retains the snapshot, rather than losing recovery.
        service.failRestore = true;
        controller.m_backup.writeEntry("originalPanelVisibility", 1);
        controller.m_backup.sync();
        controller.m_savedModeValid = true;
        controller.m_savedMode = 1;
        controller.deactivate();
        QTRY_COMPARE(service.scripts.size(), 2);
        QTRY_VERIFY(controller.error().contains(QStringLiteral("restore")));
        QCOMPARE(controller.m_backup.readEntry("originalPanelVisibility", -1), 1);

        // Fullscreen reveal promotes only the managed panel and restores its
        // original layer when control ends.
        QQuickWindow window;
        auto *layerWindow = LayerShellQt::Window::get(&window);
        QVERIFY(layerWindow);
        layerWindow->setLayer(LayerShellQt::Window::LayerBottom);
        controller.m_window = &window;
        controller.m_token = QStringLiteral("layer-token");
        controller.raiseManagedPanel();
        QCOMPARE(layerWindow->layer(), LayerShellQt::Window::LayerOverlay);
        controller.deactivate();
        QCOMPARE(layerWindow->layer(), LayerShellQt::Window::LayerBottom);

        // An external panel-editor hiding change invalidates the cached mode;
        // the next synchronized request must reclaim visibility ownership.
        FakePanelWindow panelWindow;
        controller.m_window = &panelWindow;
        controller.m_token = QStringLiteral("visibility-token");
        controller.m_configuration.insert(QStringLiteral("dockFixed"), true);
        controller.m_appliedMode = QStringLiteral("none");
        controller.raiseManagedPanel();
        panelWindow.setVisibilityMode(1);
        QVERIFY(controller.m_appliedMode.isEmpty());
        QVERIFY(controller.m_schedule.isActive());
        controller.m_schedule.stop();
        controller.deactivate();

        // Dedicated dock panels use the applet's own background, including
        // icons-only transparency. Restore the exact native hint afterwards.
        Plasma::Containment containment(nullptr, KPluginMetaData(), {});
        containment.setBackgroundHints(Plasma::Types::TranslucentBackground);
        controller.m_containment = &containment;
        controller.m_token = QStringLiteral("background-token");
        controller.suppressPanelBackground();
        QCOMPARE(containment.backgroundHints(), Plasma::Types::NoBackground);
        containment.setBackgroundHints(Plasma::Types::StandardBackground);
        QCOMPARE(containment.backgroundHints(), Plasma::Types::NoBackground);
        controller.deactivate();
        QCOMPARE(containment.backgroundHints(), Plasma::Types::TranslucentBackground);

        // Even destruction during an in-flight RegisterDock must release the
        // eventual token; plasmashell's DBus owner outlives individual applets.
        auto *pending = new DockController;
        pending->registerDock(24);
        delete pending;
        QTRY_VERIFY(service.unregistered.contains(QStringLiteral("late-token")));
        QCOMPARE(service.registrations, 1);
    }
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    DockControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_dockcontroller.moc"
