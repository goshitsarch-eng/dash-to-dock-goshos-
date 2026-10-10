/*
    SPDX-FileCopyrightText: 2016, 2019 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "smartlauncherbackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QSet>
#include <QTimer>

#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <algorithm>
#include <cmath>
#include <limits>

#include "log_settings.h"
#include <settings.h>
#include <notifications.h>

using namespace SmartLauncher;
using namespace NotificationManager;

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(this))
    , m_jobsModel(nullptr)
    , m_settings(new Settings(this))
    , m_notificationsModel(new Notifications(this))
    , m_inhibitionTimer(new QTimer(this))
{
    m_watcher->setConnection(QDBusConnection::sessionBus());
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Backend::onServiceUnregistered);

    setupUnity();
    setupNotifications();

    m_inhibitionTimer->setSingleShot(true);
    connect(m_inhibitionTimer, &QTimer::timeout, this, &Backend::reload);

    reload();
    connect(m_settings, &Settings::settingsChanged, this, &Backend::reload);
    connect(m_settings, &Settings::notificationsInhibitedByApplicationChanged, this, &Backend::reload);
}

Backend::~Backend() = default;

void Backend::reload()
{
    m_badgeBlacklist = m_settings->badgeBlacklistedApplications();

    // Unity Launcher API operates on storage IDs ("foo.desktop"), whereas settings return desktop entries "foo"
    std::transform(m_badgeBlacklist.begin(), m_badgeBlacklist.end(), m_badgeBlacklist.begin(), [](const QString &desktopEntry) -> QString {
        return desktopEntry.endsWith(QLatin1String(".desktop")) ? desktopEntry : desktopEntry + QStringLiteral(".desktop");
    });

    if (!m_jobsModel) {
        m_jobsModel = JobsModel::createJobsModel();
        m_jobsModel->init();
    }

    // A timed inhibition can end without a settings write or a launcher update.
    m_inhibitionTimer->stop();
    const qint64 inhibitedFor = QDateTime::currentDateTimeUtc().msecsTo(m_settings->notificationsInhibitedUntil());
    if (inhibitedFor > 0) {
        m_inhibitionTimer->start(static_cast<int>(std::min(inhibitedFor, qint64(std::numeric_limits<int>::max()))));
    }

    Q_EMIT reloadRequested(QString() /*all*/);
}

bool Backend::doNotDisturbMode() const
{
    return m_settings->notificationsInhibitedByApplication()
        || (m_settings->notificationsInhibitedUntil().isValid() && m_settings->notificationsInhibitedUntil() > QDateTime::currentDateTimeUtc());
}

bool Backend::badgesAllowed(const QString &storageId) const
{
    return m_settings->badgesInTaskManager() && !doNotDisturbMode() && !m_badgeBlacklist.contains(storageId);
}

void Backend::setupNotifications()
{
    m_notificationsModel->setShowNotifications(true);
    m_notificationsModel->setShowJobs(false);
    m_notificationsModel->setShowExpired(true);
    m_notificationsModel->setShowDismissed(true);
    m_notificationsModel->setGroupMode(Notifications::GroupDisabled);
    m_notificationsModel->setLimit(0);

    connect(m_notificationsModel, &QAbstractItemModel::rowsInserted, this, &Backend::updateNotificationCounts);
    connect(m_notificationsModel, &QAbstractItemModel::rowsRemoved, this, &Backend::updateNotificationCounts);
    connect(m_notificationsModel, &QAbstractItemModel::modelReset, this, &Backend::updateNotificationCounts);
    connect(m_notificationsModel, &QAbstractItemModel::dataChanged, this, &Backend::updateNotificationCounts);
    updateNotificationCounts();
}

