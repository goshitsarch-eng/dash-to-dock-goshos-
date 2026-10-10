/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "applicationactions.h"

#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>
#include <KIO/DesktopExecParser>
#include <KDesktopFile>
#include <KLocalizedString>
#include <KNotification>
#include <KNotificationJobUiDelegate>
#include <KPropertiesDialog>

#include <QAction>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

namespace
{
const QString gpuService = QStringLiteral("net.hadess.SwitcherooControl");
const QString gpuPath = QStringLiteral("/net/hadess/SwitcherooControl");
}

ApplicationActions::ApplicationActions(QObject *parent)
    : ApplicationActions(QDBusConnection::systemBus(), parent)
{
}

ApplicationActions::ApplicationActions(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_gpuBus(bus)
{
    auto *watcher = new QDBusServiceWatcher(gpuService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &ApplicationActions::refreshGpus);
    m_gpuBus.connect(gpuService, gpuPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    refreshGpus();
}

void ApplicationActions::propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface == gpuService && (changed.contains(QStringLiteral("GPUs")) || invalidated.contains(QStringLiteral("GPUs")))) {
        refreshGpus();
    }
}

void ApplicationActions::refreshGpus()
{
    const quint64 generation = ++m_generation;
    auto message = QDBusMessage::createMethodCall(gpuService, gpuPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message.setArguments({gpuService});
    auto *watcher = new QDBusPendingCallWatcher(m_gpuBus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (generation != m_generation) {
            return;
        }
        QList<QVariantMap> gpus;
        if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
            const auto properties = qdbus_cast<QVariantMap>(reply.arguments().first());
            gpus = qdbus_cast<QList<QVariantMap>>(properties.value(QStringLiteral("GPUs")));
        }
        if (m_gpus != gpus) {
            m_gpus = std::move(gpus);
            Q_EMIT gpuChoicesChanged();
        }
    });
}

KService::Ptr ApplicationActions::serviceForUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        auto service = KService::serviceByDesktopPath(url.toLocalFile());
        if (!service && KDesktopFile::isDesktopFile(url.toLocalFile()) && QFile::exists(url.toLocalFile())) {
            service = KService::Ptr(new KService(url.toLocalFile()));
        }
        return service;
    }
    if (url.scheme() == QStringLiteral("applications")) {
        return KService::serviceByMenuId(url.path());
    }
    return {};
}

QProcessEnvironment ApplicationActions::gpuEnvironment(const QList<QVariantMap> &gpus, int gpuIndex, const QProcessEnvironment &base)
{
    QProcessEnvironment environment(base);
    // A shell itself launched on a discrete GPU can inherit its routing vars.
    // Clear every advertised override, then apply exactly the chosen device.
    for (const auto &gpu : gpus) {
        const QStringList pairs = qdbus_cast<QStringList>(gpu.value(QStringLiteral("Environment")));
        for (qsizetype i = 0; i + 1 < pairs.size(); i += 2) {
            environment.remove(pairs.at(i));
        }
    }
    if (gpuIndex >= 0 && gpuIndex < gpus.size()) {
        const QStringList pairs = qdbus_cast<QStringList>(gpus.at(gpuIndex).value(QStringLiteral("Environment")));
        for (qsizetype i = 0; i + 1 < pairs.size(); i += 2) {
            environment.insert(pairs.at(i), pairs.at(i + 1));
        }
    }
    return environment;
}

KIO::CommandLauncherJob *ApplicationActions::gpuLaunchJob(const KService::Ptr &service, int gpuIndex, QObject *parent) const
{
    if (!service || !KDesktopFile::isAuthorizedDesktopFile(service->entryPath()) || gpuIndex < 0 || gpuIndex >= m_gpus.size()) {
        return nullptr;
    }
    KIO::DesktopExecParser parser(*service, {});
    QStringList arguments = parser.resultingArguments();
    if (arguments.isEmpty()) {
        return nullptr;
    }
    // ApplicationLauncherJob has no per-launch GPU override. This public KIO
    // job accepts argv and an explicit environment, preserving startup tokens
    // and desktop identity without changing the installed desktop entry.
    const QString executable = arguments.takeFirst();
    auto *job = new KIO::CommandLauncherJob(executable, arguments, parent);
    job->setDesktopName(service->desktopEntryName());
    job->setWorkingDirectory(service->workingDirectory());
    job->setProcessEnvironment(gpuEnvironment(m_gpus, gpuIndex, QProcessEnvironment::systemEnvironment()));
    return job;
}

