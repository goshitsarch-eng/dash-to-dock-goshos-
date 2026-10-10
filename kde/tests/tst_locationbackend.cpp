/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "locationbackend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QSignalSpy>
#include <QRect>
#include <QTest>

using LocationMap = QMap<QString, QStringList>;

class FakeFileManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
    Q_PROPERTY(LocationMap OpenWindowsWithLocations READ locations)
public:
    LocationMap values;
    LocationMap locations() const { return values; }
};

class FakeDolphin : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.dolphin.MainWindow")
public:
    QStringList exact;
    Q_SCRIPTABLE bool isUrlOpen(const QString &url) { return exact.contains(url); }
};

class FakeTasks : public QAbstractListModel
{
    Q_OBJECT
public:
    enum { Window = Qt::UserRole, AppId, AppPid, WinIdList, Active, Minimized, MenuService, Urgent, LastActivated, Geometry };
    QList<QVariantMap> windows;
    QString lastMethod;
    QList<int> requestedRows;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : windows.size(); }
    QHash<int, QByteArray> roleNames() const override {
        return {{Window, "IsWindow"}, {AppId, "AppId"}, {AppPid, "AppPid"}, {WinIdList, "WinIdList"},
                {Active, "IsActive"}, {Minimized, "IsMinimized"}, {MenuService, "ApplicationMenuServiceName"},
                {Urgent, "IsDemandingAttention"}, {LastActivated, "LastActivated"}, {Geometry, "Geometry"}};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= windows.size()) return {};
        const auto row = windows[index.row()];
        switch (role) {
        case Qt::DisplayRole: return QStringLiteral("A deliberately unrelated window title");
        case Window: return true;
        case AppId: return row.value(QStringLiteral("appId"), QStringLiteral("org.kde.dolphin"));
        case AppPid: return uint(QCoreApplication::applicationPid());
        case WinIdList: return QVariantList{row.value(QStringLiteral("id"))};
        case Active: return row.value(QStringLiteral("active"), false);
        case Minimized: return row.value(QStringLiteral("minimized"), false);
        case Urgent: return row.value(QStringLiteral("urgent"), false);
        case LastActivated: return row.value(QStringLiteral("lastActivated"), 0);
        case Geometry: return QRect(20, 40, 1200, 800);
        default: return {};
        }
    }
    void addWindow(const QVariant &id) {
        beginInsertRows({}, windows.size(), windows.size());
        windows.append({{QStringLiteral("id"), id}});
        endInsertRows();
    }
    Q_INVOKABLE void requestActivate(const QModelIndex &index) { lastMethod = QStringLiteral("activate"); requestedRows.append(index.row()); }
    Q_INVOKABLE void requestToggleMinimized(const QModelIndex &index) { lastMethod = QStringLiteral("minimize"); requestedRows.append(index.row()); }
    Q_INVOKABLE void requestClose(const QModelIndex &index) { lastMethod = QStringLiteral("close"); requestedRows.append(index.row()); }
};

class LocationBackendTest : public QObject
{
    Q_OBJECT
    FakeFileManager m_fileManager;
    FakeDolphin m_dolphin;
    const QString m_service = QStringLiteral("org.kde.dolphin-goshos-test");

