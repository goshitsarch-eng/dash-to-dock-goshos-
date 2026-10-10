/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "locationbackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace
{
using WindowLocations = QMap<QString, QStringList>;
constexpr auto fileManagerPath = "/org/freedesktop/FileManager1";
constexpr auto fileManagerInterface = "org.freedesktop.FileManager1";
constexpr auto dolphinPath = "/dolphin/Dolphin_1";
constexpr auto dolphinInterface = "org.kde.dolphin.MainWindow";

QDBusPendingCallWatcher *call(QObject *parent, const QString &service, const QString &path, const QString &interface,
                              const QString &method, const QVariantList &arguments = {})
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    // Never launch a file manager merely to discover existing windows.
    message.setAutoStartService(false);
    return new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 2000), parent);
}

bool isDolphin(const QString &appId)
{
    return appId == QLatin1String("org.kde.dolphin") || appId == QLatin1String("org.kde.dolphin.desktop") || appId == QLatin1String("dolphin");
}
}

struct LocationBackend::Snapshot {
    quint64 generation = 0;
    int remaining = 0;
    QList<ServiceLocations> services;
};

LocationBackend::LocationBackend(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_refreshTimer(new QTimer(this))
{
    qDBusRegisterMetaType<WindowLocations>();
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &LocationBackend::refresh);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(40);
    connect(m_refreshTimer, &QTimer::timeout, this, &LocationBackend::refresh);
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                                        SLOT(serviceOwnerChanged(QString,QString,QString)));
    QDBusConnection::sessionBus().connect(QString(), QString::fromLatin1(fileManagerPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                                        QStringLiteral("PropertiesChanged"), this,
                                        SLOT(servicePropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
}

LocationBackend::~LocationBackend() = default;

bool LocationBackend::enabled() const { return m_enabled; }
QAbstractItemModel *LocationBackend::tasksModel() const { return m_tasksModel; }
QStringList LocationBackend::locations() const { return m_locations; }
QVariantMap LocationBackend::matches() const { return m_matches; }
QVariantList LocationBackend::excludedWindowIds() const { return m_excludedWindowIds; }
QString LocationBackend::capability() const { return m_capability; }

void LocationBackend::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    ++m_generation;
    if (enabled) {
        m_pollTimer->start();
        scheduleRefresh();
    } else {
        m_pollTimer->stop();
        m_refreshTimer->stop();
        m_services.clear();
        setCapability(QStringLiteral("disabled"));
    }
    rebuildMatches();
    Q_EMIT enabledChanged();
}

void LocationBackend::setTasksModel(QAbstractItemModel *model)
{
    if (m_tasksModel == model) return;
    for (const auto &connection : std::as_const(m_modelConnections)) disconnect(connection);
    m_modelConnections.clear();
    m_tasksModel = model;
    m_roles.clear();
    if (model) {
        const auto names = model->roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it) m_roles.insert(it.value(), it.key());
        auto update = [this] { rebuildMatches(); scheduleRefresh(); };
        m_modelConnections.append(connect(model, &QAbstractItemModel::rowsInserted, this, update));
        m_modelConnections.append(connect(model, &QAbstractItemModel::rowsRemoved, this, update));
        m_modelConnections.append(connect(model, &QAbstractItemModel::modelReset, this, update));
        m_modelConnections.append(connect(model, &QAbstractItemModel::layoutChanged, this, &LocationBackend::rebuildMatches));
        m_modelConnections.append(connect(model, &QAbstractItemModel::dataChanged, this, &LocationBackend::rebuildMatches));
        m_modelConnections.append(connect(model, &QObject::destroyed, this, [this] {
            m_tasksModel = nullptr;
            rebuildMatches();
            Q_EMIT tasksModelChanged();
        }));
    }
    rebuildMatches();
    scheduleRefresh();
    Q_EMIT tasksModelChanged();
}

QString LocationBackend::normalizedUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty() || url.scheme().isEmpty()) return {};
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash | QUrl::RemoveFragment | QUrl::RemovePassword).toString(QUrl::FullyEncoded);
}