void Backend::updateNotificationCounts()
{
    QHash<QString, int> counts;
    for (int row = 0; row < m_notificationsModel->rowCount(); ++row) {
        const QModelIndex index = m_notificationsModel->index(row, 0);
        if (index.data(Notifications::TypeRole).toInt() != Notifications::NotificationType) {
            continue;
        }
        // Match the original notification tray: retained ordinary notifications
        // count until removed; acknowledged resident notifications do not.
        if (index.data(Notifications::ResidentRole).toBool() && index.data(Notifications::ReadRole).toBool()) {
            continue;
        }
        QString storageId = index.data(Notifications::DesktopEntryRole).toString();
        if (storageId.isEmpty()) {
            continue;
        }
        if (!storageId.endsWith(QLatin1String(".desktop"))) {
            storageId += QStringLiteral(".desktop");
        }
        ++counts[storageId];
    }

    QSet<QString> changed;
    for (auto it = m_notificationCounts.cbegin(); it != m_notificationCounts.cend(); ++it) {
        if (counts.value(it.key()) != it.value()) {
            changed.insert(it.key());
        }
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (m_notificationCounts.value(it.key()) != it.value()) {
            changed.insert(it.key());
        }
    }
    m_notificationCounts = std::move(counts);
    for (const QString &storageId : std::as_const(changed)) {
        Q_EMIT reloadRequested(storageId);
    }
}

void Backend::setupUnity()
{
    auto sessionBus = QDBusConnection::sessionBus();

    if (!sessionBus.connect({},
                            {},
                            QStringLiteral("com.canonical.Unity.LauncherEntry"),
                            QStringLiteral("Update"),
                            this,
                            SLOT(update(QString, QMap<QString, QVariant>)))) {
        qCWarning(TASKMANAGER_DEBUG) << "failed to register Update signal";
    }

    // The stock Plasma task manager may already export /Unity in this process.
    // LauncherEntry updates are broadcast signals, so both observers can listen.
    if (!sessionBus.objectRegisteredAt(QStringLiteral("/Unity")) && !sessionBus.registerObject(QStringLiteral("/Unity"), this)) {
        qCWarning(TASKMANAGER_DEBUG) << "Failed to register unity object";
    }

    const QString unityService = QStringLiteral("com.canonical.Unity");
    if (sessionBus.interface() && !sessionBus.interface()->isServiceRegistered(unityService).value() && !sessionBus.registerService(unityService)) {
        qCWarning(TASKMANAGER_DEBUG) << "Failed to register unity service";
        // In case an external process uses this (e.g. Latte Dock), let it just listen.
    }

    KConfigGroup grp(KSharedConfig::openConfig(QStringLiteral("taskmanagerrulesrc")), QStringLiteral("Unity Launcher Mapping"));

    const QStringList keys = grp.keyList();
    for (const QString &key : keys) {
        const QString &value = grp.readEntry(key, QString());
        if (value.isEmpty()) {
            continue;
        }

        m_unityMappingRules.insert(key, value);
    }
}

bool Backend::hasLauncher(const QString &storageId) const
{
    return m_launchers.contains(storageId);
}

int Backend::count(const QString &uri) const
{
    if (!badgesAllowed(uri)) {
        return 0;
    }
    return m_launchers.value(uri).count;
}

bool Backend::countVisible(const QString &uri) const
{
    if (!badgesAllowed(uri)) {
        return false;
    }
    return m_launchers.value(uri).countVisible;
}

int Backend::notificationCount(const QString &storageId) const
{
    return badgesAllowed(storageId) ? m_notificationCounts.value(storageId) : 0;
}

int Backend::progress(const QString &uri) const
{
    return doNotDisturbMode() ? 0 : m_launchers.value(uri).progress;
}

bool Backend::progressVisible(const QString &uri) const
{
    return !doNotDisturbMode() && m_launchers.value(uri).progressVisible;
}

bool Backend::urgent(const QString &uri) const
{
    return !doNotDisturbMode() && m_launchers.value(uri).urgent;
}

bool Backend::updating(const QString &uri) const
{
    return m_launchers.value(uri).updating;
}

QHash<QString, QString> Backend::unityMappingRules() const
{
    return m_unityMappingRules;
}

