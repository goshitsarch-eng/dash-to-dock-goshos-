// SPDX-License-Identifier: GPL-2.0-or-later
// Include GIO before Qt, whose signals keyword conflicts with GLib headers.
#include <gio/gdesktopappinfo.h>
#include <libmalcontent/manager.h>
#include <unistd.h>
#include "applicationfilter.h"
#include <KService>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QFutureWatcher>
#include <QPointer>
#include <QUrl>
#include <QtConcurrentRun>
#include <utility>

namespace {
using Filter = std::shared_ptr<MctAppFilter>;
struct Result { Filter filter; bool absent = false; };
Result readPolicy()
{
    GError *error = nullptr;
    auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        g_clear_error(&error);
        return {{}, true};
    }
    auto *manager = mct_manager_new(bus);
    auto *filter = mct_manager_get_app_filter(manager, getuid(), MCT_MANAGER_GET_VALUE_FLAGS_NONE, nullptr, &error);
    // No installed policy is a supported state. A transient read failure must
    // not discard the last successfully read restriction.
    const bool absent = error && (g_error_matches(error, MCT_MANAGER_ERROR, MCT_MANAGER_ERROR_DISABLED)
        || g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)
        || g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER));
    g_clear_error(&error);
    g_object_unref(manager);
    g_object_unref(bus);
    return {filter ? Filter(filter, mct_app_filter_unref) : Filter{}, absent};
}
}

struct ApplicationFilter::Private {
    Filter filter;
    bool loaded = false;
    bool busy = false;
    bool pending = false;
};

ApplicationFilter *ApplicationFilter::instance()
{
    static QPointer<ApplicationFilter> filter;
    if (!filter) filter = new ApplicationFilter(QCoreApplication::instance());
    return filter;
}

ApplicationFilter::ApplicationFilter(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    auto bus = QDBusConnection::systemBus();
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.freedesktop.Accounts"), bus,
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &ApplicationFilter::reload);
    bus.connect(QStringLiteral("org.freedesktop.Accounts"), QString(), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    reload();
}
ApplicationFilter::~ApplicationFilter() = default;

void ApplicationFilter::propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &)
{
    if (interface == QLatin1String("com.endlessm.ParentalControls.AppFilter")) reload();
}

void ApplicationFilter::reload()
{
    if (d->busy) { d->pending = true; return; }
    d->busy = true;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        d->busy = false;
        if (result.filter || result.absent) d->filter = result.filter;
        d->loaded = true;
        Q_EMIT changed();
        if (std::exchange(d->pending, false)) reload();
    });
    watcher->setFuture(QtConcurrent::run(readPolicy));
}

bool ApplicationFilter::allows(MctAppFilter *filter, const QString &desktopFile)
{
    if (!filter) return true;
    auto *info = g_desktop_app_info_new_from_filename(desktopFile.toUtf8().constData());
    if (!info) return false;
    const bool allowed = mct_app_filter_is_appinfo_allowed(filter, G_APP_INFO(info));
    g_object_unref(info);
    return allowed;
}

bool ApplicationFilter::isAllowed(const QString &desktopId) const
{
    if (!d->loaded) return false;
    if (!d->filter) return true;
    const QUrl url(desktopId);
    QString id = url.scheme() == QLatin1String("applications") ? url.path() : desktopId;
    auto service = url.isLocalFile() ? KService::serviceByDesktopPath(url.toLocalFile()) : KService::serviceByStorageId(id);
    if (!service && !id.endsWith(QLatin1String(".desktop"))) service = KService::serviceByStorageId(id + QStringLiteral(".desktop"));
    // Window-only tasks without a desktop identity cannot appear in the
    // application chooser. They remain manageable by the user.
    return !service || allows(d->filter.get(), service->entryPath());
}
