/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dockcontroller.h"

#include <KPluginMetaData>
#include <KSharedConfig>
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <PlasmaQuick/AppletQuickItem>
#include <LayerShellQt/Window>

#include <QDBusConnection>
#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <qpa/qplatformwindow_p.h>
#include <wayland-client-core.h>
#include <utility>

namespace
{
const QString service = QStringLiteral("org.gosh.GoshosDock.KWin");
const QString path = QStringLiteral("/org/gosh/GoshosDock");
const QString interface = QStringLiteral("org.gosh.GoshosDock.KWin1");

QDBusMessage pluginCall(const QString &method, const QVariantList &arguments)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    return message;
}

QDBusMessage panelCall(uint panelId, const QString &mode, bool checkDedicated)
{
    // Only numeric IDs and our fixed mode vocabulary enter this script.
    QString script = QStringLiteral("var p=panelById(%1); if(!p) throw new Error('Dock panel no longer exists'); ").arg(panelId);
    if (checkDedicated) {
        script += QStringLiteral("if(p.widgetIds.length!==1 || p.widgetById(p.widgetIds[0]).type!=='org.gosh.goshosdock') "
                                 "throw new Error('Dock controller requires a dedicated Gosho panel'); ");
    }
    script += QStringLiteral("p.hiding='%1'; print('goshosdock-mode-applied');").arg(mode);
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                               QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    call.setArguments({script});
    return call;
}
}

DockController::DockController(QObject *parent)
    : QObject(parent)
{
    m_schedule.setSingleShot(true);
    connect(&m_schedule, &QTimer::timeout, this, &DockController::synchronize);
    QDBusConnection::sessionBus().connect(service, path, interface, QStringLiteral("OverviewChanged"),
                                         this, SLOT(setOverviewVisible(bool)));
    QDBusConnection::sessionBus().connect(service, path, interface, QStringLiteral("OverviewAvailableChanged"),
                                         this, SLOT(setOverviewAvailable(bool)));
    refreshOverview();
    auto *watcher = new QDBusServiceWatcher(service, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &newOwner) {
        deactivate();
        setOverviewVisible(false);
        setOverviewAvailable(false);
        if (!newOwner.isEmpty()) {
            refreshOverview();
            schedule();
        } else if (m_enabled) {
            setError(QStringLiteral("The Gosho’s Dock KWin plugin is not available."));
        }
    });
}

DockController::~DockController()
{
    deactivate();
}

void DockController::setTarget(QQuickItem *target)
{
    if (m_target == target) {
        return;
    }
    deactivate();
    if (m_target) {
        disconnect(m_target, nullptr, this, nullptr);
    }
    m_target = target;
    if (target) {
        connect(target, &QQuickItem::windowChanged, this, [this] { deactivate(); schedule(); });
        connect(target, &QObject::destroyed, this, [this] { deactivate(); schedule(); });
    }
    Q_EMIT targetChanged();
    schedule();
}

void DockController::setConfiguration(const QVariantMap &configuration)
{
    if (m_configuration == configuration) {
        return;
    }
    m_configuration = configuration;
    Q_EMIT configurationChanged();
    schedule();
}

void DockController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!enabled) {
        deactivate();
        setError({});
    }
    Q_EMIT enabledChanged();
    schedule();
}

void DockController::setHoldVisible(bool holdVisible)
{
    if (m_holdVisible == holdVisible) {
        return;
    }
    m_holdVisible = holdVisible;
    Q_EMIT holdVisibleChanged();
    schedule();
}

void DockController::setUrgent(bool urgent)
{
    if (m_urgent == urgent) {
        return;
    }
    m_urgent = urgent;
    Q_EMIT urgentChanged();
    schedule();
}

void DockController::schedule()
{
    m_schedule.start(0);
}

void DockController::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
}

void DockController::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        Q_EMIT activeChanged();
    }
}

