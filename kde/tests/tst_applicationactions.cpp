/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "applicationactions.h"

#include <KIO/CommandLauncherJob>
#include <QApplication>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FakeSwitcheroo : public QDBusVirtualObject
{
public:
    QList<QVariantMap> gpus{
        {{QStringLiteral("Name"), QStringLiteral("Integrated")}, {QStringLiteral("Default"), true},
         {QStringLiteral("Environment"), QStringList{QStringLiteral("GPU_TEST_ROUTE"), QStringLiteral("integrated")}}},
        {{QStringLiteral("Name"), QStringLiteral("Discrete")}, {QStringLiteral("Default"), false}, {QStringLiteral("Discrete"), true},
         {QStringLiteral("Environment"), QStringList{QStringLiteral("GPU_TEST_ROUTE"), QStringLiteral("discrete"), QStringLiteral("DRI_PRIME"), QStringLiteral("1")}}},
    };

    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() != QStringLiteral("GetAll")) {
            return false;
        }
        const QVariantMap properties{{QStringLiteral("GPUs"), QVariant::fromValue(gpus)}};
        return connection.send(message.createReply(QVariant::fromValue(properties)));
    }
};

class ApplicationActionsTest : public QObject
{
    Q_OBJECT
private:
    static void write(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private Q_SLOTS:
    void gpuLaunchAndChanges()
    {
        qDBusRegisterMetaType<QList<QVariantMap>>();
        const auto connection = QDBusConnection::sessionBus();
        QVERIFY(connection.isConnected());
        auto serverConnection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("switcheroo-test"));
        QVERIFY(serverConnection.registerService(QStringLiteral("net.hadess.SwitcherooControl")));
        FakeSwitcheroo server;
        QVERIFY(serverConnection.registerVirtualObject(QStringLiteral("/net/hadess/SwitcherooControl"), &server));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString desktopPath = directory.filePath(QStringLiteral("gpu-test.desktop"));
        const QString output = directory.filePath(QStringLiteral("launch output.txt"));
        const QString desktop = QStringLiteral("[Desktop Entry]\nType=Application\nName=GPU test\nExec=\"%1\" --dump-route \"%2\"\nPath=%3\n")
            .arg(QCoreApplication::applicationFilePath(), output, directory.path());
        write(desktopPath, desktop.toUtf8());
        QVERIFY(QFile::setPermissions(desktopPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const KService::Ptr service(new KService(desktopPath));
        ApplicationActions actions(connection);
        QSignalSpy changed(&actions, &ApplicationActions::gpuChoicesChanged);
        QTRY_COMPARE(changed.count(), 1);
        QObject actionOwner;
        QCOMPARE(actions.gpuActions(QUrl::fromLocalFile(desktopPath), &actionOwner).size(), 2);

        const auto previousPrime = qgetenv("DRI_PRIME");
        qputenv("DRI_PRIME", "inherited");
        for (int gpu = 0; gpu < 2; ++gpu) {
            QFile::remove(output);
            auto *job = actions.gpuLaunchJob(service, gpu);
            QVERIFY(job);
            QCOMPARE(job->workingDirectory(), directory.path());
            job->setAutoDelete(false);
            QSignalSpy finished(job, &KJob::result);
            job->start();
            QTRY_COMPARE(finished.count(), 1);
            QCOMPARE(job->error(), 0);
            const QByteArray expected = gpu == 0 ? QByteArray("integrated\n\n") : QByteArray("discrete\n1\n");
            QTRY_COMPARE(read(output), expected);
            delete job;
        }
        if (previousPrime.isNull()) {
            qunsetenv("DRI_PRIME");
        } else {
            qputenv("DRI_PRIME", previousPrime);
        }

        // Selection must not mutate the application's desktop entry.
        QCOMPARE(read(desktopPath), desktop.toUtf8());
        QVERIFY(QFile::setPermissions(desktopPath, QFile::ReadOwner | QFile::WriteOwner));
        if (QFileInfo(desktopPath).ownerId() != 0) {
            QVERIFY(!actions.gpuLaunchJob(service, 0));
        }

        server.gpus.removeLast();
        auto signal = QDBusMessage::createSignal(QStringLiteral("/net/hadess/SwitcherooControl"),
                                                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        signal.setArguments({QStringLiteral("net.hadess.SwitcherooControl"), QVariantMap(), QStringList{QStringLiteral("GPUs")}});
        QVERIFY(serverConnection.send(signal));
        QTRY_COMPARE(changed.count(), 2);
        QVERIFY(actions.gpuActions(QUrl::fromLocalFile(desktopPath), &actionOwner).isEmpty());
        QVERIFY(serverConnection.unregisterService(QStringLiteral("net.hadess.SwitcherooControl")));
        QTRY_COMPARE(changed.count(), 3);
        QVERIFY(!actions.gpuLaunchJob(service, 0));
    }

    void appstreamMapping()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("metainfo"))));
        const QString desktop = directory.filePath(QStringLiteral("example.desktop"));
        write(desktop, "[Desktop Entry]\nType=Application\nName=Example\nExec=/bin/true\n");
        const KService::Ptr service(new KService(desktop));
        const QString metadata = directory.filePath(QStringLiteral("metainfo/example.xml"));
        write(metadata, "<components><component><id>org.Example.DifferentId</id><launchable type=\"desktop-id\">example.desktop</launchable>"
                        "<provides><id>must.not.replace.component</id></provides></component><component><id>other.component</id></component></components>");
        const QUrl url = ApplicationActions::detailsUrl(service, {directory.path()});
        QCOMPARE(url.scheme(), QStringLiteral("appstream"));
        QCOMPARE(url.path(), QStringLiteral("org.Example.DifferentId"));
        QCOMPARE(url.toString(), QStringLiteral("appstream:org.Example.DifferentId"));
        write(metadata, "<component><id>other.component</id><launchable type=\"desktop-id\">other.desktop</launchable></component>");
        QVERIFY(ApplicationActions::detailsUrl(service, {directory.path()}).isEmpty());
        write(metadata, "<component><id>broken");
        QVERIFY(ApplicationActions::detailsUrl(service, {directory.path()}).isEmpty());
    }
};

int main(int argc, char **argv)
{
    if (argc == 3 && QByteArray(argv[1]) == "--dump-route") {
        QFile output(QString::fromLocal8Bit(argv[2]));
        if (!output.open(QIODevice::WriteOnly)) {
            return 1;
        }
        output.write(qgetenv("GPU_TEST_ROUTE") + '\n' + qgetenv("DRI_PRIME") + '\n');
        return 0;
    }
    QApplication application(argc, argv);
    ApplicationActionsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_applicationactions.moc"