bool LocationBackend::containsLocation(const QUrl &root, const QUrl &location)
{
    const QUrl base(normalizedUrl(root));
    const QUrl child(normalizedUrl(location));
    if (base.isEmpty() || child.isEmpty() || base.scheme() != child.scheme() || base.authority() != child.authority() || base.query() != child.query()) return false;
    const QString path = base.path(QUrl::FullyEncoded);
    const QString childPath = child.path(QUrl::FullyEncoded);
    return path == childPath || childPath.startsWith(path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/'));
}

void LocationBackend::setLocations(const QStringList &locations)
{
    QStringList normalized;
    for (const QString &location : locations) {
        const QString url = normalizedUrl(QUrl(location));
        if (!url.isEmpty() && !normalized.contains(url)) normalized.append(url);
    }
    if (m_locations == normalized) return;
    m_locations = normalized;
    ++m_generation;
    rebuildMatches();
    scheduleRefresh();
    Q_EMIT locationsChanged();
}

void LocationBackend::scheduleRefresh()
{
    if (m_enabled && !m_refreshTimer->isActive()) m_refreshTimer->start();
}

void LocationBackend::serviceOwnerChanged(const QString &name, const QString &, const QString &)
{
    if (!name.startsWith(QLatin1String("org.kde.dolphin-"))) return;
    ++m_generation;
    // Remove the old owner's mapping before a process ID or service name can be reused.
    m_services.removeIf([&name](const ServiceLocations &service) { return service.service == name; });
    rebuildMatches();
    scheduleRefresh();
}

void LocationBackend::servicePropertiesChanged(const QString &interface, const QVariantMap &changes, const QStringList &invalidated, const QDBusMessage &message)
{
    if (interface != QLatin1String(fileManagerInterface) || (!changes.contains(QStringLiteral("OpenWindowsWithLocations"))
        && !invalidated.contains(QStringLiteral("OpenWindowsWithLocations")))) return;
    // Ignore unrelated implementations of FileManager1 on the session bus.
    if (std::any_of(m_services.cbegin(), m_services.cend(), [&message](const ServiceLocations &service) { return service.owner == message.service(); })) {
        scheduleRefresh();
    }
}

void LocationBackend::refresh()
{
    if (!m_enabled) return;
    m_pollTimer->stop();
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->generation = ++m_generation;
    auto *watcher = call(this, QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                         QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, snapshot](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QStringList> reply = *finished;
        finished->deleteLater();
        if (snapshot->generation != m_generation || !m_enabled) return;
        QStringList services;
        if (!reply.isError()) {
            for (const auto &name : reply.value()) if (name.startsWith(QLatin1String("org.kde.dolphin-"))) services.append(name);
        }
        snapshot->remaining = services.size();
        if (services.isEmpty()) {
            m_services.clear();
            setCapability(QStringLiteral("unavailable"));
            rebuildMatches();
            m_pollTimer->start();
        }
        for (const auto &service : services) queryService(service, snapshot);
    });
}

void LocationBackend::queryService(const QString &name, const std::shared_ptr<Snapshot> &snapshot)
{
    auto *watcher = call(this, QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                         QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"), {name});
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, snapshot](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        if (snapshot->generation != m_generation || !m_enabled) return;
        ServiceLocations service;
        service.service = name;
        if (reply.isError()) { finishService(service, snapshot); return; }
        service.owner = reply.value();
        auto *pidWatcher = call(this, QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"), {service.owner});
        connect(pidWatcher, &QDBusPendingCallWatcher::finished, this, [this, service, snapshot](QDBusPendingCallWatcher *pidFinished) mutable {
            QDBusPendingReply<uint> pidReply = *pidFinished;
            pidFinished->deleteLater();
            if (snapshot->generation != m_generation || !m_enabled) return;
            if (pidReply.isError()) { finishService(service, snapshot); return; }
            service.pid = pidReply.value();
            queryLocations(service, snapshot);
        });
    });
}

void LocationBackend::queryLocations(const ServiceLocations &service, const std::shared_ptr<Snapshot> &snapshot)
{
    auto *watcher = call(this, service.owner, QString::fromLatin1(fileManagerPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                         QStringLiteral("Get"), {QString::fromLatin1(fileManagerInterface), QStringLiteral("OpenWindowsWithLocations")});
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, service = ServiceLocations(service), snapshot](QDBusPendingCallWatcher *finished) mutable {
        QDBusPendingReply<QDBusVariant> reply = *finished;
        finished->deleteLater();
        if (snapshot->generation != m_generation || !m_enabled) return;
        if (!reply.isError()) {
            service.windows = qdbus_cast<WindowLocations>(reply.value().variant());
            service.complete = true;
            finishService(service, snapshot);
            return;
        }
        // Stock Dolphin supports exact URL queries. It does not publish its tab
        // locations, so claiming descendant matches without the integration would be guessing.
        if (m_locations.isEmpty()) { finishService(service, snapshot); return; }
        auto result = std::make_shared<ServiceLocations>(service);
        auto remaining = std::make_shared<int>(m_locations.size());
        for (const auto &location : std::as_const(m_locations)) {
            auto *exact = call(this, service.owner, QString::fromLatin1(dolphinPath), QString::fromLatin1(dolphinInterface),
                               QStringLiteral("isUrlOpen"), {location});
            connect(exact, &QDBusPendingCallWatcher::finished, this, [this, result, remaining, snapshot, location](QDBusPendingCallWatcher *done) {
                QDBusPendingReply<bool> open = *done;
                done->deleteLater();
                if (snapshot->generation != m_generation || !m_enabled) return;
                if (!open.isError() && open.value()) result->exactLocations.append(location);
                if (--*remaining == 0) finishService(*result, snapshot);
            });
        }
    });
}

