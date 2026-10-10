/* SPDX-License-Identifier: GPL-2.0-or-later */
// Include GIO before Qt, whose "signals" macro otherwise conflicts with GLib headers.
#include <gio/gio.h>

#include "remotemounts.h"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QHash>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <KPasswordDialog>
#include <algorithm>
#include <utility>

namespace
{
void ensureGioDispatch()
{
    const auto *dispatcher = QAbstractEventDispatcher::instance();
    if (dispatcher && QByteArray(dispatcher->metaObject()->className()).contains("Glib")) {
        return;
    }
    // Keep dispatch alive until application shutdown, including cancellation
    // callbacks arriving after the last RemoteMounts instance is destroyed.
    static QPointer<QTimer> contextPump;
    if (!contextPump && QCoreApplication::instance()) {
        contextPump = new QTimer(QCoreApplication::instance());
        contextPump->setInterval(50);
        QObject::connect(contextPump, &QTimer::timeout, contextPump, [] {
            for (int count = 0; count < 16 && g_main_context_pending(nullptr); ++count) {
                g_main_context_iteration(nullptr, false);
            }
        });
        contextPump->start();
    }
}

QString takeString(char *value)
{
    const QString result = QString::fromUtf8(value ? value : "");
    g_free(value);
    return result;
}

QString iconName(GIcon *icon)
{
    QString result = QStringLiteral("folder-network");
    if (G_IS_THEMED_ICON(icon)) {
        const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        if (names && names[0]) {
            result = QString::fromUtf8(names[0]);
        }
    } else if (G_IS_FILE_ICON(icon)) {
        result = takeString(g_file_get_uri(g_file_icon_get_file(G_FILE_ICON(icon))));
    }
    g_clear_object(&icon);
    return result;
}

QString fileUri(GFile *file)
{
    if (!file) return {};
    const QUrl url(takeString(g_file_get_uri(file)));
    if (!url.isValid()) return {};
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash | QUrl::RemovePassword).toString(QUrl::FullyEncoded);
}

QString volumeId(GVolume *volume)
{
    const QString uuid = takeString(g_volume_get_uuid(volume));
    if (!uuid.isEmpty()) return QStringLiteral("gio:volume:") + uuid;
    GFile *root = g_volume_get_activation_root(volume);
    const QString uri = fileUri(root);
    g_clear_object(&root);
    if (!uri.isEmpty()) return QStringLiteral("gio:volume:") + uri;
    // Some providers cannot supply a URL until credentials have been accepted.
    QStringList identities{takeString(g_volume_get_name(volume))};
    char **kinds = g_volume_enumerate_identifiers(volume);
    for (char **kind = kinds; kind && *kind; ++kind) {
        identities.append(QString::fromUtf8(*kind) + u'=' + takeString(g_volume_get_identifier(volume, *kind)));
    }
    g_strfreev(kinds);
    identities.sort();
    return QStringLiteral("gio:volume:") + QString::fromLatin1(QCryptographicHash::hash(identities.join(u'\n').toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool isRemoteVolume(GVolume *volume)
{
    if (takeString(g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_CLASS)) == QLatin1String("network")
        || !takeString(g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_NFS_MOUNT)).isEmpty()) return true;
    GFile *root = g_volume_get_activation_root(volume);
    const bool remote = root && !g_file_is_native(root);
    g_clear_object(&root);
    return remote;
}

bool isRemote(GMount *mount, GFile *root)
{
    if (!g_file_is_native(root)) {
        return true;
    }
    GVolume *volume = g_mount_get_volume(mount);
    if (!volume) {
        return false;
    }
    const QString volumeClass = takeString(g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_CLASS));
    g_object_unref(volume);
    return volumeClass == QLatin1String("network");
}
}

class RemoteMounts::Private
{
public:
    enum class OperationKind { MountVolume, EjectVolume, UnmountMount, EjectMount };
    struct Operation {
        QPointer<RemoteMounts> owner;
        QString id;
        GCancellable *cancellable = g_cancellable_new();
        GMountOperation *interaction = g_mount_operation_new();
        OperationKind kind;
        QPointer<QDialog> dialog;