    void configure(LocationBackend &backend, FakeTasks &tasks, const QStringList &roots) {
        backend.setTasksModel(&tasks);
        backend.setLocations(roots);
        backend.setEnabled(true);
        backend.refresh();
    }

private Q_SLOTS:
    void initTestCase() {
        qDBusRegisterMetaType<LocationMap>();
        QVERIFY(QDBusConnection::sessionBus().registerService(m_service));
        QVERIFY(QDBusConnection::sessionBus().registerObject(QStringLiteral("/dolphin/Dolphin_1"), &m_dolphin, QDBusConnection::ExportScriptableContents));
    }
    void init() {
        m_fileManager.values.clear();
        m_dolphin.exact.clear();
        QVERIFY(QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &m_fileManager, QDBusConnection::ExportAllProperties));
    }
    void cleanup() { QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/freedesktop/FileManager1")); }
    void cleanupTestCase() {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/dolphin/Dolphin_1"));
        QDBusConnection::sessionBus().unregisterService(m_service);
    }

    void subtreeBoundariesAndRemoteUrls() {
        QVERIFY(LocationBackend::containsLocation(QUrl(QStringLiteral("file:///home/example/Documents/")), QUrl(QStringLiteral("file:///home/example/Documents/reports/2026"))));
        QVERIFY(!LocationBackend::containsLocation(QUrl(QStringLiteral("file:///home/example/Documents")), QUrl(QStringLiteral("file:///home/example/Documents-old"))));
        QVERIFY(LocationBackend::containsLocation(QUrl(QStringLiteral("sftp://person:secret@server/home/person")), QUrl(QStringLiteral("sftp://person@server/home/person/report"))));
        QVERIFY(!LocationBackend::containsLocation(QUrl(QStringLiteral("sftp://server/home")), QUrl(QStringLiteral("sftp://other/home/report"))));
        QVERIFY(LocationBackend::containsLocation(QUrl(QStringLiteral("trash:/")), QUrl(QStringLiteral("trash:/0-report"))));
        QVERIFY(!LocationBackend::containsLocation(QUrl(), QUrl(QStringLiteral("file:///tmp"))));
    }

    void publishedWaylandLocationsUseAuthenticatedPidAndOriginalIds() {
        m_fileManager.values.insert(QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid()), {QStringLiteral("file:///home/example/Documents/report")});
        FakeTasks tasks;
        tasks.addWindow(QStringLiteral("native-wayland-uuid"));
        LocationBackend backend;
        configure(backend, tasks, {QStringLiteral("file:///home/example/Documents"), QStringLiteral("file:///home/example/Downloads")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.capability(), QStringLiteral("full"), 5000);
        QCOMPARE(backend.excludedWindowIds(), QVariantList{QStringLiteral("native-wayland-uuid")});
        QCOMPARE(backend.windowsForUrl(QUrl(QStringLiteral("file:///home/example/Documents/"))).size(), 1);
        QCOMPARE(backend.windowsForUrl(QUrl(QStringLiteral("file:///home/example/Documents/"))).first().toMap().value(QStringLiteral("geometry")).toRect(),
                 QRect(20, 40, 1200, 800));
        QVERIFY(backend.windowsForUrl(QUrl(QStringLiteral("file:///home/example/Downloads"))).isEmpty());
    }

    void nativeWindowActionsTargetMatchedIndexes() {
        m_fileManager.values = {{QStringLiteral("x11:123"), {QStringLiteral("file:///tmp/stack/one")}},
                                {QStringLiteral("x11:456"), {QStringLiteral("file:///tmp/stack/two")}}};
        FakeTasks tasks;
        tasks.addWindow(qulonglong(123));
        tasks.addWindow(qulonglong(456));
        tasks.windows[0].insert(QStringLiteral("active"), true);
        LocationBackend backend;
        const QUrl url(QStringLiteral("file:///tmp/stack"));
        configure(backend, tasks, {url.toString()});
        QTRY_COMPARE_WITH_TIMEOUT(backend.excludedWindowIds().size(), 2, 5000);
        QVERIFY(backend.activateWindow(url, qulonglong(456)));
        QCOMPARE(tasks.requestedRows, QList<int>{1});
        tasks.requestedRows.clear();
        QVERIFY(backend.cycle(url, 1));
        QCOMPARE(tasks.requestedRows, QList<int>{1});
        tasks.requestedRows.clear();
        QVERIFY(backend.minimize(url));
        QCOMPARE(tasks.lastMethod, QStringLiteral("minimize"));
        QCOMPARE(tasks.requestedRows, (QList<int>{0, 1}));
        tasks.requestedRows.clear();
        QVERIFY(backend.close(url));
        QCOMPARE(tasks.lastMethod, QStringLiteral("close"));
        QCOMPARE(tasks.requestedRows, (QList<int>{0, 1}));
        QVERIFY(!backend.close(QUrl(QStringLiteral("file:///unmatched"))));
        tasks.requestedRows.clear();
        QVERIFY(backend.closeWindow(url, qulonglong(456)));
        QCOMPARE(tasks.requestedRows, QList<int>{1});
        QVERIFY(!backend.closeWindow(url, QStringLiteral("unrelated-window")));
    }

    void ambiguousWaylandWindowsAreNotGuessed() {
        m_fileManager.values.insert(QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid()), {QStringLiteral("file:///tmp/stack")});
        FakeTasks tasks;
        tasks.addWindow(QStringLiteral("uuid-one"));
        tasks.addWindow(QStringLiteral("uuid-two"));
        LocationBackend backend;
        configure(backend, tasks, {QStringLiteral("file:///tmp/stack")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.capability(), QStringLiteral("full"), 5000);
        QVERIFY(backend.excludedWindowIds().isEmpty());
        QVERIFY(!backend.close(QUrl(QStringLiteral("file:///tmp/stack"))));
    }

    void activationPrefersActiveThenMostRecentWindow() {
        m_fileManager.values = {{QStringLiteral("x11:123"), {QStringLiteral("file:///tmp/stack/one")}},
                                {QStringLiteral("x11:456"), {QStringLiteral("file:///tmp/stack/two")}}};
        FakeTasks tasks;
        tasks.addWindow(qulonglong(123));
        tasks.addWindow(qulonglong(456));
        tasks.windows[0].insert(QStringLiteral("lastActivated"), 1000);
        tasks.windows[1].insert(QStringLiteral("lastActivated"), 2000);
        tasks.windows[1].insert(QStringLiteral("urgent"), true);
        LocationBackend backend;
        const QUrl root(QStringLiteral("file:///tmp/stack"));
        configure(backend, tasks, {root.toString()});
        QTRY_COMPARE_WITH_TIMEOUT(backend.windowsForUrl(root).size(), 2, 5000);
        QVERIFY(backend.windowsForUrl(root)[1].toMap().value(QStringLiteral("urgent")).toBool());
        QVERIFY(backend.activate(root));
        QCOMPARE(tasks.requestedRows, QList<int>{1});
        tasks.requestedRows.clear();
        tasks.windows[0].insert(QStringLiteral("active"), true);
        QVERIFY(backend.activate(root));
        QCOMPARE(tasks.requestedRows, QList<int>{1}); // Attention takes precedence.
        tasks.requestedRows.clear();
        tasks.windows[1].insert(QStringLiteral("urgent"), false);
        QVERIFY(backend.activate(root));
        QCOMPARE(tasks.requestedRows, QList<int>{0});
        tasks.requestedRows.clear();
        QVERIFY(backend.minimize(root, false));
        QCOMPARE(tasks.requestedRows, QList<int>{0});
    }

    void stockDolphinFallbackReportsExactCapability() {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/freedesktop/FileManager1"));
        m_dolphin.exact = {QStringLiteral("file:///tmp/stack")};
        FakeTasks tasks;
        tasks.addWindow(QStringLiteral("stock-window"));
        LocationBackend backend;
        configure(backend, tasks, {QStringLiteral("file:///tmp/stack"), QStringLiteral("file:///tmp")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.capability(), QStringLiteral("exact"), 5000);
        QCOMPARE(backend.windowsForUrl(QUrl(QStringLiteral("file:///tmp/stack"))).size(), 1);
        QVERIFY(backend.windowsForUrl(QUrl(QStringLiteral("file:///tmp"))).isEmpty());
    }

    void disablingClearsExclusionsAndRejectsStaleReplies() {
        m_fileManager.values.insert(QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid()), {QStringLiteral("file:///tmp/stack")});
        FakeTasks tasks;
        tasks.addWindow(QStringLiteral("window"));
        LocationBackend backend;
        configure(backend, tasks, {QStringLiteral("file:///tmp/stack")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.excludedWindowIds().size(), 1, 5000);
        backend.refresh();
        backend.setEnabled(false);
        QTest::qWait(100);
        QCOMPARE(backend.capability(), QStringLiteral("disabled"));
        QVERIFY(backend.excludedWindowIds().isEmpty());
        QVERIFY(!backend.activate(QUrl(QStringLiteral("file:///tmp/stack"))));
    }

    void propertyUpdatesRestoreOrdinaryTaskVisibility() {
        const auto identity = QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid());
        m_fileManager.values.insert(identity, {QStringLiteral("file:///tmp/stack/inside")});
        FakeTasks tasks;
        tasks.addWindow(QStringLiteral("window"));
        LocationBackend backend;
        configure(backend, tasks, {QStringLiteral("file:///tmp/stack")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.excludedWindowIds().size(), 1, 5000);
        m_fileManager.values[identity] = {QStringLiteral("file:///tmp/outside")};
        auto signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.DBus.Properties"),
                                               QStringLiteral("PropertiesChanged"));
        signal.setArguments({QStringLiteral("org.freedesktop.FileManager1"),
                             QVariantMap{{QStringLiteral("OpenWindowsWithLocations"), QVariant::fromValue(m_fileManager.values)}}, QStringList{}});
        QVERIFY(QDBusConnection::sessionBus().send(signal));
        QTRY_VERIFY_WITH_TIMEOUT(backend.excludedWindowIds().isEmpty(), 5000);
        QVERIFY(backend.windowsForUrl(QUrl(QStringLiteral("file:///tmp/stack"))).isEmpty());
    }
};

QTEST_GUILESS_MAIN(LocationBackendTest)
#include "tst_locationbackend.moc"
