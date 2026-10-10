/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractItemModel>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>
#include <memory>

class QTimer;

/** Associates Dolphin's published locations with native Plasma task indexes. */
class LocationBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QAbstractItemModel *tasksModel READ tasksModel WRITE setTasksModel NOTIFY tasksModelChanged)
    Q_PROPERTY(QStringList locations READ locations WRITE setLocations NOTIFY locationsChanged)
    Q_PROPERTY(QVariantMap matches READ matches NOTIFY matchesChanged)
    Q_PROPERTY(QVariantList excludedWindowIds READ excludedWindowIds NOTIFY excludedWindowIdsChanged)
    Q_PROPERTY(QString capability READ capability NOTIFY capabilityChanged)

public:
    explicit LocationBackend(QObject *parent = nullptr);
    ~LocationBackend() override;

    bool enabled() const;
    void setEnabled(bool enabled);
    QAbstractItemModel *tasksModel() const;
    void setTasksModel(QAbstractItemModel *model);
    QStringList locations() const;
    void setLocations(const QStringList &locations);
    QVariantMap matches() const;
    QVariantList excludedWindowIds() const;
    QString capability() const;

    Q_INVOKABLE QVariantList windowsForUrl(const QUrl &url) const;
    Q_INVOKABLE bool activate(const QUrl &url);
    Q_INVOKABLE bool activateWindow(const QUrl &url, const QVariant &windowId);
    Q_INVOKABLE bool minimize(const QUrl &url, bool allWindows = true);
    Q_INVOKABLE bool cycle(const QUrl &url, int direction = 1);
    Q_INVOKABLE bool close(const QUrl &url);
    Q_INVOKABLE bool closeWindow(const QUrl &url, const QVariant &windowId);
    Q_INVOKABLE void refresh();

    static QString normalizedUrl(const QUrl &url);
    static bool containsLocation(const QUrl &root, const QUrl &location);

Q_SIGNALS:
    void enabledChanged();
    void tasksModelChanged();
    void locationsChanged();
    void matchesChanged();
    void excludedWindowIdsChanged();
    void capabilityChanged();

private Q_SLOTS:
    void serviceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void servicePropertiesChanged(const QString &interface, const QVariantMap &changes, const QStringList &invalidated, const QDBusMessage &message);

private:
    struct ServiceLocations {
        QString service;
        QString owner;
        uint pid = 0;
        bool complete = false;
        QMap<QString, QStringList> windows;
        QStringList exactLocations;
    };
    struct Snapshot;

    void queryService(const QString &service, const std::shared_ptr<Snapshot> &snapshot);
    void queryLocations(const ServiceLocations &service, const std::shared_ptr<Snapshot> &snapshot);
    void finishService(const ServiceLocations &service, const std::shared_ptr<Snapshot> &snapshot);
    void rebuildMatches();
    void scheduleRefresh();
    void setCapability(const QString &capability);
    QVariant role(const QModelIndex &index, const QByteArray &name) const;
    QList<QPersistentModelIndex> matchingIndexes(const QUrl &url) const;
    QPersistentModelIndex preferredIndex(const QList<QPersistentModelIndex> &indexes, bool preferUrgent) const;
    bool request(const QByteArray &method, const QPersistentModelIndex &index);

    QPointer<QAbstractItemModel> m_tasksModel;
    QHash<QByteArray, int> m_roles;
    QList<QMetaObject::Connection> m_modelConnections;
    QList<ServiceLocations> m_services;
    QHash<QString, QList<QPersistentModelIndex>> m_matchingIndexes;
    QStringList m_locations;
    QVariantMap m_matches;
    QVariantList m_excludedWindowIds;
    QString m_capability = QStringLiteral("disabled");
    QTimer *m_pollTimer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    quint64 m_generation = 0;
    bool m_enabled = false;
};