void DockController::setOverviewVisible(bool visible)
{
    ++m_overviewRevision;
    if (m_overviewVisible != visible) {
        m_overviewVisible = visible;
        Q_EMIT overviewVisibleChanged();
    }
}

void DockController::setOverviewAvailable(bool available)
{
    ++m_overviewAvailabilityRevision;
    if (m_overviewAvailable != available) {
        m_overviewAvailable = available;
        Q_EMIT overviewAvailableChanged();
    }
}

void DockController::refreshOverview()
{
    const quint64 availabilityRevision = m_overviewAvailabilityRevision;
    auto *availability = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
        pluginCall(QStringLiteral("OverviewAvailable"), {})), this);
    connect(availability, &QDBusPendingCallWatcher::finished, this, [this, availabilityRevision](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (availabilityRevision == m_overviewAvailabilityRevision && reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
            setOverviewAvailable(reply.arguments().first().toBool());
        }
    });
    const quint64 revision = m_overviewRevision;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
        pluginCall(QStringLiteral("OverviewVisible"), {})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, revision](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        // A signal received after the request contains newer compositor state.
        if (revision == m_overviewRevision && reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
            setOverviewVisible(reply.arguments().first().toBool());
        }
    });
}

void DockController::hideOverview()
{
    if (m_overviewVisible) {
        // The compositor's signal is authoritative; keep state unchanged if
        // the request fails or a different fullscreen effect is active.
        QDBusConnection::sessionBus().asyncCall(pluginCall(QStringLiteral("HideOverview"), {}));
    }
}

void DockController::toggleOverview()
{
    if (m_overviewAvailable) {
        QDBusConnection::sessionBus().asyncCall(pluginCall(QStringLiteral("ToggleOverview"), {}));
    }
}

bool DockController::resolveTarget()
{
    Plasma::Applet *applet = nullptr;
    for (QQuickItem *item = m_target; item; item = item->parentItem()) {
        if (auto *appletItem = qobject_cast<PlasmaQuick::AppletQuickItem *>(item)) {
            applet = appletItem->applet();
            break;
        }
    }
    auto *containment = applet ? applet->containment() : nullptr;
    const uint panelId = containment ? containment->id() : 0;
    const uint appletId = applet ? applet->id() : 0;
    if (m_applet != applet || m_containment != containment) {
        deactivate();
        if (m_applet) {
            disconnect(m_applet, nullptr, this, nullptr);
        }
        if (m_containment) {
            disconnect(m_containment, nullptr, this, nullptr);
        }
        m_applet = applet;
        m_containment = containment;
        if (applet) {
            connect(applet, &Plasma::Applet::containmentChanged, this, &DockController::schedule);
        }
        if (containment) {
            connect(containment, &Plasma::Containment::appletAdded, this, &DockController::schedule);
            connect(containment, &Plasma::Containment::appletRemoved, this, &DockController::schedule);
            connect(containment, &Plasma::Containment::containmentTypeChanged, this, &DockController::schedule);
            connect(containment, &Plasma::Containment::locationChanged, this, &DockController::schedule);
        }
    }
    if (m_panelId != panelId || m_appletId != appletId) {
        m_panelId = panelId;
        m_appletId = appletId;
        Q_EMIT identityChanged();
    }
    if (!applet || applet->pluginMetaData().pluginId() != QStringLiteral("org.gosh.goshosdock")) {
        setError(QStringLiteral("Dock control requires the Gosho’s Dock applet."));
        return false;
    }
    if (!containment || (containment->containmentType() != Plasma::Containment::Panel
                         && containment->containmentType() != Plasma::Containment::CustomPanel)
        || containment->applets().size() != 1 || containment->applets().first() != applet) {
        setError(QStringLiteral("Dock hiding requires a dedicated panel containing only Gosho’s Dock."));
        return false;
    }
    return true;
}

bool DockController::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window && event->type() == QEvent::PlatformSurface) {
        auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            deactivate();
        } else {
            schedule();
        }
    }
    return QObject::eventFilter(object, event);
}