QVariantList ApplicationActions::gpuActions(const QUrl &launcherUrl, QObject *parent)
{
    QVariantList actions;
    const auto service = serviceForUrl(launcherUrl);
    if (!parent || !service || !service->isApplication() || m_gpus.size() < 2) {
        return actions;
    }
    for (qsizetype i = 0; i < m_gpus.size(); ++i) {
        const auto &gpu = m_gpus.at(i);
        const QString name = gpu.value(QStringLiteral("Name")).toString();
        const QString fallback = gpu.value(QStringLiteral("Default")).toBool()
            ? i18n("Integrated Graphics") : i18n("Discrete Graphics");
        auto *action = new QAction(QIcon::fromTheme(QStringLiteral("video-display")),
                                   i18nc("@action:inmenu GPU name", "Launch Using %1", name.isEmpty() ? fallback : name), parent);
        connect(action, &QAction::triggered, this, [this, service, i] {
            if (auto *job = gpuLaunchJob(service, i)) {
                auto *delegate = new KNotificationJobUiDelegate;
                delegate->setAutoErrorHandlingEnabled(true);
                job->setUiDelegate(delegate);
                job->start();
            } else {
                KNotification::event(KNotification::Error, i18n("Could Not Launch Application"), i18n("The application desktop entry could not be started."));
            }
        });
        actions.append(QVariant::fromValue(action));
    }
    return actions;
}

QUrl ApplicationActions::detailsUrl(const KService::Ptr &service, const QStringList &dataDirectories)
{
    if (!service) {
        return {};
    }
    const QSet<QString> desktopIds{service->storageId(), service->menuId(), service->desktopEntryName() + QStringLiteral(".desktop")};
    // Resolve the component ID from installed AppStream metadata. Desktop IDs
    // and AppStream IDs are not interchangeable (e.g. Firefox distributions).
    for (const QString &dataDir : dataDirectories) {
        for (const QString &subdirectory : {QStringLiteral("metainfo"), QStringLiteral("appdata")}) {
            const QDir directory(dataDir + u'/' + subdirectory);
            for (const QString &filename : directory.entryList({QStringLiteral("*.xml")}, QDir::Files)) {
                QFile file(directory.filePath(filename));
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                QXmlStreamReader xml(&file);
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (!xml.isStartElement() || (xml.name() != QStringLiteral("component") && xml.name() != QStringLiteral("application"))) {
                        continue;
                    }
                    QString componentId;
                    bool matches = false;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == QStringLiteral("id")) {
                            componentId = xml.readElementText();
                        } else if (xml.name() == QStringLiteral("launchable") && xml.attributes().value(QStringLiteral("type")) == QStringLiteral("desktop-id")) {
                            const QString desktopId = xml.readElementText();
                            matches = matches || desktopIds.contains(desktopId);
                        } else {
                            xml.skipCurrentElement();
                        }
                    }
                    if (!xml.hasError() && !componentId.isEmpty() && (matches || desktopIds.contains(componentId))) {
                        // Discover accepts an appstream URI path as well as a
                        // host. The path preserves case in component IDs.
                        QUrl url;
                        url.setScheme(QStringLiteral("appstream"));
                        url.setPath(componentId);
                        return url;
                    }
                }
            }
        }
    }
    return {};
}

QAction *ApplicationActions::detailsAction(const QUrl &launcherUrl, QObject *parent)
{
    const auto service = serviceForUrl(launcherUrl);
    if (!service || !service->isApplication() || !parent) {
        return nullptr;
    }
    auto *action = new QAction(QIcon::fromTheme(QStringLiteral("help-about")), i18nc("@action:inmenu", "App Details"), parent);
    connect(action, &QAction::triggered, this, [service] {
        const auto discover = KService::serviceByStorageId(QStringLiteral("org.kde.discover.desktop"));
        const QUrl url = discover ? detailsUrl(service, QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) : QUrl();
        if (discover && !url.isEmpty()) {
            auto *job = new KIO::ApplicationLauncherJob(discover);
            job->setUrls({url});
            auto *delegate = new KNotificationJobUiDelegate;
            delegate->setAutoErrorHandlingEnabled(true);
            job->setUiDelegate(delegate);
            job->start();
        } else {
            KPropertiesDialog::showDialog(QUrl::fromLocalFile(service->entryPath()), nullptr);
        }
    });
    return action;
}

#include "moc_applicationactions.cpp"
