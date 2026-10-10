/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "remotemountfixture.h"
#include "remotemounts.h"
#include "stackbackend.h"

#include <KFilePlacesModel>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class StackBackendTest : public QObject
{
    Q_OBJECT

    static bool writeFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("stack backend test\n") > 0;
    }

    static QVariantMap remotePlace(const StackBackend &backend, const QString &id)
    {
        for (const auto &value : backend.places()) {
            const auto entry = value.toMap();
            if (entry.value(QStringLiteral("remoteMountId")).toString() == id) {
                return entry;
            }
        }
        return {};
    }

private Q_SLOTS:
    void applicationDiscoveryHonorsDesktopVisibility()
    {
        StackBackend backend;
        QTRY_VERIFY_WITH_TIMEOUT(!backend.applications().isEmpty(), 10000);
        QSet<QString> desktopIds;
        for (const auto &value : backend.applications()) {
            const auto entry = value.toMap();
            desktopIds.insert(entry.value(QStringLiteral("desktopId")).toString());
            QCOMPARE(entry.value(QStringLiteral("url")).toUrl().scheme(), QStringLiteral("applications"));
        }
        QVERIFY(desktopIds.contains(QStringLiteral("org.gosh.StackTestVisible.desktop")));
        QVERIFY(!desktopIds.contains(QStringLiteral("org.gosh.StackTestHidden.desktop")));
        QVERIFY(!desktopIds.contains(QStringLiteral("org.gosh.StackTestOtherDesktop.desktop")));
    }

    void invalidUrlReportsError()
    {
        StackBackend backend;
        backend.listFolder(QUrl());
        QVERIFY(!backend.busy());
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.entries().isEmpty());
    }

    void listsFilesFoldersAndHiddenEntries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath(QStringLiteral("visible.txt"))));
        QVERIFY(writeFile(directory.filePath(QStringLiteral(".hidden"))));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("nested")));
        QVERIFY(writeFile(directory.filePath(QStringLiteral("nested/child.txt"))));

        StackBackend backend;
        backend.listFolder(QUrl::fromLocalFile(directory.path()));
        QVERIFY(backend.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 10000);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.totalItems(), 3);

        QSet<QString> names;
        for (const auto &value : backend.entries()) {
            const auto entry = value.toMap();
            const auto name = entry.value(QStringLiteral("name")).toString();
            names.insert(name);
            QVERIFY(entry.value(QStringLiteral("url")).toUrl().isLocalFile());
            QVERIFY(!entry.value(QStringLiteral("icon")).toString().isEmpty());
            QVERIFY(!entry.value(QStringLiteral("mimeType")).toString().isEmpty());
            QVERIFY(entry.value(QStringLiteral("modified")).toLongLong() > 0);
            QCOMPARE(entry.value(QStringLiteral("isDir")).toBool(), name == QLatin1String("nested"));
            QCOMPARE(entry.value(QStringLiteral("hidden")).toBool(), name == QLatin1String(".hidden"));
        }
        QCOMPARE(names, (QSet<QString>{QStringLiteral("visible.txt"), QStringLiteral(".hidden"), QStringLiteral("nested")}));
    }

    void switchingFoldersDiscardsStaleResults()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid() && second.isValid());
        for (int i = 0; i < 100; ++i) {
            QVERIFY(writeFile(first.filePath(QString::number(i) + QStringLiteral(".txt"))));
        }
        QVERIFY(writeFile(second.filePath(QStringLiteral("current.txt"))));

        StackBackend backend;
        backend.listFolder(QUrl::fromLocalFile(first.path()));
        backend.listFolder(QUrl::fromLocalFile(second.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 10000);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.totalItems(), 1);
        QCOMPARE(backend.entries().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("current.txt"));
        QCOMPARE(backend.entries().first().toMap().value(QStringLiteral("url")).toUrl(), QUrl::fromLocalFile(second.filePath(QStringLiteral("current.txt"))));
    }

    void cancellationStopsPublication()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath(QStringLiteral("file.txt"))));
        StackBackend backend;
        backend.listFolder(QUrl::fromLocalFile(directory.path()));
        backend.cancelListing();
        QVERIFY(!backend.busy());
        QTest::qWait(100);
        QCOMPARE(backend.totalItems(), 0);
    }

    void localFolderChangesRefreshEntries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath(QStringLiteral("first.txt"))));
        StackBackend backend;
        backend.listFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 10000);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.totalItems(), 1);

        QVERIFY(writeFile(directory.filePath(QStringLiteral("second.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(backend.totalItems(), 2, 10000);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
    }

    void missingDirectoryReportsError()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        StackBackend backend;
        backend.listFolder(QUrl::fromLocalFile(directory.filePath(QStringLiteral("does-not-exist"))));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 10000);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.entries().isEmpty());
    }

    void invalidDeviceReportsError()
    {
        StackBackend backend;
        backend.setupPlace(-1);
        QVERIFY(!backend.error().isEmpty());
    }

    void stalePlaceIdentityRejectsAction()
    {
        StackBackend backend;
        QVERIFY(!backend.places().isEmpty());
        const auto place = backend.places().first().toMap();
        QVERIFY(!place.value(QStringLiteral("id")).toString().isEmpty());
        backend.setupPlace(place.value(QStringLiteral("index")).toInt(), QStringLiteral("device:no-longer-present"));
        QVERIFY(!backend.error().isEmpty());
    }

    void discoveredRemoteMountsSupportSafeDisconnectAndErrors()
    {
        RemoteMountFixture fixture;
        auto *mount = fixture.add("smb://server/share/", "Connected Share", "share-id");
        mount->canEject = true;
        RemoteMounts remote(G_VOLUME_MONITOR(fixture.monitor), nullptr);
        StackBackend backend(&remote, nullptr);
        const QString id = QStringLiteral("gio:uuid:share-id");
        const auto place = remotePlace(backend, id);
        QVERIFY(!place.isEmpty());
        QCOMPARE(place.value(QStringLiteral("id")).toString(), id);
        QVERIFY(place.value(QStringLiteral("isNetwork")).toBool());
        QVERIFY(place.value(QStringLiteral("canTeardown")).toBool());
        QVERIFY(!place.value(QStringLiteral("setupNeeded")).toBool());

        const int row = place.value(QStringLiteral("index")).toInt();
        backend.teardownPlace(row, id);
        QVERIFY(mount->pending);
        QVERIFY(!mount->lastWasEject);
        QCOMPARE(mount->lastFlags, G_MOUNT_UNMOUNT_NONE);
        QVERIFY(remotePlace(backend, id).value(QStringLiteral("busy")).toBool());
        backend.ejectPlace(row, id); // A second operation must not replace the first.
        QVERIFY(!mount->lastWasEject);
        fixture.complete(mount, false);
        QTRY_COMPARE(backend.error(), QStringLiteral("Files are still open"));
        QVERIFY(!remotePlace(backend, id).value(QStringLiteral("busy")).toBool());

        // Native Places can finish asynchronous discovery while GIO is working.
        // A newly opened menu reads the current row and keeps the same stable ID.
        backend.ejectPlace(remotePlace(backend, id).value(QStringLiteral("index")).toInt(), id);
        QVERIFY(mount->lastWasEject);
        fixture.complete(mount);
        QTRY_VERIFY(!remote.busy(id));
    }

    void remoteMountMergesWithKdeBookmarkAndRetainsDisconnect()
    {
        RemoteMountFixture fixture;
        auto *mount = fixture.add("sftp://host/shared/", "Connected Share", "bookmarked-id");
        RemoteMounts remote(G_VOLUME_MONITOR(fixture.monitor), nullptr);
        StackBackend backend(&remote, nullptr);
        auto *places = backend.findChild<KFilePlacesModel *>();
        QVERIFY(places);
        places->addPlace(QStringLiteral("Favorite Share"), QUrl(QStringLiteral("sftp://host/shared")), QStringLiteral("folder-network"));
        backend.refresh();
        const auto place = remotePlace(backend, QStringLiteral("gio:uuid:bookmarked-id"));
        QCOMPARE(place.value(QStringLiteral("name")).toString(), QStringLiteral("Favorite Share"));
        QCOMPARE(place.value(QStringLiteral("id")).toString(), QStringLiteral("url:sftp://host/shared"));
        int matching = 0;
        for (const auto &value : backend.places()) {
            matching += value.toMap().value(QStringLiteral("remoteMountId")).toString() == QStringLiteral("gio:uuid:bookmarked-id");
        }
        QCOMPARE(matching, 1);
        QVERIFY(place.value(QStringLiteral("canTeardown")).toBool());
        backend.teardownPlace(place.value(QStringLiteral("index")).toInt(), place.value(QStringLiteral("id")).toString());
        QVERIFY(mount->pending);
        fixture.complete(mount);
        QTRY_VERIFY(!remote.busy(QStringLiteral("gio:uuid:bookmarked-id")));
    }

    void staleRemoteRowCannotDisconnectReplacement()
    {
        RemoteMountFixture fixture;
        auto *first = fixture.add("smb://server/first", "First", "first-id");
        RemoteMounts remote(G_VOLUME_MONITOR(fixture.monitor), nullptr);
        StackBackend backend(&remote, nullptr);
        const auto original = remotePlace(backend, QStringLiteral("gio:uuid:first-id"));
        fixture.remove(first);
        auto *second = fixture.add("smb://server/second", "Second", "second-id");
        QTRY_VERIFY(!remotePlace(backend, QStringLiteral("gio:uuid:second-id")).isEmpty());
        backend.teardownPlace(original.value(QStringLiteral("index")).toInt(), original.value(QStringLiteral("id")).toString());
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!second->pending);
    }

    void explicitLocationLaunchRequestsNewDolphinWindow()
    {
        QTemporaryDir directory;
        const QString output = directory.filePath(QStringLiteral("arguments.json"));
        qputenv("GOSHOS_TEST_LAUNCH_OUTPUT", output.toUtf8());
        const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("folder with 'quotes' & spaces")));
        StackBackend backend;
        backend.openNewWindow(url);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto arguments = QJsonDocument::fromJson(file.readAll()).array().toVariantList();
        QVERIFY(arguments.contains(QStringLiteral("--new-window")));
        QVERIFY(arguments.contains(url.toString()) || arguments.contains(url.toLocalFile()));
        QVERIFY(backend.error().isEmpty());
        qunsetenv("GOSHOS_TEST_LAUNCH_OUTPUT");
    }

    void unmountedNetworkVolumeOpensResolvedLocationAfterSuccess()
    {
        QTemporaryDir directory;
        const QString output = directory.filePath(QStringLiteral("arguments.json"));
        qputenv("GOSHOS_TEST_LAUNCH_OUTPUT", output.toUtf8());
        RemoteMountFixture fixture;
        auto *volume = fixture.addVolume(nullptr, "Disconnected Share", "mount-later");
        fixture.addVolume(nullptr, "Another Disconnected Share", "another-volume");
        RemoteMounts remote(G_VOLUME_MONITOR(fixture.monitor), nullptr);
        StackBackend backend(&remote, nullptr);
        const QString id = QStringLiteral("gio:volume:mount-later");
        auto place = remotePlace(backend, id);
        QVERIFY(!place.isEmpty());
        QVERIFY(place.value(QStringLiteral("setupNeeded")).toBool());
        QVERIFY(place.value(QStringLiteral("canMount")).toBool());
        QVERIFY(place.value(QStringLiteral("url")).toUrl().isEmpty());
        // Unknown activation URLs must not collapse unrelated volumes into one row.
        QVERIFY(!remotePlace(backend, QStringLiteral("gio:volume:another-volume")).isEmpty());
        backend.setupPlace(place.value(QStringLiteral("index")).toInt(), id);
        QVERIFY(volume->pending);
        QVERIFY(remotePlace(backend, id).value(QStringLiteral("busy")).toBool());
        QVERIFY(!QFileInfo::exists(output));
        fixture.complete(volume, false);
        QTRY_COMPARE(backend.error(), QStringLiteral("Connection denied"));
        QVERIFY(!QFileInfo::exists(output));

        place = remotePlace(backend, id);
        backend.setupPlace(place.value(QStringLiteral("index")).toInt(), id);
        QVERIFY(volume->pending);
        const QUrl mountedUrl = QUrl::fromLocalFile(directory.path());
        auto *mount = fixture.add(mountedUrl.toEncoded().constData(), "Connected Share");
        fixture.attach(volume, mount);
        fixture.complete(volume);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
        QVERIFY(!remotePlace(backend, id).value(QStringLiteral("setupNeeded")).toBool());
        QCOMPARE(remotePlace(backend, id).value(QStringLiteral("url")).toUrl(), mountedUrl);
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto arguments = QJsonDocument::fromJson(file.readAll()).array().toVariantList();
        QVERIFY(arguments.contains(mountedUrl.toString()) || arguments.contains(mountedUrl.toLocalFile()));
        qunsetenv("GOSHOS_TEST_LAUNCH_OUTPUT");
    }
};

