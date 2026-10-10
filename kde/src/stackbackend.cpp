/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stackbackend.h"
#include "applicationfilter.h"
#include "remotemounts.h"

#include <KApplicationTrader>
#include <KConfig>
#include <KConfigGroup>
#include <KFileItem>
#include <KFilePlacesModel>
#include <KIO/ApplicationLauncherJob>
#include <KIO/EmptyTrashJob>
#include <KIO/ListJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KNotificationJobUiDelegate>
#include <KService>
#include <KSycoca>
#include <PlasmaActivities/Stats/ResultModel>
#include <PlasmaActivities/Stats/Terms>
#include <Solid/Device>

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace
{
KService::Ptr applicationService(const QString &id)
{
    const QUrl url(id);
    QString storageId = id;
    if (url.scheme() == QLatin1String("applications")) {
        storageId = url.path();
        while (storageId.startsWith(QLatin1Char('/'))) {
            storageId.remove(0, 1);
        }
    } else if (url.isLocalFile()) {
        storageId = url.toLocalFile();
    }
    auto service = KService::serviceByStorageId(storageId);
    if (!service && !storageId.endsWith(QLatin1String(".desktop"))) {
        service = KService::serviceByStorageId(storageId + QStringLiteral(".desktop"));
    }
    return service;
}

bool visibleApplication(const KService::Ptr &service)
{
    return service && service->isValid() && service->isApplication() && !service->noDisplay() && service->showInCurrentDesktop()
        && ApplicationFilter::instance()->isAllowed(service->storageId());
}

QVariantMap applicationEntry(const KService::Ptr &service)
{
    QUrl url;
    url.setScheme(QStringLiteral("applications"));
    url.setPath(service->storageId());
    return {
        {QStringLiteral("name"), service->name()},
        {QStringLiteral("icon"), service->icon().isEmpty() ? QStringLiteral("application-x-executable") : service->icon()},
        {QStringLiteral("url"), url},
        {QStringLiteral("desktopId"), service->storageId()},
    };
}

void addErrorDelegate(KJob *job)
{
    auto *delegate = new KNotificationJobUiDelegate;
    delegate->setAutoErrorHandlingEnabled(true);
    job->setUiDelegate(delegate);
}

QString normalizedPlaceUrl(const QUrl &url)
{
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash | QUrl::RemovePassword)
        .toString(QUrl::FullyEncoded);
}
}

StackBackend::StackBackend(QObject *parent)
    : StackBackend(nullptr, parent)
{
}

