/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <qqmlregistration.h>

#include <memory>

class QDBusMessage;
class QDBusServiceWatcher;
class QMenu;

// Keep one instance alive for the applet's lifetime: Unity announcements can
// arrive before a task's context menu is opened.
class QuicklistBackend : public QObject, protected QDBusContext
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit QuicklistBackend(QObject *parent = nullptr);
    ~QuicklistBackend() override;

    Q_INVOKABLE QVariantList actions(const QUrl &launcherUrl) const;
    Q_INVOKABLE void aboutToShow(const QUrl &launcherUrl);
    Q_INVOKABLE void closed(const QUrl &launcherUrl);

Q_SIGNALS:
    void actionsChanged(const QString &storageId);

private Q_SLOTS:
    void update(const QString &uri, const QVariantMap &properties);
    void menuChanged(const QDBusMessage &message);
    void serviceUnregistered(const QString &service);

private:
    struct Entry;
    struct Layout;
    using EntryPtr = std::shared_ptr<Entry>;

    static QString storageId(const QUrl &url);
    EntryPtr entryForLauncher(const QUrl &launcherUrl) const;
    bool isCurrent(const EntryPtr &entry) const;
    void fetchLayout(const EntryPtr &entry);
    void prepareMenu(const EntryPtr &entry, int id);
    void sendEvent(const EntryPtr &entry, int id, const QString &event);
    void populateMenu(QMenu *menu, const Layout &layout, const EntryPtr &entry);
    static bool readLayout(const QVariant &value, Layout &layout, int depth, int &remaining);

    QDBusServiceWatcher *m_watcher;
    QHash<QString, EntryPtr> m_entries;
    QHash<QString, QString> m_mapping;
    quint64 m_sequence = 0;
};
