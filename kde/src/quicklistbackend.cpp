/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quicklistbackend.h"

#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QAction>
#include <QActionGroup>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QSet>

#include <chrono>

#include "log_settings.h"

namespace
{
const QString menuInterface = QStringLiteral("com.canonical.dbusmenu");

QString qtMnemonic(const QString &label)
{
    // DBusMenu uses GTK underscores for mnemonics; Qt uses ampersands.
    QString result;
    for (qsizetype i = 0; i < label.size(); ++i) {
        const QChar c = label.at(i);
        if (c == u'&') {
            result += QStringLiteral("&&");
        } else if (c == u'_') {
            if (i + 1 < label.size() && label.at(i + 1) == u'_') {
                result += u'_';
                ++i;
            } else {
                result += u'&';
            }
        } else {
            result += c;
        }
    }
    return result;
}
}

struct QuicklistBackend::Layout {
    int id = 0;
    QVariantMap properties;
    QList<Layout> children;
};

struct QuicklistBackend::Entry {
    QString key;
    QString storageId;
    QString service;
    QString path;
    quint64 sequence = 0;
    bool loading = false;
    bool dirty = false;
    std::unique_ptr<QMenu> menu;
};

QuicklistBackend::QuicklistBackend(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(this))
{
    auto bus = QDBusConnection::sessionBus();
    m_watcher->setConnection(bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &QuicklistBackend::serviceUnregistered);

    const bool updates = bus.connect({}, {}, QStringLiteral("com.canonical.Unity.LauncherEntry"), QStringLiteral("Update"),
                                     this, SLOT(update(QString,QVariantMap)));
    const bool layouts = bus.connect({}, {}, menuInterface, QStringLiteral("LayoutUpdated"), this, SLOT(menuChanged(QDBusMessage)));
    const bool properties = bus.connect({}, {}, menuInterface, QStringLiteral("ItemsPropertiesUpdated"), this, SLOT(menuChanged(QDBusMessage)));
    if (!updates || !layouts || !properties) {
        qCWarning(TASKMANAGER_DEBUG) << "Could not subscribe to Unity quicklist signals";
    }

    // libunity announces existing entries when the shell acquires this name.
    // Plasma's smart launcher may already own it; listening still works then.
    bus.registerService(QStringLiteral("com.canonical.Unity"));

    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("taskmanagerrulesrc")), QStringLiteral("Unity Launcher Mapping"));
    for (const QString &key : group.keyList()) {
        const QString value = group.readEntry(key, QString());
        if (!value.isEmpty()) {
            m_mapping.insert(key, value);
        }
    }
}

QuicklistBackend::~QuicklistBackend()
{
    // Closing an open QMenu can emit aboutToHide. Leave a valid empty index
    // while destroying the menus so those callbacks cannot access a hash that
    // is already being destroyed.
    auto entries = std::move(m_entries);
    entries.clear();
}

QString QuicklistBackend::storageId(const QUrl &url)
{
    if (url.isLocalFile()) {
        const auto service = KService::serviceByDesktopPath(url.toLocalFile());
        return service ? service->storageId() : QFileInfo(url.toLocalFile()).fileName();
    }
    QString name = url.toString();
    if (name.startsWith(QStringLiteral("application://"))) {
        name = QUrl::fromPercentEncoding(name.mid(14).toUtf8());
    } else if (url.scheme() == QStringLiteral("applications")) {
        name = url.path();
    }
    const auto service = KService::serviceByStorageId(name);
    return service ? service->storageId() : name;
}

QuicklistBackend::EntryPtr QuicklistBackend::entryForLauncher(const QUrl &url) const
{
    const QString id = storageId(url);
    const QString mappedId = m_mapping.value(id, id);
    EntryPtr latest;
    for (const auto &entry : m_entries) {
        if ((entry->storageId == id || entry->storageId == mappedId) && (!latest || latest->sequence < entry->sequence)) {
            latest = entry;
        }
    }
    return latest;
}

bool QuicklistBackend::isCurrent(const EntryPtr &entry) const
{
    return m_entries.value(entry->key) == entry;
}

QVariantList QuicklistBackend::actions(const QUrl &url) const
{
    QVariantList result;
    const auto entry = entryForLauncher(url);
    if (entry && entry->menu) {
        for (QAction *action : entry->menu->actions()) {
            result.append(QVariant::fromValue(action));
        }
    }
    return result;
}