        static void suppressDefaultReply(GMountOperation *native)
        {
            // GMountOperation's default RUN_LAST handler otherwise schedules an
            // UNHANDLED reply while our nonblocking Qt dialog is still open.
            if (const auto *hint = g_signal_get_invocation_hint(native)) g_signal_stop_emission(native, hint->signal_id, hint->detail);
        }

        ~Operation()
        {
            clearDialog();
            g_signal_handlers_disconnect_by_data(interaction, this);
            g_object_unref(interaction);
            g_object_unref(cancellable);
        }

        void clearDialog()
        {
            if (!dialog) return;
            QObject::disconnect(dialog, nullptr, nullptr, nullptr);
            dialog->hide();
            dialog->deleteLater();
            dialog.clear();
        }

        static void askPassword(GMountOperation *native, const char *message, const char *username, const char *domain,
                                GAskPasswordFlags flags, gpointer data)
        {
            suppressDefaultReply(native);
            auto *operation = static_cast<Operation *>(data);
            operation->clearDialog();
            if (!qobject_cast<QApplication *>(QCoreApplication::instance()) || !operation->owner) {
                g_mount_operation_reply(native, G_MOUNT_OPERATION_ABORTED);
                return;
            }
            KPasswordDialog::KPasswordDialogFlags options;
            if (flags & G_ASK_PASSWORD_NEED_USERNAME) options |= KPasswordDialog::ShowUsernameLine;
            if (flags & G_ASK_PASSWORD_NEED_DOMAIN) options |= KPasswordDialog::ShowDomainLine;
            if (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) options |= KPasswordDialog::ShowAnonymousLoginCheckBox;
            if (flags & G_ASK_PASSWORD_SAVING_SUPPORTED) options |= KPasswordDialog::ShowKeepPassword;
            auto *dialog = new KPasswordDialog(QApplication::activeWindow(), options);
            operation->dialog = dialog;
            dialog->setWindowTitle(RemoteMounts::tr("Connect to Network Location"));
            dialog->setPrompt(QString::fromUtf8(message).toHtmlEscaped());
            dialog->setUsername(QString::fromUtf8(username));
            dialog->setDomain(QString::fromUtf8(domain));
            QObject::connect(dialog, &QDialog::finished, dialog, [operation, dialog, flags](int result) {
                if (result == QDialog::Accepted) {
                    if (flags & G_ASK_PASSWORD_NEED_USERNAME) g_mount_operation_set_username(operation->interaction, dialog->username().toUtf8().constData());
                    if (flags & G_ASK_PASSWORD_NEED_DOMAIN) g_mount_operation_set_domain(operation->interaction, dialog->domain().toUtf8().constData());
                    g_mount_operation_set_password(operation->interaction, dialog->password().toUtf8().constData());
                    g_mount_operation_set_anonymous(operation->interaction, dialog->anonymousMode());
                    g_mount_operation_set_password_save(operation->interaction, dialog->keepPassword() ? G_PASSWORD_SAVE_PERMANENTLY : G_PASSWORD_SAVE_NEVER);
                }
                operation->clearDialog();
                g_mount_operation_reply(operation->interaction, result == QDialog::Accepted ? G_MOUNT_OPERATION_HANDLED : G_MOUNT_OPERATION_ABORTED);
            });
            dialog->open();
        }