void LocationBackend::finishService(const ServiceLocations &service, const std::shared_ptr<Snapshot> &snapshot)
{
    if (snapshot->generation != m_generation || !m_enabled) return;
    if (service.pid) snapshot->services.append(service);
    if (--snapshot->remaining != 0) return;
    m_services = snapshot->services;
    std::sort(m_services.begin(), m_services.end(), [](const auto &left, const auto &right) { return left.service < right.service; });
    const bool full = std::any_of(m_services.cbegin(), m_services.cend(), [](const auto &provider) { return provider.complete; });
    const bool exact = std::any_of(m_services.cbegin(), m_services.cend(), [](const auto &provider) { return !provider.complete; });
    setCapability(full ? (exact ? QStringLiteral("mixed") : QStringLiteral("full")) : (exact ? QStringLiteral("exact") : QStringLiteral("unavailable")));
    rebuildMatches();
    m_pollTimer->start();
}

void LocationBackend::setCapability(const QString &capability)
{
    if (m_capability == capability) return;
    m_capability = capability;
    Q_EMIT capabilityChanged();
}

QVariant LocationBackend::role(const QModelIndex &index, const QByteArray &name) const
{
    const auto it = m_roles.constFind(name);
    return it == m_roles.cend() ? QVariant() : index.data(*it);
}

void LocationBackend::rebuildMatches()
{
    m_matchingIndexes.clear();
    QVariantMap matches;
    QVariantList excluded;
    QList<QPersistentModelIndex> windows;
    if (m_enabled && m_tasksModel) {
        std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
            for (int row = 0; row < m_tasksModel->rowCount(parent); ++row) {
                const auto index = m_tasksModel->index(row, 0, parent);
                if (m_tasksModel->rowCount(index)) collect(index);
                else if (role(index, "IsWindow").toBool() && isDolphin(role(index, "AppId").toString())) windows.append(index);
            }
        };
        collect({});
        for (const auto &service : std::as_const(m_services)) {
            QList<QPersistentModelIndex> candidates;
            for (const auto &window : std::as_const(windows)) {
                const auto menuService = role(window, "ApplicationMenuServiceName").toString();
                if (role(window, "AppPid").toUInt() == service.pid || menuService == service.owner || menuService == service.service) candidates.append(window);
            }
            for (const auto &window : std::as_const(candidates)) {
                QStringList urls;
                if (service.complete) {
                    for (auto it = service.windows.cbegin(); it != service.windows.cend(); ++it) {
                        bool identityMatches = false;
                        if (it.key().startsWith(QLatin1String("pid:"))) {
                            // Dolphin currently has exactly one main window per process.
                            // Refuse ambiguous matches instead of guessing from titles or PIDs.
                            identityMatches = candidates.size() == 1 && it.key().mid(4).toUInt() == service.pid;
                        } else if (it.key().startsWith(QLatin1String("x11:"))) {
                            const auto nativeId = it.key().mid(4).toULongLong();
                            for (const auto &id : role(window, "WinIdList").toList()) if (id.toULongLong() == nativeId && nativeId) identityMatches = true;
                        }
                        if (identityMatches) urls.append(it.value());
                    }
                } else if (candidates.size() == 1) {
                    urls = service.exactLocations;
                }
                for (const auto &root : std::as_const(m_locations)) {
                    const bool match = std::any_of(urls.cbegin(), urls.cend(), [&](const QString &url) {
                        return service.complete ? containsLocation(QUrl(root), QUrl(url)) : normalizedUrl(QUrl(url)) == root;
                    });
                    if (match && !m_matchingIndexes[root].contains(window)) m_matchingIndexes[root].append(window);
                }
            }
        }
        for (auto it = m_matchingIndexes.cbegin(); it != m_matchingIndexes.cend(); ++it) {
            QVariantList values;
            for (const auto &window : it.value()) {
                const auto ids = role(window, "WinIdList").toList();
                for (const auto &id : ids) if (!excluded.contains(id)) excluded.append(id);
                values.append(QVariantMap{{QStringLiteral("windowIds"), ids}, {QStringLiteral("title"), window.data(Qt::DisplayRole)},
                                          {QStringLiteral("active"), role(window, "IsActive")}, {QStringLiteral("minimized"), role(window, "IsMinimized")},
                                          {QStringLiteral("urgent"), role(window, "IsDemandingAttention")},
                                          {QStringLiteral("geometry"), role(window, "Geometry")},
                                          {QStringLiteral("pid"), role(window, "AppPid")}});
            }
            matches.insert(it.key(), values);
        }
    }
    if (m_matches != matches) { m_matches = matches; Q_EMIT matchesChanged(); }
    if (m_excludedWindowIds != excluded) { m_excludedWindowIds = excluded; Q_EMIT excludedWindowIdsChanged(); }
}