void DockController::synchronize()
{
    const bool validTarget = resolveTarget();
    if (!m_enabled) {
        setError({});
        return;
    }
    if (!validTarget) {
        deactivate();
        return;
    }
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        deactivate();
        setError(QStringLiteral("Native dock hiding requires a Plasma Wayland session."));
        return;
    }
    auto *window = m_target ? m_target->window() : nullptr;
    if (m_window != window) {
        deactivate();
        if (m_window) {
            m_window->removeEventFilter(this);
        }
        m_window = window;
        if (window) {
            window->installEventFilter(this);
        }
    }
    if (!window) {
        setError(QStringLiteral("Waiting for the dock panel’s window."));
        return;
    }
    auto *nativeWindow = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!nativeWindow || !nativeWindow->surface()) {
        setError(QStringLiteral("Waiting for the dock panel’s Wayland surface."));
        return;
    }
    if (m_token.isEmpty()) {
        if (!m_registering && !m_registrationPending) {
            registerDock(wl_proxy_get_id(reinterpret_cast<wl_proxy *>(nativeWindow->surface())));
        }
    } else {
        applyPanelMode();
        updateDock();
    }
}

void DockController::registerDock(uint surfaceId)
{
    ++m_registrationAttempts;
    m_registering = true;
    m_registrationPending = true;
    const quint64 generation = ++m_generation;
    const QPointer<DockController> guard(this);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
        pluginCall(QStringLiteral("RegisterDock"), {surfaceId, m_panelId, dockConfiguration()})), QCoreApplication::instance());
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [guard, generation](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        const QString token = reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()
            ? reply.arguments().first().toString() : QString();
        if (guard) {
            guard->m_registrationPending = false;
        }
        if (!guard || generation != guard->m_generation) {
            if (!token.isEmpty()) {
                QDBusConnection::sessionBus().asyncCall(pluginCall(QStringLiteral("UnregisterDock"), {token}));
            }
            if (guard) {
                guard->schedule();
            }
            return;
        }
        guard->m_registering = false;
        if (token.isEmpty()) {
            guard->setError(QStringLiteral("The Gosho’s Dock KWin plugin could not attach to this panel: %1").arg(reply.errorMessage()));
            // A newly created PanelView can expose its wl_surface before
            // KWin receives the layer-shell role and first committed buffer.
            // Recheck ownership on every retry; foreign/mixed panels never
            // enter registration. Stop after a bounded startup grace period.
            if (reply.type() != QDBusMessage::ErrorMessage && guard->m_registrationAttempts < 16
                && guard->m_enabled && guard->m_target && guard->m_target->window() == guard->m_window
                && guard->resolveTarget() && generation == guard->m_generation) {
                guard->m_schedule.start(qMin(500, 100 * guard->m_registrationAttempts));
            }
            return;
        }
        guard->m_registrationAttempts = 0;
        guard->m_token = token;
        if (!guard->savePanelMode()) {
            guard->deactivate();
            return;
        }
        guard->applyPanelMode();
        guard->updateDock();
    });
}

QString DockController::panelModeName(int rawMode)
{
    switch (rawMode) {
    case 0: return QStringLiteral("none");
    case 1: return QStringLiteral("autohide");
    case 2: return QStringLiteral("dodgewindows");
    case 3: return QStringLiteral("windowsgobelow");
    default: return {};
    }
}

QVariantMap DockController::dockConfiguration() const
{
    QVariantMap configuration = m_configuration;
    // The panel may have been moved with Plasma's own editor. Reveal follows
    // its actual edge, including while a requested layout change is pending.
    if (m_containment) {
        int edge = 2;
        switch (m_containment->location()) {
        case Plasma::Types::TopEdge: edge = 0; break;
        case Plasma::Types::RightEdge: edge = 1; break;
        case Plasma::Types::BottomEdge: edge = 2; break;
        case Plasma::Types::LeftEdge: edge = 3; break;
        default: break;
        }
        configuration.insert(QStringLiteral("dockPosition"), edge);
    }
    return configuration;
}