        static void askQuestion(GMountOperation *native, const char *message, const char *const *choices, gpointer data)
        {
            suppressDefaultReply(native);
            auto *operation = static_cast<Operation *>(data);
            operation->clearDialog();
            if (!qobject_cast<QApplication *>(QCoreApplication::instance()) || !operation->owner) {
                g_mount_operation_reply(native, G_MOUNT_OPERATION_ABORTED);
                return;
            }
            auto *dialog = new QMessageBox(QMessageBox::Question, RemoteMounts::tr("Network Location"), QString::fromUtf8(message),
                                          QMessageBox::Cancel, QApplication::activeWindow());
            dialog->setTextFormat(Qt::PlainText);
            operation->dialog = dialog;
            for (int index = 0; choices && choices[index]; ++index) {
                auto *button = dialog->addButton(QString::fromUtf8(choices[index]), QMessageBox::ActionRole);
                button->setProperty("gioChoice", index);
            }
            QObject::connect(dialog, &QDialog::finished, dialog, [operation, dialog] {
                const auto *button = dialog->clickedButton();
                const QVariant choice = button ? button->property("gioChoice") : QVariant();
                if (choice.isValid()) g_mount_operation_set_choice(operation->interaction, choice.toInt());
                operation->clearDialog();
                g_mount_operation_reply(operation->interaction, choice.isValid() ? G_MOUNT_OPERATION_HANDLED : G_MOUNT_OPERATION_ABORTED);
            });
            dialog->open();
        }

        static void showProcesses(GMountOperation *native, const char *message, GArray *, const char *const *choices, gpointer data)
        {
            askQuestion(native, message, choices, data);
        }
    };

    Private(RemoteMounts *owner, GVolumeMonitor *provider)
        : q(owner)
        , monitor(provider ? G_VOLUME_MONITOR(g_object_ref(provider)) : g_volume_monitor_get())
    {
        for (const char *signal : {"mount-added", "mount-removed", "mount-changed", "volume-added", "volume-removed", "volume-changed"}) {
            g_signal_connect(monitor, signal, G_CALLBACK(monitorChanged), this);
        }

        // Qt normally integrates the default GLib context. Support Qt builds using
        // the Unix dispatcher too, without creating a GTK application or a thread.
        ensureGioDispatch();
        refresh();
    }

    ~Private()
    {
        g_signal_handlers_disconnect_by_data(monitor, this);
        for (auto *operation : std::as_const(operations)) {
            // Completion owns the operation until GIO calls it, even after this
            // QObject disappears. Clear the guard before cancellation callbacks.
            operation->owner.clear();
            operation->clearDialog();
            g_cancellable_cancel(operation->cancellable);
        }
        releaseMounts();
        g_object_unref(monitor);
    }

    static void monitorChanged(GVolumeMonitor *, gpointer, gpointer data)
    {
        auto *self = static_cast<Private *>(data);
        QMetaObject::invokeMethod(self->q, [q = QPointer<RemoteMounts>(self->q)] {
            if (q) {
                q->d->refresh();
            }
        }, Qt::QueuedConnection);
    }

    static void operationCompleted(GObject *source, GAsyncResult *result, gpointer data)
    {
        std::unique_ptr<Operation> operation(static_cast<Operation *>(data));
        GError *error = nullptr;
        bool success = false;
        switch (operation->kind) {
        case OperationKind::MountVolume: success = g_volume_mount_finish(G_VOLUME(source), result, &error); break;
        case OperationKind::EjectVolume: success = g_volume_eject_with_operation_finish(G_VOLUME(source), result, &error); break;
        case OperationKind::UnmountMount: success = g_mount_unmount_with_operation_finish(G_MOUNT(source), result, &error); break;
        case OperationKind::EjectMount: success = g_mount_eject_with_operation_finish(G_MOUNT(source), result, &error); break;
        }
        const QString message = error ? QString::fromUtf8(error->message) : RemoteMounts::tr("The network location operation could not be completed.");
        g_clear_error(&error);
        operation->clearDialog();
        if (const auto owner = operation->owner) {
            owner->d->operations.remove(operation->id);
            owner->d->refresh();
            Q_EMIT owner->busyChanged();
            if (success) {
                Q_EMIT owner->operationFinished(operation->id);
            } else {
                Q_EMIT owner->operationFailed(operation->id, message);
            }
        }
    }

    void releaseMounts()
    {
        for (auto *mount : std::as_const(nativeMounts)) {
            g_object_unref(mount);
        }
        nativeMounts.clear();
        for (auto *volume : std::as_const(nativeVolumes)) g_object_unref(volume);
        nativeVolumes.clear();
    }