void Backend::update(const QString &uri, const QMap<QString, QVariant> &properties)
{
    Q_ASSERT(calledFromDBus());

    QString storageId;

    auto foundStorageId = m_launcherUrlToStorageId.constFind(uri);
    if (foundStorageId == m_launcherUrlToStorageId.constEnd()) { // we don't know this one, register
        // According to Unity Launcher API documentation applications *should* send along their
        // desktop file name with application:// prefix
        const QString applicationSchemePrefix = QStringLiteral("application://");

        QString normalizedUri = uri;
        if (normalizedUri.startsWith(applicationSchemePrefix)) {
            normalizedUri = uri.mid(applicationSchemePrefix.length());
        }

        KService::Ptr service = KService::serviceByStorageId(normalizedUri);
        if (!service) {
            qCWarning(TASKMANAGER_DEBUG) << "Failed to find service for Unity Launcher" << uri;
            return;
        }

        storageId = service->storageId();
        m_launcherUrlToStorageId.insert(uri, storageId);

        m_dbusServiceToLauncherUrl.insert(message().service(), uri);
        m_watcher->addWatchedService(message().service());
    } else {
        storageId = *foundStorageId;
    }

    auto foundEntry = m_launchers.find(storageId);
    if (foundEntry == m_launchers.end()) { // we don't have it yet, create a new Entry
        Entry entry;
        foundEntry = m_launchers.insert(storageId, entry);
    }

    auto propertiesEnd = properties.constEnd();

    auto foundCount = properties.constFind(QStringLiteral("count"));
    if (foundCount != propertiesEnd) {
        const int oldSanitizedCount = count(storageId);
        bool valid = false;
        const qint64 newCount = foundCount->toLongLong(&valid);
        if (valid) {
            foundEntry->count = static_cast<int>(std::clamp(newCount, qint64(0), qint64(std::numeric_limits<int>::max())));
        }
        const int newSanitizedCount = count(storageId);
        if (newSanitizedCount != oldSanitizedCount) {
            Q_EMIT countChanged(storageId, newSanitizedCount);
        }
    }

    updateLauncherProperty(storageId,
                           properties,
                           QStringLiteral("count-visible"),
                           &foundEntry->countVisible,
                           &Backend::countVisible,
                           &Backend::countVisibleChanged);

    // the API gives us progress as 0..1 double but we'll use percent to avoid unnecessary
    // changes when it just changed a fraction of a percent, hence not using our fancy updateLauncherProperty method
    auto foundProgress = properties.constFind(QStringLiteral("progress"));
    if (foundProgress != propertiesEnd) {
        const int oldSanitizedProgress = progress(storageId);

        double progressValue = foundProgress->toDouble();
        if (!std::isfinite(progressValue)) {
            progressValue = 0.0; // Treat NaN/Inf as zero
        }
        foundEntry->progress = qRound(progressValue * 100);

        const int newSanitizedProgress = progress(storageId);

        if (oldSanitizedProgress != newSanitizedProgress) {
            Q_EMIT progressChanged(storageId, newSanitizedProgress);
        }
    }

    updateLauncherProperty(storageId,
                           properties,
                           QStringLiteral("progress-visible"),
                           &foundEntry->progressVisible,
                           &Backend::progressVisible,
                           &Backend::progressVisibleChanged);
    updateLauncherProperty(storageId, properties, QStringLiteral("urgent"), &foundEntry->urgent, &Backend::urgent, &Backend::urgentChanged);
    updateLauncherProperty(storageId, properties, QStringLiteral("updating"), &foundEntry->updating, &Backend::updating, &Backend::updatingChanged);
}

void Backend::onServiceUnregistered(const QString &service)
{
    const QString &launcherUrl = m_dbusServiceToLauncherUrl.take(service);
    if (launcherUrl.isEmpty()) {
        return;
    }

    const QString &storageId = m_launcherUrlToStorageId.take(launcherUrl);
    if (storageId.isEmpty()) {
        return;
    }

    m_launchers.remove(storageId);
    Q_EMIT launcherRemoved(storageId);
}

#include "moc_smartlauncherbackend.cpp"
