/*
    SPDX-FileCopyrightText: 2016, 2019 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "smartlauncheritem.h"

#include <KDesktopFile>
#include <KService>

#include "log_settings.h"

using namespace SmartLauncher;

Item::Item(QObject *parent)
    : QObject(parent)
{
    m_backendPtr = s_backend.lock();
    if (!m_backendPtr) {
        auto ptr = std::make_shared<Backend>();
        s_backend = ptr;
        m_backendPtr = s_backend.lock();
    }
}

std::weak_ptr<Backend> Item::s_backend;

void Item::init()
{
    if (m_inited || m_storageId.isEmpty() || !m_backendPtr) {
        return;
    }

    connect(m_backendPtr.get(), &Backend::reloadRequested, this, [this](const QString &uri) {
        if (uri.isEmpty() || m_storageId == uri || m_notificationStorageId == uri) {
            populate();
        }
    });

    connect(m_backendPtr.get(), &Backend::launcherRemoved, this, [this](const QString &uri) {
        if (uri.isEmpty() || m_storageId == uri) {
            populate();
        }
    });

    connect(m_backendPtr.get(), &Backend::countChanged, this, [this](const QString &uri, int) {
        if (uri.isEmpty() || m_storageId == uri) {
            populate();
        }
    });

    connect(m_backendPtr.get(), &Backend::countVisibleChanged, this, [this](const QString &uri, bool) {
        if (uri.isEmpty() || m_storageId == uri) {
            populate();
        }
    });

    connect(m_backendPtr.get(), &Backend::progressChanged, this, [this](const QString &uri, int progress) {
        if (uri.isEmpty() || m_storageId == uri) {
            setProgress(progress);
        }
    });

    connect(m_backendPtr.get(), &Backend::progressVisibleChanged, this, [this](const QString &uri, bool progressVisible) {
        if (uri.isEmpty() || m_storageId == uri) {
            setProgressVisible(progressVisible);
        }
    });

    connect(m_backendPtr.get(), &Backend::urgentChanged, this, [this](const QString &uri, bool urgent) {
        if (uri.isEmpty() || m_storageId == uri) {
            setUrgent(urgent);
        }
    });

    connect(m_backendPtr.get(), &Backend::updatingChanged, this, [this](const QString &uri, bool updating) {
        if (uri.isEmpty() || m_storageId == uri) {
            setUpdating(updating);
        }
    });

    m_inited = true;
}

void Item::populate()
{
    if (!m_backendPtr || m_storageId.isEmpty()) {
        return;
    }

    const int badgeCount = m_backendPtr->badgesAllowed(m_notificationStorageId)
        ? combinedBadgeCount(m_backendPtr->count(m_storageId),
                             m_backendPtr->countVisible(m_storageId),
                             m_backendPtr->notificationCount(m_notificationStorageId),
                             m_notificationsEnabled,
                             m_applicationCounterOverridesNotifications)
        : 0;
    setCount(badgeCount);
    setCountVisible(badgeCount > 0);
    setProgress(m_backendPtr->progress(m_storageId));
    setProgressVisible(m_backendPtr->progressVisible(m_storageId));
    setUrgent(m_backendPtr->urgent(m_storageId));
    setUpdating(m_backendPtr->updating(m_storageId));
}

void Item::clear()
{
    setCount(0);
    setCountVisible(false);
    setProgress(0);
    setProgressVisible(false);
    setUrgent(false);
    setUpdating(false);
}

QUrl Item::launcherUrl() const
{
    return m_launcherUrl;
}

void Item::setLauncherUrl(const QUrl &launcherUrl)
{
    if (launcherUrl != m_launcherUrl) {
        m_launcherUrl = launcherUrl;
        Q_EMIT launcherUrlChanged(launcherUrl);

        m_storageId.clear();
        m_notificationStorageId.clear();
        clear();

        if (launcherUrl.scheme() == QLatin1String("applications")) {
            const KService::Ptr service = KService::serviceByMenuId(launcherUrl.path());

            if (service && launcherUrl.path() == service->menuId()) {
                m_storageId = service->menuId();
            }
        }

        if (launcherUrl.isLocalFile() && KDesktopFile::isDesktopFile(launcherUrl.toLocalFile())) {
            KDesktopFile f(launcherUrl.toLocalFile());

            const KService::Ptr service = KService::serviceByStorageId(f.fileName());
            if (service) {
                m_storageId = service->storageId();
            }
        }

        if (m_storageId.isEmpty()) {
            return;
        }

        // Unity mapping rules rename only the Unity endpoint. Notifications
        // continue to advertise the real desktop entry of the application.
        m_notificationStorageId = m_storageId;

        if (m_backendPtr) {
            // check if we have a mapping to a different desktop file
            const QString &overrideStorageId = m_backendPtr->unityMappingRules().value(m_storageId);
            if (!overrideStorageId.isEmpty()) {
                m_storageId = overrideStorageId;
            }
        }

        init();
        populate();
    }
}

bool Item::notificationsEnabled() const
{
    return m_notificationsEnabled;
}

void Item::setNotificationsEnabled(bool enabled)
{
    if (m_notificationsEnabled != enabled) {
        m_notificationsEnabled = enabled;
        Q_EMIT notificationsEnabledChanged(enabled);
        populate();
    }
}

bool Item::applicationCounterOverridesNotifications() const
{
    return m_applicationCounterOverridesNotifications;
}

void Item::setApplicationCounterOverridesNotifications(bool overrides)
{
    if (m_applicationCounterOverridesNotifications != overrides) {
        m_applicationCounterOverridesNotifications = overrides;
        Q_EMIT applicationCounterOverridesNotificationsChanged(overrides);
        populate();
    }
}

int Item::count() const
{
    return m_count;
}

void Item::setCount(int count)
{
    if (m_count != count) {
        m_count = count;
        Q_EMIT countChanged(count);
    }
}

bool Item::countVisible() const
{
    return m_countVisible;
}

void Item::setCountVisible(bool countVisible)
{
    if (m_countVisible != countVisible) {
        m_countVisible = countVisible;
        Q_EMIT countVisibleChanged(countVisible);
    }
}

int Item::progress() const
{
    return m_progress;
}

void Item::setProgress(int progress)
{
    int boundedProgress = std::clamp(progress, 0, 100);

    if (progress != boundedProgress) {
        qCDebug(TASKMANAGER_DEBUG).nospace() << qUtf8Printable(m_launcherUrl.toString()) << ": Progress value " << progress << " is out of bounds!";
    }

    if (m_progress != boundedProgress) {
        m_progress = boundedProgress;
        Q_EMIT progressChanged(boundedProgress);
    }
}

bool Item::progressVisible() const
{
    return m_progressVisible;
}

void Item::setProgressVisible(bool progressVisible)
{
    if (m_progressVisible != progressVisible) {
        m_progressVisible = progressVisible;
        Q_EMIT progressVisibleChanged(progressVisible);
    }
}

bool Item::urgent() const
{
    return m_urgent;
}

void Item::setUrgent(bool urgent)
{
    if (m_urgent != urgent) {
        m_urgent = urgent;
        Q_EMIT urgentChanged(urgent);
    }
}

bool Item::updating() const
{
    return m_updating;
}

void Item::setUpdating(bool updating)
{
    if (m_updating != updating) {
        m_updating = updating;
        Q_EMIT updatingChanged(updating);
    }
}

#include "moc_smartlauncheritem.cpp"
