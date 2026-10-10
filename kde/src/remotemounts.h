/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QObject>
#include <QVariantList>
#include <memory>

typedef struct _GVolumeMonitor GVolumeMonitor;

// GIO discovers connected network shares even when they have no KDE Places bookmark.
class RemoteMounts : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList mounts READ mounts NOTIFY mountsChanged)

public:
    explicit RemoteMounts(QObject *parent = nullptr);
    // Retains the supplied monitor; useful for a provider and for isolated tests.
    RemoteMounts(GVolumeMonitor *monitor, QObject *parent);
    ~RemoteMounts() override;

    QVariantList mounts() const;
    bool busy(const QString &id) const;
    Q_INVOKABLE bool mount(const QString &id);
    Q_INVOKABLE bool unmount(const QString &id, bool eject = false);

Q_SIGNALS:
    void mountsChanged();
    void busyChanged();
    void operationFailed(const QString &id, const QString &message);
    void operationFinished(const QString &id);

private:
    class Private;
    std::unique_ptr<Private> d;
};