    void refresh()
    {
        QHash<QString, GMount *> nextMounts;
        QHash<QString, GVolume *> nextVolumes;
        QVariantList nextRows;
        GList *volumes = g_volume_monitor_get_volumes(monitor);
        for (GList *item = volumes; item; item = item->next) {
            GVolume *volume = G_VOLUME(item->data);
            if (!isRemoteVolume(volume)) continue;
            GMount *mount = g_volume_get_mount(volume);
            if (mount ? g_mount_is_shadowed(mount) || (!g_mount_can_unmount(mount) && !g_mount_can_eject(mount))
                      : !g_volume_can_mount(volume) && !g_volume_can_eject(volume)) {
                g_clear_object(&mount);
                continue;
            }
            GFile *root = mount ? g_mount_get_root(mount) : g_volume_get_activation_root(volume);
            const QString uri = fileUri(root);
            g_clear_object(&root);
            const QString id = volumeId(volume);
            if (nextVolumes.contains(id)) {
                g_clear_object(&mount);
                continue;
            }
            nextVolumes.insert(id, G_VOLUME(g_object_ref(volume)));
            if (mount) nextMounts.insert(id, mount); // Retain get_mount()'s reference.
            nextRows.append(QVariantMap{
                {QStringLiteral("id"), id}, {QStringLiteral("name"), takeString(g_volume_get_name(volume))},
                {QStringLiteral("icon"), iconName(g_volume_get_icon(volume))}, {QStringLiteral("url"), uri},
                {QStringLiteral("setupNeeded"), !mount}, {QStringLiteral("canMount"), !mount && bool(g_volume_can_mount(volume))},
                {QStringLiteral("canUnmount"), mount && bool(g_mount_can_unmount(mount))},
                {QStringLiteral("canEject"), bool(g_volume_can_eject(volume)) || (mount && bool(g_mount_can_eject(mount)))},
                {QStringLiteral("busy"), operations.contains(id)},
            });
        }
        g_list_free_full(volumes, g_object_unref);
        GList *list = g_volume_monitor_get_mounts(monitor);
        for (GList *item = list; item; item = item->next) {
            GMount *mount = G_MOUNT(item->data);
            if (g_mount_is_shadowed(mount) || (!g_mount_can_unmount(mount) && !g_mount_can_eject(mount))) continue;
            GFile *root = g_mount_get_root(mount);
            if (!root) continue;
            const bool remote = isRemote(mount, root);
            const QString uri = fileUri(root);
            g_object_unref(root);
            if (!remote || uri.isEmpty()) continue;
            GVolume *volume = g_mount_get_volume(mount);
            const QString uuid = takeString(g_mount_get_uuid(mount));
            const QString id = volume ? volumeId(volume) : uuid.isEmpty() ? QStringLiteral("gio:uri:") + uri : QStringLiteral("gio:uuid:") + uuid;
            g_clear_object(&volume);
            if (nextMounts.contains(id)) continue;
            nextMounts.insert(id, G_MOUNT(g_object_ref(mount)));
            nextRows.append(QVariantMap{
                {QStringLiteral("id"), id}, {QStringLiteral("name"), takeString(g_mount_get_name(mount))},
                {QStringLiteral("icon"), iconName(g_mount_get_icon(mount))}, {QStringLiteral("url"), uri},
                {QStringLiteral("setupNeeded"), false}, {QStringLiteral("canMount"), false},
                {QStringLiteral("canUnmount"), bool(g_mount_can_unmount(mount))},
                {QStringLiteral("canEject"), bool(g_mount_can_eject(mount))},
                {QStringLiteral("busy"), operations.contains(id)},
            });
        }
        g_list_free_full(list, g_object_unref);
        std::sort(nextRows.begin(), nextRows.end(), [](const QVariant &left, const QVariant &right) {
            const auto a = left.toMap();
            const auto b = right.toMap();
            const int comparison = QString::localeAwareCompare(a.value(QStringLiteral("name")).toString(), b.value(QStringLiteral("name")).toString());
            return comparison ? comparison < 0 : a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
        });
        releaseMounts();
        nativeMounts = std::move(nextMounts);
        nativeVolumes = std::move(nextVolumes);
        if (rows != nextRows) {
            rows = std::move(nextRows);
            Q_EMIT q->mountsChanged();
        }
    }