bool DockController::savePanelMode()
{
    if (m_savedModeValid) {
        return true;
    }
    m_backup = m_applet->config().group(QStringLiteral("NativeDockController"));
    const KConfigGroup panel(KSharedConfig::openConfig(QStringLiteral("plasmashellrc")), QStringLiteral("PlasmaViews"));
    const int rawMode = panel.group(QStringLiteral("Panel %1").arg(m_panelId)).readEntry("panelVisibility", 0);
    m_savedMode = m_backup.readEntry("panelId", 0u) == m_panelId
        ? m_backup.readEntry("originalPanelVisibility", rawMode) : rawMode;
    if (panelModeName(m_savedMode).isEmpty()) {
        setError(QStringLiteral("This panel uses an unsupported visibility mode; its settings were preserved."));
        return false;
    }
    m_savedPanelId = m_panelId;
    m_savedModeValid = true;
    m_backup.writeEntry("panelId", m_panelId);
    m_backup.writeEntry("originalPanelVisibility", m_savedMode);
    m_backup.writeEntry("controllerToken", m_token);
    m_backup.sync();
    return true;
}

void DockController::applyPanelMode()
{
    const QString mode = m_configuration.value(QStringLiteral("dockFixed")).toBool()
            && !m_configuration.value(QStringLiteral("manualHide")).toBool()
        ? QStringLiteral("none") : QStringLiteral("windowsgobelow");
    if (mode == m_appliedMode || m_modePending) {
        return;
    }
    m_modePending = true;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(panelCall(m_panelId, mode, true)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation, mode](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (generation != m_generation) {
            return;
        }
        m_modePending = false;
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()
            || !reply.arguments().first().toString().contains(QStringLiteral("goshosdock-mode-applied"))) {
            deactivate();
            setError(QStringLiteral("Plasma could not apply the dock panel’s visibility mode."));
            return;
        }
        m_appliedMode = mode;
        raiseManagedPanel();
        suppressPanelBackground();
        setActive(true);
        setError({});
        // Catch configuration changes received during this asynchronous call.
        schedule();
    });
}

void DockController::raiseManagedPanel()
{
    if (!m_window) {
        return;
    }
    if (!m_visibilityConnection) {
        const int propertyIndex = m_window->metaObject()->indexOfProperty("visibilityMode");
        if (propertyIndex >= 0) {
            const auto property = m_window->metaObject()->property(propertyIndex);
            const int slotIndex = metaObject()->indexOfSlot("panelVisibilityModeChanged()");
            if (property.hasNotifySignal() && slotIndex >= 0) {
                m_visibilityConnection = connect(m_window, property.notifySignal(), this, metaObject()->method(slotIndex));
            }
        }
    }
    auto *layerWindow = LayerShellQt::Window::get(m_window);
    if (!layerWindow) {
        return;
    }
    if (!m_layerWindow) {
        m_layerWindow = layerWindow;
        m_originalLayer = layerWindow->layer();
        m_layerConnection = connect(layerWindow, &LayerShellQt::Window::layerChanged, this, [this] {
            if (!m_token.isEmpty() && m_layerWindow && m_layerWindow->layer() != LayerShellQt::Window::LayerOverlay) {
                m_layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
            }
        });
    }
    // LayerTop is below active fullscreen windows in KWin. Overlay allows the
    // compositor policy to reveal a requested dock above fullscreen content.
    layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
}

void DockController::panelVisibilityModeChanged()
{
    if (m_token.isEmpty() || !m_window) {
        return;
    }
    const int expected = m_configuration.value(QStringLiteral("dockFixed")).toBool()
            && !m_configuration.value(QStringLiteral("manualHide")).toBool() ? 0 : 3;
    if (m_window->property("visibilityMode").toInt() != expected) {
        // The panel editor may change its native AutoHide/Dodge mode while
        // this controller owns hiding. Reapply without replacing the backup.
        m_appliedMode.clear();
        schedule();
    }
}