StackBackend::StackBackend(RemoteMounts *remoteMounts, QObject *parent)
    : QObject(parent)
    , m_placesModel(new KFilePlacesModel(this))
    , m_remoteMounts(remoteMounts ? remoteMounts : new RemoteMounts(this))
    , m_trashWatcher(new QFileSystemWatcher(this))
    , m_folderWatcher(new QFileSystemWatcher(this))
{
    using namespace KActivities::Stats;
    using namespace KActivities::Stats::Terms;
    // Plasma records application launches as applications:<desktop-file-id> resources.
    // ResultModel follows activity-manager changes, rather than taking a startup snapshot.
    m_recentModel = new ResultModel(UsedResources | HighScoredFirst | Agent::any() | Activity::any()
                                       | Url::startsWith(QStringLiteral("applications:")) | Limit(200),
                                   this);
    connect(m_recentModel, &QAbstractItemModel::modelReset, this, &StackBackend::refreshRecentApplications);
    connect(m_recentModel, &QAbstractItemModel::rowsInserted, this, &StackBackend::refreshRecentApplications);
    connect(m_recentModel, &QAbstractItemModel::rowsRemoved, this, &StackBackend::refreshRecentApplications);
    connect(m_recentModel, &QAbstractItemModel::dataChanged, this, &StackBackend::refreshRecentApplications);
    connect(m_recentModel, &QAbstractItemModel::layoutChanged, this, &StackBackend::refreshRecentApplications);

    connect(m_placesModel, &QAbstractItemModel::modelReset, this, &StackBackend::refreshPlaces);
    connect(m_placesModel, &QAbstractItemModel::rowsInserted, this, &StackBackend::refreshPlaces);
    connect(m_placesModel, &QAbstractItemModel::rowsRemoved, this, &StackBackend::refreshPlaces);
    connect(m_placesModel, &QAbstractItemModel::dataChanged, this, &StackBackend::refreshPlaces);
    connect(m_placesModel, &QAbstractItemModel::layoutChanged, this, &StackBackend::refreshPlaces);
    connect(m_placesModel, &KFilePlacesModel::errorMessage, this, &StackBackend::setError);
    connect(m_remoteMounts, &RemoteMounts::mountsChanged, this, &StackBackend::refreshPlaces);
    connect(m_remoteMounts, &RemoteMounts::operationFailed, this, [this](const QString &id, const QString &message) {
        m_pendingRemoteMounts.remove(id);
        setError(message);
    });
    connect(m_remoteMounts, &RemoteMounts::operationFinished, this, [this](const QString &id) {
        if (!m_pendingRemoteMounts.remove(id)) return;
        for (const auto &value : m_remoteMounts->mounts()) {
            const auto mount = value.toMap();
            if (mount.value(QStringLiteral("id")).toString() == id) {
                const QUrl url = mount.value(QStringLiteral("url")).toUrl();
                if (url.isValid() && !url.isEmpty()) openUrl(url);
                return;
            }
        }
    });
    connect(m_placesModel, &KFilePlacesModel::setupDone, this, [this](const QModelIndex &index, bool success) {
        const bool requested = m_pendingMounts.removeAll(QPersistentModelIndex(index)) > 0;
        refreshPlaces();
        if (requested && success) {
            openUrl(m_placesModel->url(index));
        }
    });
    connect(ApplicationFilter::instance(), &ApplicationFilter::changed, this, [this] {
        refreshApplications();
        refreshRecentApplications();
    });
    connect(KSycoca::self(), &KSycoca::databaseChanged, this, [this] {
        refreshApplications();
        refreshRecentApplications();
    });

    // Watch both the file and its directory: KConfig may replace trashrc atomically.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (QFileInfo::exists(configDir)) {
        m_trashWatcher->addPath(configDir);
    }
    connect(m_trashWatcher, &QFileSystemWatcher::fileChanged, this, &StackBackend::refreshTrash);
    connect(m_trashWatcher, &QFileSystemWatcher::directoryChanged, this, &StackBackend::refreshTrash);
    connect(m_trashWatcher, &QFileSystemWatcher::directoryChanged, this, &StackBackend::refreshRecentApplications);

    auto *folderRefreshTimer = new QTimer(this);
    folderRefreshTimer->setSingleShot(true);
    folderRefreshTimer->setInterval(150);
    connect(m_folderWatcher, &QFileSystemWatcher::directoryChanged, folderRefreshTimer, qOverload<>(&QTimer::start));
    connect(folderRefreshTimer, &QTimer::timeout, this, [this] {
        if (m_folderUrl.isValid()) {
            listFolder(m_folderUrl);
        }
    });

    refresh();
}

StackBackend::~StackBackend()
{
    cancelListing();
}

QVariantList StackBackend::applications() const { return m_applications; }
QVariantList StackBackend::recentApplications() const { return m_recentApplications; }
QVariantList StackBackend::places() const { return m_places; }
QVariantList StackBackend::entries() const { return m_entries; }
int StackBackend::totalItems() const { return m_entries.size(); }
bool StackBackend::busy() const { return m_busy; }
QString StackBackend::error() const { return m_error; }
bool StackBackend::trashEmpty() const { return m_trashEmpty; }

QVariantMap StackBackend::standardLocations() const
{
    return {
        {QStringLiteral("home"), QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))},
        {QStringLiteral("documents"), QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))},
        {QStringLiteral("downloads"), QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))},
        {QStringLiteral("trash"), QUrl(QStringLiteral("trash:/"))},
    };
}