int main(int argc, char **argv)
{
    // A real KIO ApplicationLauncherJob starts this harmless helper through a
    // fixture desktop entry, so the test checks arguments without opening Dolphin.
    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--launch-helper")) {
        QCoreApplication helper(argc, argv);
        QFile output(QString::fromUtf8(qgetenv("GOSHOS_TEST_LAUNCH_OUTPUT")));
        if (!output.open(QIODevice::WriteOnly)) return 1;
        output.write(QJsonDocument(QJsonArray::fromStringList(helper.arguments().mid(1))).toJson());
        return 0;
    }
    // Keep discovery and all runtime caches out of the developer's own profile.
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
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    qputenv("XDG_MENU_PREFIX", "");
    const QString applicationsDirectory = profile.filePath(QStringLiteral("data/applications"));
    QDir().mkpath(applicationsDirectory);
    QDir().mkpath(profile.filePath(QStringLiteral("config/menus")));
    const QList<QPair<QString, QByteArray>> applications{
        {QStringLiteral("Visible"), QByteArrayLiteral("OnlyShowIn=KDE;\n")},
        {QStringLiteral("Hidden"), QByteArrayLiteral("NoDisplay=true\n")},
        {QStringLiteral("OtherDesktop"), QByteArrayLiteral("OnlyShowIn=GNOME;\n")},
    };
    for (const auto &[name, visibility] : applications) {
        QFile desktopFile(applicationsDirectory + QStringLiteral("/org.gosh.StackTest") + name + QStringLiteral(".desktop"));
        if (!desktopFile.open(QIODevice::WriteOnly)) {
            return 1;
        }
        desktopFile.write("[Desktop Entry]\nType=Application\nName=Stack Test " + name.toUtf8()
                          + "\nExec=/bin/true\nIcon=application-x-executable\n" + visibility);
    }
    QFile dolphin(applicationsDirectory + QStringLiteral("/org.kde.dolphin.desktop"));
    if (!dolphin.open(QIODevice::WriteOnly)) return 1;
    dolphin.write("[Desktop Entry]\nType=Application\nName=Test File Manager\nMimeType=inode/directory;\nExec=\""
                  + QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath().toUtf8()
                  + "\" --launch-helper %u\nIcon=system-file-manager\n");
    dolphin.close();
    QFile defaults(profile.filePath(QStringLiteral("config/mimeapps.list")));
    if (!defaults.open(QIODevice::WriteOnly)) return 1;
    defaults.write("[Default Applications]\ninode/directory=org.kde.dolphin.desktop;\n");
    defaults.close();
    QFile menu(profile.filePath(QStringLiteral("config/menus/applications.menu")));
    if (!menu.open(QIODevice::WriteOnly)) {
        return 1;
    }
    menu.write("<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\" \"http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd\">\n"
               "<Menu><Name>Applications</Name><AppDir>" + applicationsDirectory.toUtf8() + "</AppDir><Include><All/></Include></Menu>\n");
    menu.close();
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("goshosdock-stackbackend-test"));
    StackBackendTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_stackbackend.moc"