void DockController::suppressPanelBackground()
{
    if (!m_containment) {
        return;
    }
    if (!m_backgroundContainment) {
        m_backgroundContainment = m_containment;
        m_originalBackgroundHints = static_cast<int>(m_containment->backgroundHints());
        m_backgroundConnection = connect(m_containment, &Plasma::Applet::backgroundHintsChanged, this, [this] {
            if (!m_token.isEmpty() && m_backgroundContainment
                && m_backgroundContainment->backgroundHints() != Plasma::Types::NoBackground) {
                m_backgroundContainment->setBackgroundHints(Plasma::Types::NoBackground);
            }
        });
    }
    // Plasma's Panel.qml binds both its frame SVGs and PanelView's blur/mask
    // to the containment's public backgroundHints. The applet supplies its
    // own theme/custom/transparent background for this dedicated panel.
    m_backgroundContainment->setBackgroundHints(Plasma::Types::NoBackground);
}

void DockController::updateDock()
{
    if (m_token.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
        pluginCall(QStringLiteral("UpdateDock"), {m_token, dockConfiguration(), m_holdVisible, m_urgent})), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *watcher) {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (generation == m_generation && (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty() || !reply.arguments().first().toBool())) {
            deactivate();
            setError(QStringLiteral("The KWin dock controller disconnected."));
        }
    });
}

void DockController::deactivate()
{
    ++m_generation;
    m_registering = false;
    m_registrationAttempts = 0;
    m_modePending = false;
    m_appliedMode.clear();
    setActive(false);
    disconnect(m_visibilityConnection);
    m_visibilityConnection = {};
    disconnect(m_backgroundConnection);
    if (m_backgroundContainment) {
        m_backgroundContainment->setBackgroundHints(static_cast<Plasma::Types::BackgroundHints>(m_originalBackgroundHints));
        m_backgroundContainment.clear();
    }
    const QPointer<LayerShellQt::Window> layerWindow = m_layerWindow;
    const auto originalLayer = static_cast<LayerShellQt::Window::Layer>(m_originalLayer);
    disconnect(m_layerConnection);
    m_layerWindow.clear();
    if (layerWindow) {
        layerWindow->setLayer(originalLayer);
    }
    if (!m_token.isEmpty()) {
        const QString token = std::exchange(m_token, QString());
        QDBusConnection::sessionBus().asyncCall(pluginCall(QStringLiteral("UnregisterDock"), {token}));
    }
    if (m_savedModeValid) {
        m_savedModeValid = false;
        const quint64 generation = m_generation;
        KConfigGroup backup = m_backup;
        const QString backupToken = backup.readEntry("controllerToken", QString());
        const QPointer<DockController> guard(this);
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
            panelCall(m_savedPanelId, panelModeName(m_savedMode), false)), QCoreApplication::instance());
        connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [guard, generation, backup, backupToken, layerWindow, originalLayer](QDBusPendingCallWatcher *watcher) mutable {
            const auto reply = watcher->reply();
            watcher->deleteLater();
            if ((!guard || generation == guard->m_generation) && layerWindow) {
                // Plasma's hiding setter can reset the layer; restore it again
                // after that request, including when the applet was removed.
                layerWindow->setLayer(originalLayer);
            }
            if ((!guard || generation == guard->m_generation) && reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()
                && reply.arguments().first().toString().contains(QStringLiteral("goshosdock-mode-applied"))) {
                if (backup.readEntry("controllerToken", QString()) == backupToken) {
                    backup.deleteGroup();
                    backup.sync();
                }
            } else if (guard && generation == guard->m_generation) {
                guard->setError(QStringLiteral("Plasma could not restore the panel’s visibility mode. Its recovery settings were retained."));
            }
        });
    }
}

#include "moc_dockcontroller.cpp"