void StackBackend::refresh()
{
    refreshApplications();
    refreshRecentApplications();
    refreshPlaces();
    refreshTrash();
    if (!m_folderUrl.isEmpty()) {
        listFolder(m_folderUrl);
    }
}

void StackBackend::refreshApplications()
{
    auto services = KApplicationTrader::query(visibleApplication);
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(services.begin(), services.end(), [&collator](const KService::Ptr &left, const KService::Ptr &right) {
        return collator.compare(left->name(), right->name()) < 0;
    });
    QVariantList entries;
    QSet<QString> seen;
    for (const auto &service : std::as_const(services)) {
        if (!seen.contains(service->storageId())) {
            seen.insert(service->storageId());
            entries.append(applicationEntry(service));
        }
    }
    if (m_applications != entries) {
        m_applications = entries;
        Q_EMIT applicationsChanged();
    }
}

void StackBackend::refreshRecentApplications()
{
    QVariantList entries;
    QSet<QString> seen;
    // Match Plasma's native recent-document menu: a disabled history setting must
    // hide old cached entries as well as stop recording newly launched applications.
    const KConfig historyConfig(QStringLiteral("kactivitymanagerd-pluginsrc"));
    const bool historyEnabled = historyConfig.group(QStringLiteral("Plugin-org.kde.ActivityManager.Resources.Scoring"))
                                    .readEntry("what-to-remember", 0) != 2;
    for (int row = 0; historyEnabled && row < m_recentModel->rowCount(); ++row) {
        const auto id = m_recentModel->index(row, 0).data(KActivities::Stats::ResultModel::ResourceRole).toString();
        const auto service = applicationService(id);
        if (visibleApplication(service) && !seen.contains(service->storageId())) {
            seen.insert(service->storageId());
            entries.append(applicationEntry(service));
        }
    }
    if (m_recentApplications != entries) {
        m_recentApplications = entries;
        Q_EMIT recentApplicationsChanged();
    }
    // There is no QML view attached to the native model to request its next page.
    // Continue fetching ourselves so filtering pinned/running entries has enough candidates.
    if (historyEnabled && m_recentModel->canFetchMore({})) {
        QTimer::singleShot(0, m_recentModel, [model = m_recentModel] {
            if (model->canFetchMore({})) {
                model->fetchMore({});
            }
        });
    }
}

