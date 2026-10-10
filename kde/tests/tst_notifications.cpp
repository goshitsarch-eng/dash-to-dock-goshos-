/*
    SPDX-FileCopyrightText: 2026 Goshos Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "smartlauncheritem.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KService>
#include <KConfigGroup>
#include <KSharedConfig>
#include <notification.h>
#include <notifications.h>
#include <server.h>
#include <settings.h>

using namespace NotificationManager;

class ObservableNotifications : public Notifications
{
public:
    using Notifications::componentComplete;
};

class NotificationsTest : public QObject
{
    Q_OBJECT

    static QString desktopEntry() { return QStringLiteral("org.gosh.notifications-test"); }
    static QString storageId() { return desktopEntry() + QStringLiteral(".desktop"); }

    static QModelIndex findNotification(const Notifications &model, uint id)
    {
        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex index = model.index(row, 0);
            if (index.data(Notifications::IdRole).toUInt() == id) {
                return index;
            }
        }
        return {};
    }

    static void sendUnity(const QDBusConnection &connection, const QVariantMap &values)
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/Launcher"),
                                                        QStringLiteral("com.canonical.Unity.LauncherEntry"),
                                                        QStringLiteral("Update"));
        signal.setArguments({QString(QStringLiteral("application://") + storageId()), values});
        QVERIFY(connection.send(signal));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        const QString applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
        QVERIFY(QDir().mkpath(applications));
        QFile desktopFile(applications + QLatin1Char('/') + storageId());
        QVERIFY(desktopFile.open(QIODevice::WriteOnly));
        desktopFile.write("[Desktop Entry]\nType=Application\nName=Goshos Notification Test\nExec=/usr/bin/true\nIcon=application-x-executable\n");
        desktopFile.close();
        QFile aliasFile(applications + QStringLiteral("/org.gosh.notifications-alias.desktop"));
        QVERIFY(aliasFile.open(QIODevice::WriteOnly));
        aliasFile.write("[Desktop Entry]\nType=Application\nName=Goshos Alias Test\nExec=/usr/bin/true\nIcon=application-x-executable\n");
        aliasFile.close();
        const QString menus = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/menus");
        QVERIFY(QDir().mkpath(menus));
        QFile menu(menus + QLatin1Char('/') + qEnvironmentVariable("XDG_MENU_PREFIX") + QStringLiteral("applications.menu"));
        QVERIFY(menu.open(QIODevice::WriteOnly));
        menu.write("<!DOCTYPE Menu PUBLIC '-//freedesktop//DTD Menu 1.0//EN' 'http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd'>\n"
                   "<Menu><Name>Applications</Name><DefaultAppDirs/><Include><All/></Include></Menu>\n");
        menu.close();
        QVERIFY(KService::serviceByStorageId(storageId()));
        QVERIFY(Server::self().init());
    }

    void aggregationPolicy_data()
    {
        QTest::addColumn<int>("applicationCount");
        QTest::addColumn<bool>("applicationVisible");
        QTest::addColumn<int>("notifications");
        QTest::addColumn<bool>("notificationsEnabled");
        QTest::addColumn<bool>("applicationOverrides");
        QTest::addColumn<int>("expected");

        QTest::newRow("ordinary-only") << 0 << false << 3 << true << true << 3;
        QTest::newRow("application-preferred") << 4 << true << 3 << true << true << 4;
        QTest::newRow("sum") << 4 << true << 3 << true << false << 7;
        QTest::newRow("hidden-application-count") << 4 << false << 3 << true << true << 3;
        QTest::newRow("notification-counter-disabled") << 4 << true << 3 << false << false << 4;
        QTest::newRow("zero-application-fallback") << 0 << true << 3 << true << true << 3;
        QTest::newRow("negative-counts") << -1 << true << -2 << true << false << 0;
        QTest::newRow("sum-overflow") << std::numeric_limits<int>::max() << true << 3 << true << false << std::numeric_limits<int>::max();
    }

    void aggregationPolicy()
    {
        QFETCH(int, applicationCount);
        QFETCH(bool, applicationVisible);
        QFETCH(int, notifications);
        QFETCH(bool, notificationsEnabled);
        QFETCH(bool, applicationOverrides);
        QFETCH(int, expected);
        QCOMPARE(SmartLauncher::combinedBadgeCount(applicationCount, applicationVisible, notifications, notificationsEnabled, applicationOverrides), expected);
    }

    void existingUnityObserverPreservesAliasesAndSignals()
    {
        auto bus = QDBusConnection::sessionBus();
        QObject nativeObserver;
        QVERIFY(bus.registerObject(QStringLiteral("/Unity"), &nativeObserver));
        QVERIFY(bus.registerService(QStringLiteral("com.canonical.Unity")));
        const QString alias = QStringLiteral("org.gosh.notifications-alias.desktop");
        KConfigGroup mappings(KSharedConfig::openConfig(QStringLiteral("taskmanagerrulesrc")), QStringLiteral("Unity Launcher Mapping"));
        mappings.writeEntry(alias, storageId());
        mappings.sync();
        Settings settings;
        settings.setBadgesInTaskManager(true);
        settings.resetNotificationsInhibitedUntil();
        settings.save();
        {
            SmartLauncher::Item item;
            item.setLauncherUrl(QUrl(QStringLiteral("applications:") + alias));
            QCOMPARE(bus.objectRegisteredAt(QStringLiteral("/Unity")), &nativeObserver);
            sendUnity(bus, {{QStringLiteral("count"), qint64(7)}, {QStringLiteral("count-visible"), true},
                            {QStringLiteral("progress"), 0.6}, {QStringLiteral("progress-visible"), true},
                            {QStringLiteral("urgent"), true}, {QStringLiteral("updating"), true}});
            QTRY_COMPARE(item.count(), 7);
            QCOMPARE(item.progress(), 60);
            QVERIFY(item.progressVisible());
            QVERIFY(item.urgent());
            QVERIFY(item.updating());
        }
        QCOMPARE(bus.objectRegisteredAt(QStringLiteral("/Unity")), &nativeObserver);
        bus.unregisterObject(QStringLiteral("/Unity"));
        bus.unregisterService(QStringLiteral("com.canonical.Unity"));
        mappings.deleteEntry(alias);
        mappings.sync();
    }

    void reactiveNotificationsAndUnity()
    {
        Settings settings;
        settings.setBadgesInTaskManager(true);
        settings.resetNotificationsInhibitedUntil();
        settings.save();

        ObservableNotifications notifications;
        notifications.setShowExpired(true);
        notifications.setShowDismissed(true);
        notifications.setShowJobs(false);
        notifications.componentComplete();

        SmartLauncher::Item first;
        SmartLauncher::Item second;
        const QUrl launcher(QStringLiteral("applications:") + storageId());
        first.setLauncherUrl(launcher);
        second.setLauncherUrl(launcher);
        QSignalSpy firstChanges(&first, &SmartLauncher::Item::countChanged);
        QCoreApplication::processEvents();

        auto sender = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("notification-app")));
        QVERIFY(sender->isConnected());
        auto notify = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                    QStringLiteral("/org/freedesktop/Notifications"),
                                                    QStringLiteral("org.freedesktop.Notifications"),
                                                    QStringLiteral("Notify"));
        notify.setArguments({QStringLiteral("Goshos Notification Test"), uint(0), QString(), QStringLiteral("First notification"), QString(), QStringList(),
                             QVariantMap{{QStringLiteral("desktop-entry"), desktopEntry()}}, 0});
        QDBusPendingReply<uint> reply = sender->asyncCall(notify);
        QTRY_VERIFY(reply.isFinished());
        QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
        const uint firstId = reply.value();
        QTRY_COMPARE(first.count(), 1);
        QTRY_COMPARE(second.count(), 1);
        QVERIFY(first.countVisible());

        Notification ordinary;
        ordinary.setDesktopEntry(desktopEntry());
        ordinary.setSummary(QStringLiteral("Retained ordinary notification"));
        ordinary.setRead(true);
        ordinary.setExpired(true);
        ordinary.setDismissed(true);
        const uint ordinaryId = Server::self().add(ordinary);
        QTRY_COMPARE(first.count(), 2);

        Notification resident;
        resident.setDesktopEntry(desktopEntry());
        resident.setSummary(QStringLiteral("Resident notification"));
        resident.setResident(true);
        const uint residentId = Server::self().add(resident);
        QTRY_COMPARE(first.count(), 3);
        const QModelIndex residentIndex = findNotification(notifications, residentId);
        QVERIFY(residentIndex.isValid());
        QVERIFY(notifications.setData(residentIndex, true, Notifications::ReadRole));
        QTRY_COMPARE(first.count(), 2);
        QTRY_COMPARE(second.count(), 2);

        sendUnity(*sender, {{QStringLiteral("count"), qint64(5)}, {QStringLiteral("count-visible"), true},
                           {QStringLiteral("progress"), 0.4}, {QStringLiteral("progress-visible"), true},
                           {QStringLiteral("urgent"), true}, {QStringLiteral("updating"), true}});
        QTRY_COMPARE(first.count(), 5);
        QTRY_COMPARE(second.count(), 5);
        QTRY_COMPARE(first.progress(), 40);
        QVERIFY(first.progressVisible());
        QVERIFY(first.urgent());
        QVERIFY(first.updating());

        first.setApplicationCounterOverridesNotifications(false);
        QCOMPARE(first.count(), 7);
        QCOMPARE(second.count(), 5);
        first.setNotificationsEnabled(false);
        QCOMPARE(first.count(), 5);
        first.setNotificationsEnabled(true);
        QCOMPARE(first.count(), 7);

        // Updates received during DND must not escape through raw change signals.
        settings.setNotificationsInhibitedUntil(QDateTime::currentDateTimeUtc().addSecs(30));
        settings.save();
        QTRY_COMPARE(first.count(), 0);
        QVERIFY(!first.countVisible());
        QCOMPARE(first.progress(), 0);
        QVERIFY(!first.progressVisible());
        QVERIFY(!first.urgent());
        QVERIFY(first.updating());
        sendUnity(*sender, {{QStringLiteral("count"), qint64(9)}, {QStringLiteral("progress"), 0.7},
                           {QStringLiteral("urgent"), true}, {QStringLiteral("updating"), false}});
        QTRY_VERIFY(!first.updating()); // The update has reached the real backend.
        QCOMPARE(first.count(), 0);
        QCOMPARE(first.progress(), 0);
        QVERIFY(!first.urgent());
        settings.resetNotificationsInhibitedUntil();
        settings.save();
        QTRY_COMPARE(first.count(), 11);
        QCOMPARE(first.progress(), 70);
        QVERIFY(first.progressVisible());
        QVERIFY(first.urgent());

        // Timed DND restores cached values even without another launcher event.
        settings.setNotificationsInhibitedUntil(QDateTime::currentDateTimeUtc().addSecs(2));
        settings.save();
        QTRY_COMPARE(first.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(first.count(), 11, 4000);

        const Settings::NotificationBehaviors behavior = settings.applicationBehavior(desktopEntry());
        settings.setApplicationBehavior(desktopEntry(), behavior & ~Settings::ShowBadges);
        settings.save();
        QTRY_COMPARE(first.count(), 0);
        settings.setApplicationBehavior(desktopEntry(), behavior | Settings::ShowBadges);
        settings.save();
        QTRY_COMPARE(first.count(), 11);

        settings.setBadgesInTaskManager(false);
        settings.save();
        QTRY_COMPARE(first.count(), 0);
        settings.setBadgesInTaskManager(true);
        settings.save();
        QTRY_COMPARE(first.count(), 11);

        // Disconnect only the Unity sender; retained notifications belong to
        // the desktop session and must remain on both applet instances.
        sendUnity(*sender, {{QStringLiteral("updating"), true}});
        QTRY_VERIFY(first.updating());
        QDBusConnection::disconnectFromBus(QStringLiteral("notification-app"));
        sender.reset();
        QTRY_COMPARE(first.count(), 2);
        QTRY_COMPARE(second.count(), 2);
        QCOMPARE(first.progress(), 0);
        QVERIFY(!first.progressVisible());
        QVERIFY(!first.urgent());
        QVERIFY(!first.updating());

        notifications.close(findNotification(notifications, firstId));
        QTRY_COMPARE(first.count(), 1);
        notifications.close(findNotification(notifications, ordinaryId));
        QTRY_COMPARE(first.count(), 0);
        QVERIFY(!first.countVisible());
        QVERIFY(firstChanges.count() >= 10);
    }
};

int main(int argc, char **argv)
{
    // The test owns its config and desktop entry; it never changes user settings.
    QTemporaryDir environment;
    if (!environment.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", (environment.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (environment.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (environment.path() + QStringLiteral("/cache")).toUtf8());
    QApplication application(argc, argv);
    NotificationsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_notifications.moc"
