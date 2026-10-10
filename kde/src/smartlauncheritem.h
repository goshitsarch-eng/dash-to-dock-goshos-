/*
    SPDX-FileCopyrightText: 2016, 2019 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QUrl>
#include <QWeakPointer>
#include <qqmlregistration.h>

#include <algorithm>
#include <limits>

#include "smartlauncherbackend.h"

namespace SmartLauncher
{

// A visible, positive application count may replace ordinary notifications.
// Saturate additions so an external application's count cannot overflow a badge.
constexpr int combinedBadgeCount(int applicationCount, bool applicationVisible, int notificationCount, bool notificationsEnabled, bool applicationOverrides)
{
    const int remote = applicationVisible ? std::max(0, applicationCount) : 0;
    const int notifications = notificationsEnabled ? std::max(0, notificationCount) : 0;
    if (applicationOverrides && remote > 0) {
        return remote;
    }
    return remote + std::min(notifications, std::numeric_limits<int>::max() - remote);
}

class Item : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartLauncherItem)

    Q_PROPERTY(QUrl launcherUrl READ launcherUrl WRITE setLauncherUrl NOTIFY launcherUrlChanged)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(bool applicationCounterOverridesNotifications READ applicationCounterOverridesNotifications WRITE setApplicationCounterOverridesNotifications
                   NOTIFY applicationCounterOverridesNotificationsChanged)

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool countVisible READ countVisible NOTIFY countVisibleChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool progressVisible READ progressVisible NOTIFY progressVisibleChanged)
    Q_PROPERTY(bool urgent READ urgent NOTIFY urgentChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY updatingChanged)

public:
    explicit Item(QObject *parent = nullptr);
    ~Item() override = default;

    QUrl launcherUrl() const;
    void setLauncherUrl(const QUrl &launcherUrl);
    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);
    bool applicationCounterOverridesNotifications() const;
    void setApplicationCounterOverridesNotifications(bool overrides);

    int count() const;
    bool countVisible() const;
    int progress() const;
    bool progressVisible() const;
    bool urgent() const;
    bool updating() const;

Q_SIGNALS:
    void launcherUrlChanged(const QUrl &launcherUrl);
    void notificationsEnabledChanged(bool enabled);
    void applicationCounterOverridesNotificationsChanged(bool overrides);

    void countChanged(int count);
    void countVisibleChanged(bool countVisible);
    void progressChanged(int progress);
    void progressVisibleChanged(bool progressVisible);
    void urgentChanged(bool urgent);
    void updatingChanged(bool updating);

private:
    void init();

    void populate();
    void clear();

    void setCount(int count);
    void setCountVisible(bool countVisible);
    void setProgress(int progress);
    void setProgressVisible(bool progressVisible);
    void setUrgent(bool urgent);
    void setUpdating(bool updating);

    static std::weak_ptr<Backend> s_backend;

    std::shared_ptr<Backend> m_backendPtr;

    QUrl m_launcherUrl;
    QString m_storageId;
    QString m_notificationStorageId;

    bool m_inited = false;
    bool m_notificationsEnabled = true;
    bool m_applicationCounterOverridesNotifications = true;

    int m_count = 0;
    bool m_countVisible = false;
    int m_progress = 0;
    bool m_progressVisible = false;
    bool m_urgent = false;
    bool m_updating = false;
};

} // namespace SmartLauncher