void StackBackend::refreshPlaces()
{
    QVariantList entries;
    QHash<QString, QVariantMap> remoteByUrl;
    const QVariantList remoteMounts = m_remoteMounts->mounts();
    for (const auto &value : remoteMounts) {
        const auto mount = value.toMap();
        const QString url = normalizedPlaceUrl(mount.value(QStringLiteral("url")).toUrl());
        if (!url.isEmpty()) remoteByUrl.insert(url, mount);
    }
    QSet<QString> nativeUrls;
    for (int row = 0; row < m_placesModel->rowCount(); ++row) {
        const QModelIndex index = m_placesModel->index(row, 0);
        if (m_placesModel->isHidden(index)) {
            continue;
        }
        const QUrl url = m_placesModel->url(index);
        const QString normalizedUrl = normalizedPlaceUrl(url);
        nativeUrls.insert(normalizedUrl);
        const auto remoteMount = remoteByUrl.value(normalizedUrl);
        const auto group = m_placesModel->groupType(index);
        const bool isDevice = m_placesModel->isDevice(index);
        entries.append(QVariantMap{
            {QStringLiteral("id"), placeIdentity(index)},
            {QStringLiteral("name"), m_placesModel->text(index)},
            {QStringLiteral("icon"), index.data(KFilePlacesModel::IconNameRole).toString()},
            {QStringLiteral("url"), url},
            {QStringLiteral("isDevice"), isDevice},
            {QStringLiteral("isNetwork"), group == KFilePlacesModel::RemoteType || !remoteMount.isEmpty()},
            {QStringLiteral("isRemovable"), group == KFilePlacesModel::RemovableDevicesType},
            {QStringLiteral("setupNeeded"), m_placesModel->setupNeeded(index) || remoteMount.value(QStringLiteral("setupNeeded")).toBool()},
            {QStringLiteral("canMount"), m_placesModel->setupNeeded(index) || remoteMount.value(QStringLiteral("canMount")).toBool()},
            {QStringLiteral("canTeardown"), m_placesModel->isTeardownAllowed(index) || remoteMount.value(QStringLiteral("canUnmount")).toBool()},
            {QStringLiteral("canEject"), m_placesModel->isEjectAllowed(index) || remoteMount.value(QStringLiteral("canEject")).toBool()},
            {QStringLiteral("busy"), remoteMount.value(QStringLiteral("busy")).toBool() || m_pendingMounts.contains(QPersistentModelIndex(index))},
            {QStringLiteral("remoteMountId"), remoteMount.value(QStringLiteral("id"))},
            {QStringLiteral("index"), row},
        });
    }
    int remoteIndex = m_placesModel->rowCount();
    for (const auto &value : remoteMounts) {
        const auto mount = value.toMap();
        const QUrl url = mount.value(QStringLiteral("url")).toUrl();
        const QString normalizedUrl = normalizedPlaceUrl(url);
        if (!normalizedUrl.isEmpty() && nativeUrls.contains(normalizedUrl)) {
            continue;
        }
        if (!normalizedUrl.isEmpty()) nativeUrls.insert(normalizedUrl);
        entries.append(QVariantMap{
            {QStringLiteral("id"), mount.value(QStringLiteral("id"))},
            {QStringLiteral("name"), mount.value(QStringLiteral("name"))},
            {QStringLiteral("icon"), mount.value(QStringLiteral("icon"))},
            {QStringLiteral("url"), url},
            {QStringLiteral("isDevice"), false},
            {QStringLiteral("isNetwork"), true},
            {QStringLiteral("isRemovable"), false},
            {QStringLiteral("setupNeeded"), mount.value(QStringLiteral("setupNeeded"))},
            {QStringLiteral("canMount"), mount.value(QStringLiteral("canMount"))},
            {QStringLiteral("canTeardown"), mount.value(QStringLiteral("canUnmount"))},
            {QStringLiteral("canEject"), mount.value(QStringLiteral("canEject"))},
            {QStringLiteral("busy"), mount.value(QStringLiteral("busy"))},
            {QStringLiteral("remoteMountId"), mount.value(QStringLiteral("id"))},
            {QStringLiteral("index"), remoteIndex++},
        });
    }
    if (m_places != entries) {
        m_places = entries;
        Q_EMIT placesChanged();
    }
}

void StackBackend::refreshTrash()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/trashrc");
    if (QFileInfo::exists(path) && !m_trashWatcher->files().contains(path)) {
        m_trashWatcher->addPath(path);
    }
    const KConfig config(QStringLiteral("trashrc"), KConfig::SimpleConfig);
    const bool empty = config.group(QStringLiteral("Status")).readEntry("Empty", true);
    if (m_trashEmpty != empty) {
        m_trashEmpty = empty;
        Q_EMIT trashEmptyChanged();
    }
}

void StackBackend::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void StackBackend::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
    if (!error.isEmpty()) {
        Q_EMIT errorOccurred(error);
    }
}

void StackBackend::watchJob(KJob *job)
{
    addErrorDelegate(job);
    connect(job, &KJob::result, this, [this](KJob *finished) {
        if (finished->error() && finished->error() != KJob::KilledJobError) {
            setError(finished->errorString());
        }
    });
}

void StackBackend::openUrl(const QUrl &url)
{
    if (url.scheme() == QLatin1String("applications")) {
        launchApplication(url.toString());
        return;
    }
    if (!url.isValid() || url.isEmpty() || url.scheme().isEmpty()) {
        setError(i18n("This location is not a valid URL."));
        return;
    }
    setError({});
    auto *job = new KIO::OpenUrlJob(url);
    watchJob(job);
    job->start();
}

