/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quicklistbackend.h"

#include <QAction>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QMenu>
#include <QPointer>
#include <QTest>

struct MenuLayout {
    int id;
    QVariantMap properties;
    QVariantList children;
};
Q_DECLARE_METATYPE(MenuLayout)

QDBusArgument &operator<<(QDBusArgument &argument, const MenuLayout &layout)
{
    argument.beginStructure();
    argument << layout.id << layout.properties << layout.children;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, MenuLayout &layout)
{
    argument.beginStructure();
    argument >> layout.id >> layout.properties >> layout.children;
    argument.endStructure();
    return argument;
}

struct PropertyUpdate {
    int id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PropertyUpdate)

QDBusArgument &operator<<(QDBusArgument &argument, const PropertyUpdate &update)
{
    argument.beginStructure();
    argument << update.id << update.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PropertyUpdate &update)
{
    argument.beginStructure();
    argument >> update.id >> update.properties;
    argument.endStructure();
    return argument;
}

struct PropertyRemoval {
    int id;
    QStringList properties;
};
Q_DECLARE_METATYPE(PropertyRemoval)

QDBusArgument &operator<<(QDBusArgument &argument, const PropertyRemoval &removal)
{
    argument.beginStructure();
    argument << removal.id << removal.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PropertyRemoval &removal)
{
    argument.beginStructure();
    argument >> removal.id >> removal.properties;
    argument.endStructure();
    return argument;
}

class FakeMenu : public QDBusVirtualObject
{
    Q_OBJECT
public:
    MenuLayout layout{0, {}, {}};
    QList<int> prepared;
    QList<QPair<int, QString>> events;
    bool needsUpdate = false;
    bool failLayout = false;

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == QStringLiteral("GetLayout")) {
            if (failLayout) {
                connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("Test failure")));
            } else {
                connection.send(message.createReply({quint32(1), QVariant::fromValue(layout)}));
            }
        } else if (message.member() == QStringLiteral("AboutToShow")) {
            prepared.append(message.arguments().at(0).toInt());
            connection.send(message.createReply(QVariantList{needsUpdate}));
        } else if (message.member() == QStringLiteral("Event")) {
            events.append({message.arguments().at(0).toInt(), message.arguments().at(1).toString()});
            connection.send(message.createReply());
        } else {
            return false;
        }
        return true;
    }

    static void announce(const QDBusConnection &connection, const QString &path = QStringLiteral("/Quicklist"))
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/Launcher"), QStringLiteral("com.canonical.Unity.LauncherEntry"), QStringLiteral("Update"));
        signal.setArguments({QStringLiteral("application://org.gosh.quicklist-test.desktop"),
                             QVariantMap{{QStringLiteral("quicklist"), QVariant::fromValue(QDBusObjectPath(path))}}});
        QVERIFY(connection.send(signal));
    }

    static void layoutChanged(const QDBusConnection &connection)
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/Quicklist"), QStringLiteral("com.canonical.dbusmenu"), QStringLiteral("LayoutUpdated"));
        signal.setArguments({quint32(2), 0});
        QVERIFY(connection.send(signal));
    }

    static void propertiesChanged(const QDBusConnection &connection)
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/Quicklist"), QStringLiteral("com.canonical.dbusmenu"), QStringLiteral("ItemsPropertiesUpdated"));
        signal.setArguments({QVariant::fromValue(QList<PropertyUpdate>{{1, {{QStringLiteral("label"), QStringLiteral("Changed")}}}}),
                             QVariant::fromValue(QList<PropertyRemoval>())});
        QVERIFY(connection.send(signal));
    }
};

class QuicklistBackendTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void liveProtocol()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        qDBusRegisterMetaType<MenuLayout>();
        qDBusRegisterMetaType<PropertyUpdate>();
        qDBusRegisterMetaType<QList<PropertyUpdate>>();
        qDBusRegisterMetaType<PropertyRemoval>();
        qDBusRegisterMetaType<QList<PropertyRemoval>>();

        QuicklistBackend backend;
        const QUrl launcher(QStringLiteral("applications:org.gosh.quicklist-test.desktop"));
        auto connection = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("quicklist-server")));
        FakeMenu server;
        auto node = [](int id, QVariantMap properties, QVariantList children = {}) {
            return QVariant::fromValue(MenuLayout{id, std::move(properties), std::move(children)});
        };
        server.layout.children = {
            node(1, {{QStringLiteral("label"), QStringLiteral("_Open & More")}}),
            node(2, {{QStringLiteral("type"), QStringLiteral("separator")}}),
            node(3, {{QStringLiteral("label"), QStringLiteral("Options")}, {QStringLiteral("children-display"), QStringLiteral("submenu")}}, {
                node(4, {{QStringLiteral("label"), QStringLiteral("_Enabled")}, {QStringLiteral("toggle-type"), QStringLiteral("checkmark")}, {QStringLiteral("toggle-state"), 1}}),
                node(5, {{QStringLiteral("label"), QStringLiteral("Hidden")}, {QStringLiteral("visible"), false}, {QStringLiteral("enabled"), false}}),
            }),
        };
        QVERIFY(connection->registerVirtualObject(QStringLiteral("/Quicklist"), &server));

        // Announce before asking for a menu: the applet-wide cache must retain it.
        FakeMenu::announce(*connection);
        QTRY_COMPARE(backend.actions(launcher).size(), 3);
        auto actions = backend.actions(launcher);
        QCOMPARE(actions.at(0).value<QAction *>()->text(), QStringLiteral("&Open && More"));
        QVERIFY(actions.at(1).value<QAction *>()->isSeparator());
        QMenu *submenu = actions.at(2).value<QAction *>()->menu();
        QVERIFY(submenu);
        QCOMPARE(submenu->actions().size(), 2);
        QVERIFY(submenu->actions().at(0)->isChecked());
        QVERIFY(!submenu->actions().at(1)->isVisible());
        QVERIFY(!submenu->actions().at(1)->isEnabled());

        backend.aboutToShow(launcher);
        QTRY_VERIFY(server.prepared.contains(0));
        QTRY_VERIFY(server.events.contains(qMakePair(0, QStringLiteral("opened"))));
        submenu->aboutToShow();
        QTRY_VERIFY(server.prepared.contains(3));
        submenu->actions().at(0)->trigger();
        QTRY_VERIFY(server.events.contains(qMakePair(4, QStringLiteral("clicked"))));
        backend.closed(launcher);
        QTRY_VERIFY(server.events.contains(qMakePair(0, QStringLiteral("closed"))));

        QPointer<QAction> retired = actions.at(0).value<QAction *>();
        server.layout.children[0] = node(1, {{QStringLiteral("label"), QStringLiteral("Changed")}});
        FakeMenu::propertiesChanged(*connection);
        QTRY_COMPARE(backend.actions(launcher).first().value<QAction *>()->text(), QStringLiteral("Changed"));
        QVERIFY(retired.isNull());

        server.layout.children[0] = node(1, {{QStringLiteral("label"), QStringLiteral("Prepared")}});
        server.needsUpdate = true;
        backend.aboutToShow(launcher);
        QTRY_COMPARE(backend.actions(launcher).first().value<QAction *>()->text(), QStringLiteral("Prepared"));
        server.needsUpdate = false;

        // A newer instance wins, then the older provider becomes active again.
        auto second = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("quicklist-second")));
        FakeMenu secondServer;
        secondServer.layout.children = {node(10, {{QStringLiteral("label"), QStringLiteral("Second instance")}})};
        QVERIFY(second->registerVirtualObject(QStringLiteral("/Quicklist"), &secondServer));
        FakeMenu::announce(*second);
        QTRY_COMPARE(backend.actions(launcher).size(), 1);
        QDBusConnection::disconnectFromBus(QStringLiteral("quicklist-second"));
        second.reset();
        QTRY_COMPARE(backend.actions(launcher).size(), 3);

        server.failLayout = true;
        QTest::ignoreMessage(QtWarningMsg, "Quicklist GetLayout failed: \"org.freedesktop.DBus.Error.Failed\"");
        FakeMenu::layoutChanged(*connection);
        QTRY_VERIFY(backend.actions(launcher).isEmpty());
        server.failLayout = false;
        FakeMenu::layoutChanged(*connection);
        QTRY_COMPARE(backend.actions(launcher).size(), 3);

        FakeMenu::announce(*connection, QStringLiteral("/"));
        QTRY_VERIFY(backend.actions(launcher).isEmpty());
        FakeMenu::announce(*connection);
        QTRY_COMPARE(backend.actions(launcher).size(), 3);
        QDBusConnection::disconnectFromBus(QStringLiteral("quicklist-server"));
        connection.reset();
        QTRY_VERIFY(backend.actions(launcher).isEmpty());
    }
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QuicklistBackendTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_quicklistbackend.moc"