QVariantList LocationBackend::windowsForUrl(const QUrl &url) const { return m_matches.value(normalizedUrl(url)).toList(); }

QList<QPersistentModelIndex> LocationBackend::matchingIndexes(const QUrl &url) const
{
    auto indexes = m_matchingIndexes.value(normalizedUrl(url));
    indexes.removeIf([](const auto &index) { return !index.isValid(); });
    return indexes;
}

bool LocationBackend::request(const QByteArray &method, const QPersistentModelIndex &index)
{
    return m_tasksModel && index.isValid() && QMetaObject::invokeMethod(m_tasksModel, method.constData(), Qt::DirectConnection, Q_ARG(QModelIndex, QModelIndex(index)));
}

QPersistentModelIndex LocationBackend::preferredIndex(const QList<QPersistentModelIndex> &indexes, bool preferUrgent) const
{
    if (indexes.isEmpty()) return {};
    auto selected = indexes.first();
    QPersistentModelIndex active;
    qint64 mostRecent = 0;
    for (const auto &index : indexes) {
        if (preferUrgent && role(index, "IsDemandingAttention").toBool()) return index;
        if (role(index, "IsActive").toBool()) active = index;
        const auto activated = role(index, "LastActivated");
        const qint64 timestamp = activated.canConvert<QDateTime>() ? activated.toDateTime().toMSecsSinceEpoch() : activated.toLongLong();
        if (timestamp > mostRecent) { mostRecent = timestamp; selected = index; }
    }
    return active.isValid() ? active : selected;
}

bool LocationBackend::activate(const QUrl &url)
{
    return request("requestActivate", preferredIndex(matchingIndexes(url), true));
}

bool LocationBackend::activateWindow(const QUrl &url, const QVariant &windowId)
{
    for (const auto &index : matchingIndexes(url)) {
        if (role(index, "WinIdList").toList().contains(windowId)) return request("requestActivate", index);
    }
    return false;
}

bool LocationBackend::minimize(const QUrl &url, bool allWindows)
{
    const auto indexes = matchingIndexes(url);
    if (indexes.isEmpty()) return false;
    if (!allWindows) {
        const auto index = preferredIndex(indexes, false);
        return role(index, "IsMinimized").toBool() || request("requestToggleMinimized", index);
    }
    bool success = true;
    for (const auto &index : indexes) if (!role(index, "IsMinimized").toBool()) success = request("requestToggleMinimized", index) && success;
    return success;
}

bool LocationBackend::cycle(const QUrl &url, int direction)
{
    const auto indexes = matchingIndexes(url);
    if (indexes.isEmpty()) return false;
    int active = -1;
    for (int i = 0; i < indexes.size(); ++i) if (role(indexes[i], "IsActive").toBool()) { active = i; break; }
    const int next = active < 0 ? 0 : (active + (direction < 0 ? -1 : 1) + indexes.size()) % indexes.size();
    return request("requestActivate", indexes[next]);
}

bool LocationBackend::close(const QUrl &url)
{
    const auto indexes = matchingIndexes(url);
    if (indexes.isEmpty()) return false;
    bool success = true;
    for (const auto &index : indexes) success = request("requestClose", index) && success;
    return success;
}

bool LocationBackend::closeWindow(const QUrl &url, const QVariant &windowId)
{
    for (const auto &index : matchingIndexes(url)) {
        if (role(index, "WinIdList").toList().contains(windowId)) return request("requestClose", index);
    }
    return false;
}
