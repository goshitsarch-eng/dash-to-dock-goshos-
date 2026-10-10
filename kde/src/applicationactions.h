/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KService>
#include <QDBusConnection>
#include <QObject>
#include <QProcessEnvironment>
#include <QVariantList>

class QAction;
namespace KIO { class CommandLauncherJob; }

class ApplicationActions : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationActions(QObject *parent = nullptr);
    explicit ApplicationActions(const QDBusConnection &gpuBus, QObject *parent = nullptr);

    QVariantList gpuActions(const QUrl &launcherUrl, QObject *parent);
    QAction *detailsAction(const QUrl &launcherUrl, QObject *parent);
    KIO::CommandLauncherJob *gpuLaunchJob(const KService::Ptr &service, int gpuIndex, QObject *parent = nullptr) const;

    static QProcessEnvironment gpuEnvironment(const QList<QVariantMap> &gpus, int gpuIndex, const QProcessEnvironment &base);
    static QUrl detailsUrl(const KService::Ptr &service, const QStringList &dataDirectories);

Q_SIGNALS:
    void gpuChoicesChanged();

private Q_SLOTS:
    void refreshGpus();
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    static KService::Ptr serviceForUrl(const QUrl &url);
    QDBusConnection m_gpuBus;
    QList<QVariantMap> m_gpus;
    quint64 m_generation = 0;
};
