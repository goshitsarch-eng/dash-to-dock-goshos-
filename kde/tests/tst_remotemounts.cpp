/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remotemountfixture.h"

#include "remotemounts.h"

#include <QSignalSpy>
#include <QApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QUrl>
#include <KPasswordDialog>

class RemoteMountsTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<RemoteMountFixture> fixture;
    TestMonitor *monitor = nullptr;
    TestMount *add(const char *uri, const char *name, const char *uuid = nullptr) { return fixture->add(uri, name, uuid); }
    void remove(TestMount *mount) { fixture->remove(mount); }
    void complete(TestMount *mount, bool success = true) { fixture->complete(mount, success); }

private Q_SLOTS:
    void init()
    {
        fixture = std::make_unique<RemoteMountFixture>();
        monitor = fixture->monitor;
    }

    void cleanup()
    {
        fixture.reset();
        monitor = nullptr;
    }

    void filtersAndNormalizesMounts()
    {
        add("smb://server/Shared%20Files/", "Shared Files", "share-id");
        add("file:///media/local", "Local volume", "local-id");
        auto *unsupported = add("sftp://server/unmanaged", "Unmanaged");
        unsupported->canUnmount = false;
        auto *shadowed = add("smb://server/hidden", "Hidden");
        g_mount_shadow(G_MOUNT(shadowed));
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QCOMPARE(backend.mounts().size(), 1);
        const auto row = backend.mounts().constFirst().toMap();
        QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("gio:uuid:share-id"));
        QCOMPARE(row.value(QStringLiteral("url")).toString(), QStringLiteral("smb://server/Shared%20Files"));
        QCOMPARE(row.value(QStringLiteral("icon")).toString(), QStringLiteral("folder-remote"));
        QVERIFY(row.value(QStringLiteral("canUnmount")).toBool());
        QVERIFY(!row.value(QStringLiteral("canEject")).toBool());
        QVERIFY(!row.value(QStringLiteral("busy")).toBool());
        g_mount_unshadow(G_MOUNT(shadowed));
    }

    void liveChangesAndStableIdentity()
    {
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QSignalSpy changes(&backend, &RemoteMounts::mountsChanged);
        auto *mount = add("sftp://host/home/user/", "Old name");
        QTRY_COMPARE(backend.mounts().size(), 1);
        const QString id = backend.mounts().constFirst().toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(id, QStringLiteral("gio:uri:sftp://host/home/user"));
        g_free(mount->name);
        mount->name = g_strdup("Renamed");
        g_signal_emit_by_name(monitor, "mount-changed", mount);
        QTRY_COMPARE(backend.mounts().constFirst().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));
        QCOMPARE(backend.mounts().constFirst().toMap().value(QStringLiteral("id")).toString(), id);
        remove(mount);
        QTRY_VERIFY(backend.mounts().isEmpty());
        QCOMPARE(changes.count(), 3);
    }

    void asynchronousUnmountAndEject()
    {
        auto *mount = add("smb://server/share", "Share", "one");
        mount->canEject = true;
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        QSignalSpy failed(&backend, &RemoteMounts::operationFailed);
        const QString id = QStringLiteral("gio:uuid:one");
        QVERIFY(backend.unmount(id));
        QVERIFY(backend.busy(id));
        QVERIFY(backend.mounts().constFirst().toMap().value(QStringLiteral("busy")).toBool());
        QVERIFY(!backend.unmount(id));
        QVERIFY(!mount->lastWasEject);
        QCOMPARE(mount->lastFlags, G_MOUNT_UNMOUNT_NONE);
        complete(mount);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!backend.busy(id));
        QVERIFY(backend.unmount(id, true));
        QVERIFY(mount->lastWasEject);
        QCOMPARE(mount->lastFlags, G_MOUNT_UNMOUNT_NONE);
        complete(mount);
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(failed.count(), 0);
    }

    void failuresClearBusyAndPreserveMessages()
    {
        auto *mount = add("smb://server/share", "Share", "one");
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QSignalSpy failed(&backend, &RemoteMounts::operationFailed);
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        const QString id = QStringLiteral("gio:uuid:one");
        QVERIFY(!backend.unmount(QStringLiteral("stale-id")));
        QVERIFY(!backend.unmount(id, true));
        QCOMPARE(failed.count(), 2);
        QVERIFY(backend.unmount(id));
        complete(mount, false);
        QTRY_COMPARE(failed.count(), 3);
        QCOMPARE(failed.constLast().at(0).toString(), id);
        QCOMPARE(failed.constLast().at(1).toString(), QStringLiteral("Files are still open"));
        QVERIFY(!backend.busy(id));
        QVERIFY(!backend.mounts().constFirst().toMap().value(QStringLiteral("busy")).toBool());
        QCOMPARE(finished.count(), 0);
    }

    void removalDuringOperation()
    {
        auto *mount = add("smb://server/share", "Share", "one");
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        const QString id = QStringLiteral("gio:uuid:one");
        QVERIFY(backend.unmount(id));
        remove(mount); // GTask retains the provider until completion.
        QTRY_VERIFY(backend.mounts().isEmpty());
        complete(mount);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!backend.busy(id));
    }

    void destructionCancelsInFlightOperation()
    {
        auto *mount = add("smb://server/share", "Share", "one");
        auto backend = std::make_unique<RemoteMounts>(G_VOLUME_MONITOR(monitor), nullptr);
        QVERIFY(backend->unmount(QStringLiteral("gio:uuid:one")));
        QVERIFY(!g_cancellable_is_cancelled(g_task_get_cancellable(mount->pending)));
        backend.reset();
        QVERIFY(g_cancellable_is_cancelled(g_task_get_cancellable(mount->pending)));
        bool taskDestroyed = false;
        g_object_weak_ref(G_OBJECT(mount->pending), [](gpointer data, GObject *) { *static_cast<bool *>(data) = true; }, &taskDestroyed);
        complete(mount);
        QTRY_VERIFY(taskDestroyed);
    }

    void unmountedVolumesKeepIdentityAcrossMounting()
    {
        auto *volume = fixture->addVolume("smb://user:do-not-publish@host/share/", "Network volume", "volume-id");
        volume->canEject = true;
        auto *unknown = fixture->addVolume(nullptr, "Discoverable network volume");
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QCOMPARE(backend.mounts().size(), 2);
        const QString id = QStringLiteral("gio:volume:volume-id");
        auto find = [&backend, &id] {
            for (const auto &entry : backend.mounts()) if (entry.toMap().value(QStringLiteral("id")).toString() == id) return entry.toMap();
            return QVariantMap();
        };
        QVERIFY(find().value(QStringLiteral("setupNeeded")).toBool());
        QVERIFY(find().value(QStringLiteral("canMount")).toBool());
        QCOMPARE(find().value(QStringLiteral("url")).toString(), QStringLiteral("smb://user@host/share"));
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        QVERIFY(backend.mount(id));
        QVERIFY(backend.busy(id));
        QVERIFY(!backend.mount(id));
        QVERIFY(!volume->lastWasEject);
        auto *mounted = add("smb://user@host/share", "Mounted share", "different-mount-id");
        fixture->attach(volume, mounted);
        fixture->complete(volume);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(backend.mounts().size(), 2); // Volume and mount are a single row.
        QVERIFY(!find().value(QStringLiteral("setupNeeded")).toBool());
        QVERIFY(!find().value(QStringLiteral("canMount")).toBool());
        QVERIFY(find().value(QStringLiteral("canUnmount")).toBool());
        QVERIFY(!backend.mount(id));
        QVERIFY(backend.unmount(id, true));
        QVERIFY(volume->lastWasEject);
        fixture->complete(volume);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(unknown->canMount);
    }

    void unmountedVolumeFailureAndEject()
    {
        auto *volume = fixture->addVolume(nullptr, "Network volume", "volume-id");
        volume->canEject = true;
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        const QString id = QStringLiteral("gio:volume:volume-id");
        QSignalSpy failed(&backend, &RemoteMounts::operationFailed);
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        QVERIFY(backend.mount(id));
        fixture->complete(volume, false);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!backend.busy(id));
        QVERIFY(backend.unmount(id, true));
        QVERIFY(volume->lastWasEject);
        fixture->complete(volume);
        QTRY_COMPARE(finished.count(), 1);
    }

    void credentialsQuestionsAndCancellationUseNativeDialogs()
    {
        auto *volume = fixture->addVolume("smb://host/share", "Network volume", "volume-id");
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        const QString id = QStringLiteral("gio:volume:volume-id");
        QVERIFY(backend.mount(id));
        int reply = -1;
        g_signal_connect(volume->interaction, "reply", G_CALLBACK(+[](GMountOperation *, GMountOperationResult value, gpointer data) {
            *static_cast<int *>(data) = value;
        }), &reply);
        const auto flags = GAskPasswordFlags(G_ASK_PASSWORD_NEED_PASSWORD | G_ASK_PASSWORD_NEED_USERNAME | G_ASK_PASSWORD_NEED_DOMAIN | G_ASK_PASSWORD_SAVING_SUPPORTED);
        g_signal_emit_by_name(volume->interaction, "ask-password", "Connect to <share>", "initial-user", "initial-domain", flags);
        KPasswordDialog *password = nullptr;
        for (QWidget *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<KPasswordDialog *>(widget); candidate && candidate->isVisible()) password = candidate;
        QVERIFY(password);
        QTest::qWait(100);
        QCOMPARE(reply, -1); // No default GIO UNHANDLED reply while the user types.
        QCOMPARE(password->username(), QStringLiteral("initial-user"));
        QCOMPARE(password->domain(), QStringLiteral("initial-domain"));
        password->setUsername(QStringLiteral("fixture-user"));
        password->setDomain(QStringLiteral("fixture-domain"));
        for (auto *line : password->findChildren<QLineEdit *>()) if (line->echoMode() == QLineEdit::Password) line->setText(QStringLiteral("fixture-secret"));
        QVERIFY(QMetaObject::invokeMethod(password, "accept"));
        QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_HANDLED));
        QCOMPARE(QString::fromUtf8(g_mount_operation_get_username(volume->interaction)), QStringLiteral("fixture-user"));
        QCOMPARE(QString::fromUtf8(g_mount_operation_get_domain(volume->interaction)), QStringLiteral("fixture-domain"));
        QCOMPARE(QString::fromUtf8(g_mount_operation_get_password(volume->interaction)), QStringLiteral("fixture-secret"));
        QCOMPARE(g_mount_operation_get_password_save(volume->interaction), G_PASSWORD_SAVE_NEVER);
        reply = -1;
        const char *choices[] = {"Reconnect", "Keep disconnected", nullptr};
        g_signal_emit_by_name(volume->interaction, "ask-question", "Reconnect this location?", choices);
        QMessageBox *question = nullptr;
        for (QWidget *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<QMessageBox *>(widget); candidate && candidate->isVisible()) question = candidate;
        QVERIFY(question);
        for (auto *button : question->findChildren<QPushButton *>()) if (button->text() == QLatin1String("Keep disconnected")) button->click();
        QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_HANDLED));
        QCOMPARE(g_mount_operation_get_choice(volume->interaction), 1);
        reply = -1;
        g_signal_emit_by_name(volume->interaction, "ask-password", "Try again", "", "", flags);
        for (QWidget *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<KPasswordDialog *>(widget); candidate && candidate->isVisible()) candidate->reject();
        QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_ABORTED));
        fixture->complete(volume, false);
        QTRY_VERIFY(!backend.busy(id));
    }

    void destructionClosesCredentialPromptAndCancelsVolume()
    {
        auto *volume = fixture->addVolume("smb://host/share", "Network volume", "volume-id");
        auto backend = std::make_unique<RemoteMounts>(G_VOLUME_MONITOR(monitor), nullptr);
        QVERIFY(backend->mount(QStringLiteral("gio:volume:volume-id")));
        g_signal_emit_by_name(volume->interaction, "ask-password", "Connect", "", "", G_ASK_PASSWORD_NEED_PASSWORD);
        QPointer<KPasswordDialog> prompt;
        for (QWidget *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<KPasswordDialog *>(widget); candidate && candidate->isVisible()) prompt = candidate;
        QVERIFY(prompt);
        backend.reset();
        QVERIFY(g_cancellable_is_cancelled(g_task_get_cancellable(volume->pending)));
        QVERIFY(!prompt || !prompt->isVisible());
        fixture->complete(volume, false);
        QTRY_VERIFY(prompt.isNull());
    }

    void unixDispatcherCanDispatchGioCallbacks()
    {
        // Run this suite with QT_NO_GLIB=1 as well to exercise the fallback pump.
        auto *mount = add("smb://server/share", "Share", "one");
        RemoteMounts backend(G_VOLUME_MONITOR(monitor), nullptr);
        QSignalSpy finished(&backend, &RemoteMounts::operationFinished);
        QVERIFY(backend.unmount(QStringLiteral("gio:uuid:one")));
        complete(mount);
        QTRY_COMPARE(finished.count(), 1);
    }
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    RemoteMountsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_remotemounts.moc"