void QuicklistBackend::aboutToShow(const QUrl &url)
{
    if (const auto entry = entryForLauncher(url)) {
        prepareMenu(entry, 0);
        sendEvent(entry, 0, QStringLiteral("opened"));
    }
}

void QuicklistBackend::closed(const QUrl &url)
{
    if (const auto entry = entryForLauncher(url)) {
        sendEvent(entry, 0, QStringLiteral("closed"));
    }
}

void QuicklistBackend::update(const QString &uri, const QVariantMap &properties)
{
    if (!calledFromDBus() || !properties.contains(QStringLiteral("quicklist"))) {
        return;
    }
    const QString id = storageId(QUrl(uri));
    const QString service = message().service();
    const QString key = id + u'\n' + service;
    const QVariant property = properties.value(QStringLiteral("quicklist"));
    const QString path = property.metaType() == QMetaType::fromType<QDBusObjectPath>()
        ? property.value<QDBusObjectPath>().path() : property.toString();

    if (path.isEmpty() || path == QStringLiteral("/")) {
        // Other instances of the application may still provide a quicklist.
        const auto removed = m_entries.take(key);
        if (removed) {
            Q_EMIT actionsChanged(id);
        }
        return;
    }
    if (!path.startsWith(u'/') || id.isEmpty()) {
        return;
    }
    auto entry = m_entries.value(key);
    if (!entry || entry->path != path) {
        auto replacement = std::make_shared<Entry>();
        replacement->key = key;
        replacement->storageId = id;
        replacement->service = service;
        replacement->path = path;
        m_entries.insert(key, replacement);
        // Remove existing QML menu wrappers before the old actions are freed.
        Q_EMIT actionsChanged(id);
        entry = replacement;
    }
    entry->sequence = ++m_sequence;
    m_watcher->addWatchedService(service);
    fetchLayout(entry);
}

void QuicklistBackend::menuChanged(const QDBusMessage &message)
{
    for (const auto &entry : m_entries) {
        if (entry->service == message.service() && entry->path == message.path()) {
            fetchLayout(entry);
        }
    }
}

void QuicklistBackend::serviceUnregistered(const QString &service)
{
    QSet<QString> changed;
    QList<EntryPtr> removed;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if ((*it)->service == service) {
            changed.insert((*it)->storageId);
            removed.append(*it);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    m_watcher->removeWatchedService(service);
    for (const QString &id : changed) {
        Q_EMIT actionsChanged(id);
    }
}

bool QuicklistBackend::readLayout(const QVariant &value, Layout &layout, int depth, int &remaining)
{
    if (depth > 64 || --remaining < 0 || value.metaType() != QMetaType::fromType<QDBusArgument>()) {
        return false;
    }
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != QStringLiteral("(ia{sv}av)")) {
        return false;
    }
    argument.beginStructure();
    argument >> layout.id >> layout.properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        Layout childLayout;
        if (!readLayout(child.variant(), childLayout, depth + 1, remaining)) {
            return false;
        }
        layout.children.append(std::move(childLayout));
    }
    argument.endArray();
    argument.endStructure();
    return true;
}

void QuicklistBackend::fetchLayout(const EntryPtr &entry)
{
    if (entry->loading) {
        entry->dirty = true;
        return;
    }
    entry->loading = true;
    auto call = QDBusMessage::createMethodCall(entry->service, entry->path, menuInterface, QStringLiteral("GetLayout"));
    call.setArguments({0, -1, QStringList()});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, entry](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        entry->loading = false;
        if (!isCurrent(entry)) {
            return;
        }
        Layout layout;
        int remaining = 4096;
        const bool valid = reply.type() != QDBusMessage::ErrorMessage && reply.arguments().size() == 2
            && readLayout(reply.arguments().at(1), layout, 0, remaining);
        auto oldMenu = std::move(entry->menu);
        if (valid) {
            entry->menu = std::make_unique<QMenu>();
            populateMenu(entry->menu.get(), layout, entry);
        } else {
            qCWarning(TASKMANAGER_DEBUG) << "Quicklist GetLayout failed:" << reply.errorName();
        }
        Q_EMIT actionsChanged(entry->storageId);
        if (entry->dirty) {
            entry->dirty = false;
            fetchLayout(entry);
        }
    });
}

