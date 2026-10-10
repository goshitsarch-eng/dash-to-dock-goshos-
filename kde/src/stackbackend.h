/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>

class QFileSystemWatcher;
class KFilePlacesModel;
class KJob;
class RemoteMounts;
namespace KIO
{
class ListJob;
}
namespace KActivities::Stats
{
class ResultModel;
}

/** Native Plasma services shared by the folder, application, device and trash stacks. */
class StackBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList applications READ applications NOTIFY applicationsChanged)
    Q_PROPERTY(QVariantList recentApplications READ recentApplications NOTIFY recentApplicationsChanged)
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    Q_PROPERTY(QVariantMap standardLocations READ standardLocations CONSTANT)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int totalItems READ totalItems NOTIFY entriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool trashEmpty READ trashEmpty NOTIFY trashEmptyChanged)

public:
    explicit StackBackend(QObject *parent = nullptr);
    // An injected provider remains owned by its caller and must outlive this object.
    StackBackend(RemoteMounts *remoteMounts, QObject *parent);
    ~StackBackend() override;

    QVariantList applications() const;
    QVariantList recentApplications() const;
    QVariantList places() const;
    QVariantMap standardLocations() const;
    QVariantList entries() const;
    int totalItems() const;
    bool busy() const;
    QString error() const;
    bool trashEmpty() const;

    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void openNewWindow(const QUrl &url);
    Q_INVOKABLE void launchApplication(const QString &desktopId);
    Q_INVOKABLE void listFolder(const QUrl &url);
    Q_INVOKABLE void cancelListing();
    Q_INVOKABLE void refresh();
    // Mount when needed, then open the device. Model indexes come from places[].index.
    Q_INVOKABLE void setupPlace(int index, const QString &expectedId = {});
    Q_INVOKABLE void teardownPlace(int index, const QString &expectedId = {});
    Q_INVOKABLE void ejectPlace(int index, const QString &expectedId = {});
    // The caller presents confirmation before invoking this destructive operation.
    Q_INVOKABLE void emptyTrash();

Q_SIGNALS:
    void applicationsChanged();
    void recentApplicationsChanged();
    void placesChanged();
    void entriesChanged();
    void busyChanged();
    void errorChanged();
    void trashEmptyChanged();
    void errorOccurred(const QString &message);

private:
    void refreshApplications();
    void refreshRecentApplications();
    void refreshPlaces();
    void refreshTrash();
    void setBusy(bool busy);
    void setError(const QString &error);
    void watchJob(KJob *job);
    QString placeIdentity(const QModelIndex &index) const;
    QModelIndex placeIndex(int row, const QString &expectedId);
    QVariantMap placeEntry(int row, const QString &expectedId);

    KFilePlacesModel *m_placesModel = nullptr;
    RemoteMounts *m_remoteMounts = nullptr;
    KActivities::Stats::ResultModel *m_recentModel = nullptr;
    QFileSystemWatcher *m_trashWatcher = nullptr;
    QFileSystemWatcher *m_folderWatcher = nullptr;
    QList<QPersistentModelIndex> m_pendingMounts;
    QSet<QString> m_pendingRemoteMounts;
    QPointer<KIO::ListJob> m_listJob;
    QUrl m_folderUrl;
    QVariantList m_applications;
    QVariantList m_recentApplications;
    QVariantList m_places;
    QVariantList m_entries;
    bool m_busy = false;
    bool m_trashEmpty = true;
    QString m_error;
};