void StackBackend::launchApplication(const QString &desktopId)
{
    const auto service = applicationService(desktopId);
    if (!service || !service->isValid() || !service->isApplication()) {
        setError(i18n("The application %1 is no longer installed.", desktopId));
        return;
    }
    setError({});
    auto *job = new KIO::ApplicationLauncherJob(service);
    watchJob(job);
    job->start();
}

void StackBackend::openNewWindow(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty() || url.scheme().isEmpty()) {
        setError(i18n("This location is not a valid URL."));
        return;
    }
    const auto service = KApplicationTrader::preferredService(QStringLiteral("inode/directory"));
    if (!service || (service->desktopEntryName() != QLatin1String("org.kde.dolphin")
                     && service->desktopEntryName() != QLatin1String("dolphin"))) {
        openUrl(url);
        return;
    }
    // Dolphin normally reuses an existing window. Preserve its configured desktop
    // entry and let KIO expand URL placeholders with its normal argument escaping.
    // This is a desktop-entry Exec field, never a shell command.
    KService::Ptr newWindow(new KService(*service));
    newWindow->setExec(service->exec() + QStringLiteral(" --new-window"));
    setError({});
    auto *job = new KIO::ApplicationLauncherJob(newWindow);
    job->setUrls({url});
    watchJob(job);
    job->start();
}

void StackBackend::cancelListing()
{
    if (m_listJob) {
        auto *job = m_listJob.data();
        m_listJob = nullptr;
        job->kill(KJob::Quietly);
    }
    if (!m_folderWatcher->directories().isEmpty()) {
        m_folderWatcher->removePaths(m_folderWatcher->directories());
    }
    m_folderUrl.clear();
    setBusy(false);
}

void StackBackend::listFolder(const QUrl &url)
{
    // Copy before cancelListing(): refresh() can pass m_folderUrl itself.
    const QUrl requestedUrl = url;
    cancelListing();
    m_entries.clear();
    Q_EMIT entriesChanged();
    setError({});
    if (!requestedUrl.isValid() || requestedUrl.isEmpty() || requestedUrl.scheme().isEmpty()) {
        setError(i18n("This folder is not a valid URL."));
        return;
    }
    m_folderUrl = requestedUrl;
    setBusy(true);
    auto *job = KIO::listDir(requestedUrl, KIO::HideProgressInfo);
    m_listJob = job;
    connect(job, &KIO::ListJob::entries, this, [this, job](KIO::Job *, const KIO::UDSEntryList &items) {
        if (m_listJob != job) {
            return;
        }
        for (const auto &entry : items) {
            const auto name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            if (name == QLatin1String(".") || name == QLatin1String("..")) {
                continue;
            }
            const KFileItem item(entry, job->url(), true, true);
            m_entries.append(QVariantMap{
                {QStringLiteral("name"), item.name()},
                {QStringLiteral("icon"), item.iconName()},
                {QStringLiteral("url"), item.url()},
                {QStringLiteral("modified"), item.time(KFileItem::ModificationTime).toMSecsSinceEpoch()},
                {QStringLiteral("isDir"), item.isDir()},
                {QStringLiteral("mimeType"), item.mimetype()},
                {QStringLiteral("hidden"), item.isHidden()},
            });
        }
        Q_EMIT entriesChanged();
    });
    connect(job, &KJob::result, this, [this, job](KJob *finished) {
        if (m_listJob != job) {
            return;
        }
        m_listJob = nullptr;
        setBusy(false);
        if (finished->error() && finished->error() != KJob::KilledJobError) {
            setError(finished->errorString());
        } else if (m_folderUrl.isLocalFile() && QFileInfo(m_folderUrl.toLocalFile()).isDir()) {
            m_folderWatcher->addPath(m_folderUrl.toLocalFile());
        }
    });
}

QString StackBackend::placeIdentity(const QModelIndex &index) const
{
    if (m_placesModel->isDevice(index)) {
        // Unlike the mount point, the UDI stays the same across mount/unmount.
        return QStringLiteral("device:") + m_placesModel->deviceForIndex(index).udi();
    }
    return QStringLiteral("url:") + m_placesModel->url(index).toString(QUrl::FullyEncoded);
}