    Operation *begin(const QString &id, OperationKind kind)
    {
        auto *operation = new Operation;
        operation->owner = q;
        operation->id = id;
        operation->kind = kind;
        g_signal_connect(operation->interaction, "ask-password", G_CALLBACK(Operation::askPassword), operation);
        g_signal_connect(operation->interaction, "ask-question", G_CALLBACK(Operation::askQuestion), operation);
        g_signal_connect(operation->interaction, "show-processes", G_CALLBACK(Operation::showProcesses), operation);
        g_signal_connect(operation->interaction, "aborted", G_CALLBACK(+[](GMountOperation *, gpointer data) {
            static_cast<Operation *>(data)->clearDialog();
        }), operation);
        operations.insert(id, operation);
        return operation;
    }

    RemoteMounts *q;
    GVolumeMonitor *monitor;
    QVariantList rows;
    QHash<QString, GMount *> nativeMounts;
    QHash<QString, GVolume *> nativeVolumes;
    QHash<QString, Operation *> operations;
};

RemoteMounts::RemoteMounts(QObject *parent)
    : RemoteMounts(nullptr, parent)
{
}

RemoteMounts::RemoteMounts(GVolumeMonitor *monitor, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, monitor))
{
}

RemoteMounts::~RemoteMounts() = default;

QVariantList RemoteMounts::mounts() const
{
    return d->rows;
}

bool RemoteMounts::busy(const QString &id) const
{
    return d->operations.contains(id);
}

bool RemoteMounts::mount(const QString &id)
{
    if (busy(id)) return false;
    GVolume *volume = d->nativeVolumes.value(id);
    if (!volume || !g_volume_can_mount(volume) || d->nativeMounts.contains(id)) {
        Q_EMIT operationFailed(id, tr("This network location cannot be connected."));
        return false;
    }
    auto *operation = d->begin(id, Private::OperationKind::MountVolume);
    g_volume_mount(volume, G_MOUNT_MOUNT_NONE, operation->interaction, operation->cancellable, Private::operationCompleted, operation);
    d->refresh();
    Q_EMIT busyChanged();
    return true;
}

bool RemoteMounts::unmount(const QString &id, bool eject)
{
    if (busy(id)) return false;
    GMount *mount = d->nativeMounts.value(id);
    GVolume *volume = d->nativeVolumes.value(id);
    if (eject && volume && g_volume_can_eject(volume)) {
        auto *operation = d->begin(id, Private::OperationKind::EjectVolume);
        g_volume_eject_with_operation(volume, G_MOUNT_UNMOUNT_NONE, operation->interaction, operation->cancellable, Private::operationCompleted, operation);
    } else {
        if (!mount) {
            Q_EMIT operationFailed(id, tr("This network location is no longer connected."));
            return false;
        }
        if (eject ? !g_mount_can_eject(mount) : !g_mount_can_unmount(mount)) {
            Q_EMIT operationFailed(id, eject ? tr("This network location cannot be ejected.") : tr("This network location cannot be disconnected."));
            return false;
        }
        auto *operation = d->begin(id, eject ? Private::OperationKind::EjectMount : Private::OperationKind::UnmountMount);
        // Busy mounts may ask the user how to proceed; never force them silently.
        if (eject) g_mount_eject_with_operation(mount, G_MOUNT_UNMOUNT_NONE, operation->interaction, operation->cancellable, Private::operationCompleted, operation);
        else g_mount_unmount_with_operation(mount, G_MOUNT_UNMOUNT_NONE, operation->interaction, operation->cancellable, Private::operationCompleted, operation);
    }
    d->refresh();
    Q_EMIT busyChanged();
    return true;
}