void QuicklistBackend::prepareMenu(const EntryPtr &entry, int id)
{
    if (!isCurrent(entry)) {
        return;
    }
    auto call = QDBusMessage::createMethodCall(entry->service, entry->path, menuInterface, QStringLiteral("AboutToShow"));
    call.setArguments({id});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, entry](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (!isCurrent(entry)) {
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCDebug(TASKMANAGER_DEBUG) << "Quicklist AboutToShow unavailable:" << reply.errorName();
        }
        if (!entry->menu || (!reply.arguments().isEmpty() && reply.arguments().first().toBool())) {
            fetchLayout(entry);
        }
    });
}

void QuicklistBackend::sendEvent(const EntryPtr &entry, int id, const QString &event)
{
    if (!isCurrent(entry)) {
        return;
    }
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto call = QDBusMessage::createMethodCall(entry->service, entry->path, menuInterface, QStringLiteral("Event"));
    call.setArguments({id, event, QVariant::fromValue(QDBusVariant(0)), quint32(milliseconds)});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCDebug(TASKMANAGER_DEBUG) << "Quicklist event failed:" << reply.errorName();
        }
        watcher->deleteLater();
    });
}

void QuicklistBackend::populateMenu(QMenu *menu, const Layout &layout, const EntryPtr &entry)
{
    const std::weak_ptr<Entry> weakEntry(entry);
    QActionGroup *radioGroup = nullptr;
    for (const Layout &child : layout.children) {
        const auto &p = child.properties;
        QAction *action;
        if (!child.children.isEmpty() || p.value(QStringLiteral("children-display")).toString() == QStringLiteral("submenu")) {
            auto *submenu = new QMenu(menu);
            action = submenu->menuAction();
            populateMenu(submenu, child, entry);
            connect(submenu, &QMenu::aboutToShow, this, [this, weakEntry, id = child.id] {
                if (const auto entry = weakEntry.lock()) {
                    prepareMenu(entry, id);
                    sendEvent(entry, id, QStringLiteral("opened"));
                }
            });
            connect(submenu, &QMenu::aboutToHide, this, [this, weakEntry, id = child.id] {
                if (const auto entry = weakEntry.lock()) {
                    sendEvent(entry, id, QStringLiteral("closed"));
                }
            });
        } else {
            action = new QAction(menu);
        }
        menu->addAction(action);
        action->setText(qtMnemonic(p.value(QStringLiteral("label")).toString()));
        action->setEnabled(p.value(QStringLiteral("enabled"), true).toBool());
        action->setVisible(p.value(QStringLiteral("visible"), true).toBool());
        action->setSeparator(p.value(QStringLiteral("type")).toString() == QStringLiteral("separator"));
        action->setToolTip(p.value(QStringLiteral("accessible-desc")).toString());
        const QString toggle = p.value(QStringLiteral("toggle-type")).toString();
        action->setCheckable(toggle == QStringLiteral("checkmark") || toggle == QStringLiteral("radio"));
        action->setChecked(p.value(QStringLiteral("toggle-state"), 0).toInt() == 1);
        if (toggle == QStringLiteral("radio")) {
            if (!radioGroup) {
                radioGroup = new QActionGroup(menu);
                radioGroup->setExclusive(true);
            }
            radioGroup->addAction(action);
        } else {
            radioGroup = nullptr;
        }
        const QString iconName = p.value(QStringLiteral("icon-name")).toString();
        if (!iconName.isEmpty()) {
            action->setIcon(QIcon::fromTheme(iconName));
        } else if (const QByteArray data = qdbus_cast<QByteArray>(p.value(QStringLiteral("icon-data"))); !data.isEmpty()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(data)) {
                action->setIcon(QIcon(pixmap));
            }
        }
        connect(action, &QAction::triggered, this, [this, weakEntry, id = child.id] {
            if (const auto entry = weakEntry.lock()) {
                sendEvent(entry, id, QStringLiteral("clicked"));
            }
        });
        connect(action, &QAction::hovered, this, [this, weakEntry, id = child.id] {
            if (const auto entry = weakEntry.lock()) {
                sendEvent(entry, id, QStringLiteral("hovered"));
            }
        });
    }
}

#include "moc_quicklistbackend.cpp"