QModelIndex StackBackend::placeIndex(int row, const QString &expectedId)
{
    const QModelIndex index = m_placesModel->index(row, 0);
    if (!index.isValid() || (!expectedId.isEmpty() && placeIdentity(index) != expectedId)) {
        setError(i18n("This device or location is no longer available."));
        return {};
    }
    return index;
}

QVariantMap StackBackend::placeEntry(int row, const QString &expectedId)
{
    for (const auto &value : std::as_const(m_places)) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("index")).toInt() == row
            && (expectedId.isEmpty() || entry.value(QStringLiteral("id")).toString() == expectedId)) {
            return entry;
        }
    }
    setError(i18n("This device or location is no longer available."));
    return {};
}

void StackBackend::setupPlace(int row, const QString &expectedId)
{
    const auto entry = placeEntry(row, expectedId);
    if (entry.isEmpty() || entry.value(QStringLiteral("busy")).toBool()) {
        return;
    }
    const QString remoteId = entry.value(QStringLiteral("remoteMountId")).toString();
    if (!remoteId.isEmpty() && entry.value(QStringLiteral("setupNeeded")).toBool()) {
        setError({});
        m_pendingRemoteMounts.insert(remoteId);
        if (!m_remoteMounts->mount(remoteId)) m_pendingRemoteMounts.remove(remoteId);
        return;
    }
    if (row >= m_placesModel->rowCount()) {
        openUrl(entry.value(QStringLiteral("url")).toUrl());
        return;
    }
    const auto index = placeIndex(row, expectedId);
    if (!index.isValid()) {
        return;
    }
    setError({});
    if (!m_placesModel->setupNeeded(index)) {
        openUrl(m_placesModel->url(index));
    } else if (!m_pendingMounts.contains(QPersistentModelIndex(index))) {
        m_pendingMounts.append(QPersistentModelIndex(index));
        refreshPlaces();
        m_placesModel->requestSetup(index);
    }
}

void StackBackend::teardownPlace(int row, const QString &expectedId)
{
    const auto entry = placeEntry(row, expectedId);
    if (entry.isEmpty() || entry.value(QStringLiteral("busy")).toBool()) {
        return;
    }
    const QString remoteId = entry.value(QStringLiteral("remoteMountId")).toString();
    if (!remoteId.isEmpty() && (row >= m_placesModel->rowCount() || !m_placesModel->isTeardownAllowed(m_placesModel->index(row, 0)))) {
        setError({});
        m_remoteMounts->unmount(remoteId);
        return;
    }
    const auto index = placeIndex(row, expectedId);
    if (!index.isValid()) {
        return;
    }
    if (!m_placesModel->isTeardownAllowed(index)) {
        setError(i18n("This device cannot be safely unmounted."));
        return;
    }
    setError({});
    m_placesModel->requestTeardown(index);
}

void StackBackend::ejectPlace(int row, const QString &expectedId)
{
    const auto entry = placeEntry(row, expectedId);
    if (entry.isEmpty() || entry.value(QStringLiteral("busy")).toBool()) {
        return;
    }
    const QString remoteId = entry.value(QStringLiteral("remoteMountId")).toString();
    if (!remoteId.isEmpty() && (row >= m_placesModel->rowCount() || !m_placesModel->isEjectAllowed(m_placesModel->index(row, 0)))) {
        setError({});
        m_remoteMounts->unmount(remoteId, true);
        return;
    }
    const auto index = placeIndex(row, expectedId);
    if (!index.isValid()) {
        return;
    }
    if (!m_placesModel->isEjectAllowed(index)) {
        setError(i18n("This device cannot be ejected."));
        return;
    }
    setError({});
    m_placesModel->requestEject(index);
}

void StackBackend::emptyTrash()
{
    setError({});
    auto *job = KIO::emptyTrash();
    watchJob(job);
    connect(job, &KJob::result, this, &StackBackend::refreshTrash);
}
