// SPDX-License-Identifier: GPL-2.0-or-later
#include "dockvisibility.h"
#include "shortcutnormalization.h"

#include <core/output.h>
#include <compositor.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <input_event.h>
#include <keyboard_input.h>
#include <keyboard_layout.h>
#include <xkb.h>
#include <pointer_input.h>
#include <wayland/clientconnection.h>
#include <wayland/seat.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QPropertyAnimation>
#include <QUuid>

namespace {
constexpr auto service = "org.gosh.GoshosDock.KWin";
constexpr auto objectPath = "/org/gosh/GoshosDock";
template<typename Rect> QRectF rectangle(const Rect &r) { return QRectF(r.x(), r.y(), r.width(), r.height()); }
bool interesting(KWin::Window *w)
{
    return w && w->isShown() && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
        && (w->isNormalWindow() || w->isDialog() || w->isUtility() || w->isToolbar()
            || w->isSplash() || w->isMenu() || w->isDropdownMenu() || w->isDock());
}
bool verticallyMaximized(KWin::Window *w)
{
    const auto tiled = w->quickTileMode();
    return w->maximizeMode() == KWin::MaximizeVertical
        || tiled == KWin::QuickTileFlag::Left || tiled == KWin::QuickTileFlag::Right;
}
}

DockVisibility::DockVisibility()
    : KWin::InputEventFilter(KWin::InputFilterOrder::ScreenEdge)
    , m_watcher(new QDBusServiceWatcher(this))
{
    m_clock.start();
    m_previousPointer = KWin::input()->globalPointer();
    m_watcher->setConnection(QDBusConnection::sessionBus());
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &owner) {
        const auto keys = m_docks.keys();
        for (const QString &token : keys) {
            if (m_docks.value(token)->owner == owner)
                release(token);
        }
    });
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QString::fromLatin1(objectPath), this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
        || !bus.registerService(QString::fromLatin1(service))) {
        qWarning("Gosho's Dock: cannot register compositor visibility service");
        return;
    }
    KWin::input()->installInputEventFilter(this);
    m_timer.setInterval(50); // Same order as source intellihide's 100 ms check.
    connect(&m_timer, &QTimer::timeout, this, &DockVisibility::refresh);
    connect(KWin::Compositor::self(), &KWin::Compositor::compositingToggled, this, [this](bool) {
        observeOverview();
        observeKeyboardLayout();
    });
    observeOverview();
    observeKeyboardLayout();
}

DockVisibility::~DockVisibility()
{
    for (const auto &token : m_docks.keys())
        release(token);
    QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(objectPath));
    QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(service));
}

QString DockVisibility::RegisterDock(uint surfaceId, uint containmentId, const QVariantMap &configuration)
{
    // A Wayland object ID is local to a client. Pair it with the bus caller's
    // verified process ID, never a supplied PID, title or screen geometry.
    if (!calledFromDBus() || !KWin::waylandServer() || !surfaceId || !containmentId)
        return {};
    const QString owner = message().service();
    const QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(owner);
    if (!pid.isValid())
        return {};
    KWin::Window *window = nullptr;
    for (KWin::Window *candidate : KWin::workspace()->stackingOrder()) {
        auto *surface = candidate->surface();
        if (candidate->isDock() && surface && surface->id() == surfaceId
            && uint(surface->client()->processId()) == pid.value()) {
            window = candidate;
            break;
        }
    }
    if (!window)
        return {};
    for (auto it = m_docks.cbegin(); it != m_docks.cend(); ++it) {
        if (it.value()->window == window)
            return it.value()->owner == owner && it.value()->containmentId == containmentId ? it.key() : QString();
    }
    auto dock = std::make_shared<Dock>();
    dock->owner = owner;
    dock->containmentId = containmentId;
    dock->window = window;
    dock->originalOpacity = window->opacity();
    dock->originallyHidden = window->isHidden();
    dock->targetVisible = !dock->originallyHidden;
    dock->configuration = configuration;
    dock->policy.configure(GoshosDock::Settings::fromMap(configuration));
    dock->animation = new QPropertyAnimation(window, "opacity", this);
    dock->animation->setEasingCurve(QEasingCurve::OutQuad);
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_docks.insert(token, dock);
    connect(dock->animation, &QPropertyAnimation::finished, this, [dock]() {
        if (dock->window && !dock->targetVisible) {
            dock->window->setHidden(true);
            dock->window->setOpacity(dock->originalOpacity);
        }
    });
    connect(window, &KWin::Window::closed, this, [this, token]() { release(token); });
    m_watcher->addWatchedService(owner);
    m_timer.start();
    refresh();
    return token;
}

DockVisibility::DockPtr DockVisibility::ownedDock(const QString &token) const
{
    const auto dock = m_docks.value(token);
    return dock && calledFromDBus() && dock->owner == message().service() ? dock : nullptr;
}

bool DockVisibility::UpdateDock(const QString &token, const QVariantMap &configuration, bool holdVisible, bool urgent)
{
    const auto dock = ownedDock(token);
    if (!dock)
        return false;
    if (configuration != dock->configuration) {
        dock->configuration = configuration;
        dock->policy.configure(GoshosDock::Settings::fromMap(configuration));
    }
    dock->holdVisible = holdVisible;
    dock->urgent = urgent;
    refresh();
    return true;
}

bool DockVisibility::UnregisterDock(const QString &token)
{
    if (!ownedDock(token))
        return false;
    release(token);
    return true;
}

QVariantMap DockVisibility::QueryDock(const QString &token)
{
    const auto dock = ownedDock(token);
    if (!dock || !dock->window)
        return {};
    return {{QStringLiteral("visible"), dock->targetVisible}, {QStringLiteral("hidden"), dock->window->isHidden()},
            {QStringLiteral("opacity"), dock->window->opacity()}, {QStringLiteral("overlap"), dock->environment.overlap},
            {QStringLiteral("fullscreen"), dock->environment.fullscreen}, {QStringLiteral("overview"), dock->environment.overview},
            {QStringLiteral("pointerX"), KWin::input()->globalPointer().x()},
            {QStringLiteral("pointerY"), KWin::input()->globalPointer().y()}};
}

QVariantMap DockVisibility::PointerOutputGeometry() const
{
    if (!calledFromDBus())
        return {};
    const QString owner = message().service();
    bool ownsDock = false;
    for (const auto &dock : std::as_const(m_docks)) {
        if (dock->owner == owner && dock->window && !dock->window->isDeleted()) {
            ownsDock = true;
            break;
        }
    }
    if (!ownsDock)
        return {};
    const auto *output = KWin::workspace()->outputAt(KWin::input()->globalPointer());
    if (!output)
        return {};
    const auto geometry = output->geometry();
    return {{QStringLiteral("x"), geometry.x()}, {QStringLiteral("y"), geometry.y()},
            {QStringLiteral("width"), geometry.width()}, {QStringLiteral("height"), geometry.height()}};
}

QVariantList DockVisibility::NormalizedAlternateShortcuts() const
{
    if (!calledFromDBus())
        return {};
    const QString owner = message().service();
    for (const auto &dock : std::as_const(m_docks)) {
        if (dock->owner == owner && dock->window && !dock->window->isDeleted()) {
            const auto *xkb = KWin::input()->keyboard()->xkb();
            return GoshosDock::normalizedAlternateShortcuts(xkb->keymap(), xkb->currentLayout());
        }
    }
    return {};
}

void DockVisibility::observeKeyboardLayout()
{
    auto *layout = KWin::input()->keyboard()->keyboardLayout();
    if (m_keyboardLayout == layout)
        return;
    disconnect(m_layoutConnection);
    disconnect(m_keymapConnection);
    m_keyboardLayout = layout;
    if (layout) {
        m_layoutConnection = connect(layout, &KWin::KeyboardLayout::layoutChanged, this, [this](uint) {
            Q_EMIT AlternateShortcutsChanged();
        });
        m_keymapConnection = connect(layout, &KWin::KeyboardLayout::layoutsReconfigured, this, [this]() {
            Q_EMIT AlternateShortcutsChanged();
        });
        Q_EMIT AlternateShortcutsChanged();
    }
}

bool DockVisibility::OverviewVisible() const
{
    if (!KWin::effects || !KWin::effects->activeFullScreenEffect())
        return false;
    return QByteArray(KWin::effects->activeFullScreenEffect()->metaObject()->className()) == "KWin::OverviewEffect";
}

bool DockVisibility::HideOverview()
{
    // Closing an already closed overview must never toggle it open. The
    // native effect exports deactivate() as a public Qt slot.
    return OverviewVisible() && QMetaObject::invokeMethod(KWin::effects->activeFullScreenEffect(), "deactivate", Qt::DirectConnection);
}

bool DockVisibility::OverviewAvailable() const
{
    return KWin::effects && KWin::effects->isEffectSupported(QStringLiteral("overview"));
}

bool DockVisibility::ToggleOverview()
{
    if (OverviewVisible())
        return HideOverview();
    if (!OverviewAvailable())
        return false;
    auto *effect = KWin::effects->findEffect(QStringLiteral("overview"));
    if (!effect && KWin::effects->loadEffect(QStringLiteral("overview")))
        effect = KWin::effects->findEffect(QStringLiteral("overview"));
    // EffectsHandler::toggleEffect toggles plugin loading, not the UI. The
    // native Overview effect's activate slot performs the requested action.
    return effect && QMetaObject::invokeMethod(effect, "activate", Qt::DirectConnection);
}

void DockVisibility::observeOverview()
{
    disconnect(m_overviewConnection);
    if (KWin::effects) {
        m_overviewConnection = connect(KWin::effects, &KWin::EffectsHandler::activeFullScreenEffectChanged,
                                      this, &DockVisibility::publishOverview);
    }
    publishOverview();
}

void DockVisibility::publishOverview()
{
    const bool available = OverviewAvailable();
    if (available != m_overviewAvailable) {
        m_overviewAvailable = available;
        Q_EMIT OverviewAvailableChanged(available);
    }
    const bool visible = OverviewVisible();
    if (visible != m_overviewVisible) {
        m_overviewVisible = visible;
        Q_EMIT OverviewChanged(visible);
        refresh();
    }
}

void DockVisibility::release(const QString &token)
{
    const auto dock = m_docks.take(token);
    if (!dock)
        return;
    dock->animation->stop();
    dock->animation->deleteLater();
    if (dock->window && !dock->window->isDeleted()) {
        dock->window->setHidden(dock->originallyHidden);
        dock->window->setOpacity(dock->originalOpacity);
    }
    bool ownerRemaining = false;
    for (const auto &other : std::as_const(m_docks))
        ownerRemaining |= other->owner == dock->owner;
    if (!ownerRemaining)
        m_watcher->removeWatchedService(dock->owner);
    if (m_docks.isEmpty())
        m_timer.stop();
}

GoshosDock::Environment DockVisibility::environment(const Dock &dock) const
{
    GoshosDock::Environment e;
    e.holdVisible = dock.holdVisible;
    e.urgent = dock.urgent;
    e.locked = KWin::waylandServer()->isScreenLocked();
    // Overview and its desktop grid share this native Plasma effect.
    e.overview = OverviewVisible();
    const auto *panel = dock.window.data();
    if (!panel)
        return e;
    const QRectF panelRect = rectangle(panel->frameGeometry());
    const QPointF pointer = KWin::input()->globalPointer();
    e.hover = !panel->isHidden() && panelRect.contains(pointer);
    if (panel->output() && rectangle(panel->output()->geometry()).contains(pointer)) {
        switch (dock.policy.settings().edge) {
        case 0: e.revealBand = pointer.y() <= panelRect.bottom(); break;
        case 1: e.revealBand = pointer.x() >= panelRect.left(); break;
        case 2: e.revealBand = pointer.y() >= panelRect.top(); break;
        case 3: e.revealBand = pointer.x() <= panelRect.right(); break;
        }
    }
    KWin::Window *active = KWin::workspace()->activeWindow();
    KWin::Window *top = nullptr;
    const auto &windows = KWin::workspace()->stackingOrder();
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
        if (*it != panel && interesting(*it) && !(*it)->isDock() && (*it)->output() == panel->output()) {
            top = *it;
            break;
        }
    }
    for (KWin::Window *w : windows) {
        if (w == panel)
            continue;
        // Native menus and stack popups must retain the panel while open.
        if (w->isShown() && w->transientFor() == panel)
            e.holdVisible = true;
        if (!interesting(w))
            continue;
        if (w->isFullScreen() && w->output() == panel->output())
            e.fullscreen = true;
        const QRectF frame = rectangle(w->frameGeometry());
        if (!(frame.left() < panelRect.right() && frame.right() >= panelRect.left()
              && frame.top() < panelRect.bottom() && frame.bottom() >= panelRect.top()))
            continue;
        switch (dock.policy.settings().intelliHideMode) {
        case 1:
            if (active && !KWin::Window::belongToSameApplication(w, active)
                && (!top || !KWin::Window::belongToSameApplication(w, top)) && !w->keepAbove()
                && !(verticallyMaximized(active) && verticallyMaximized(w)
                     && w->output() == active->output()))
                continue;
            break;
        case 2:
            if (w->maximizeMode() == KWin::MaximizeRestore && !verticallyMaximized(w) && !w->isFullScreen())
                continue;
            break;
        case 3:
            if (active && !active->isFullScreen())
                continue;
            break;
        default:
            break;
        }
        e.overlap = true;
    }
    return e;
}

void DockVisibility::apply(Dock &dock, bool visible)
{
    if (!dock.window)
        return;
    if (dock.targetVisible == visible
        && (dock.animation->state() == QAbstractAnimation::Running || dock.window->isHidden() == !visible))
        return;
    dock.targetVisible = visible;
    dock.animation->stop();
    const auto &settings = dock.policy.settings();
    const int duration = (settings.manualHide && !dock.environment.overview) || dock.environment.locked ? 0 : settings.animationTime;
    if (visible && dock.window->isHidden()) {
        dock.window->setOpacity(duration ? 0 : dock.originalOpacity);
        dock.window->setHidden(false);
    }
    if (!duration) {
        dock.window->setHidden(!visible);
        dock.window->setOpacity(dock.originalOpacity);
        return;
    }
    dock.animation->setDuration(duration);
    dock.animation->setStartValue(dock.window->opacity());
    dock.animation->setEndValue(visible ? dock.originalOpacity : 0);
    dock.animation->start();
}

void DockVisibility::refresh()
{
    const qint64 now = m_clock.elapsed();
    for (const auto &dock : std::as_const(m_docks)) {
        dock->environment = environment(*dock);
        // A compositor warp or output change can move the pointer without
        // ordinary motion reaching this filter. Cancel stale dwell/pressure
        // then; our own internal-edge warp remains on the same valid edge.
        if (dock->window && dock->window->output()) {
            const auto *output = dock->window->output();
            const QRectF bounds = rectangle(output->geometry());
            const QRectF work = rectangle(KWin::workspace()->clientArea(KWin::MaximizeArea, output));
            if (!GoshosDock::Policy::onEdge(KWin::input()->globalPointer(), bounds, work, dock->policy.settings().edge))
                dock->policy.cancelEdge();
        } else {
            dock->policy.cancelEdge();
        }
        apply(*dock, dock->policy.update(dock->environment, now));
    }
}

void DockVisibility::cancelEdges()
{
    for (const auto &dock : std::as_const(m_docks))
        dock->policy.cancelEdge();
}

bool DockVisibility::pointerButton(KWin::PointerButtonEvent *)
{
    cancelEdges();
    return false;
}

bool DockVisibility::keyboardKey(KWin::KeyboardKeyEvent *)
{
    cancelEdges();
    return false;
}

bool DockVisibility::pointerMotion(KWin::PointerMotionEvent *event)
{
    if (m_warping || event->warp) {
        m_previousPointer = event->position;
        return false;
    }
    const QPointF previous = m_previousPointer;
    m_previousPointer = event->position;
    const qint64 now = m_clock.elapsed();
    for (const auto &dock : std::as_const(m_docks)) {
        if (!dock->window || !dock->window->output())
            continue;
        const auto *output = dock->window->output();
        const QRectF bounds = rectangle(output->geometry());
        const QRectF work = rectangle(KWin::workspace()->clientArea(KWin::MaximizeArea, output)).intersected(bounds);
        const auto &settings = dock->policy.settings();
        dock->environment = environment(*dock);
        QPointF edgePoint = event->position;
        // Only pressure-enabled, hidden managed docks create an internal
        // monitor barrier. Dragging, fullscreen suppression and absolute
        // devices are never trapped, and native corner actions are excluded.
        const bool relative = !event->deltaUnaccelerated.isNull();
        const auto insideOutput = [&bounds](const QPointF &p) {
            return p.x() >= bounds.left() && p.x() < bounds.right() && p.y() >= bounds.top() && p.y() < bounds.bottom();
        };
        const bool crossing = insideOutput(previous) && !insideOutput(edgePoint);
        bool barrier = false;
        if (crossing && !dock->targetVisible && relative && event->buttons == Qt::NoButton
            && settings.pressureToShow && dock->policy.canReveal(dock->environment)
            && !KWin::waylandServer()->seat()->isDrag()) {
            switch (settings.edge) {
            case 0: if (edgePoint.y() < bounds.top()) { edgePoint.setY(bounds.top()); barrier = true; } break;
            case 1: if (edgePoint.x() >= bounds.right()) { edgePoint.setX(bounds.right() - 1); barrier = true; } break;
            case 2: if (edgePoint.y() >= bounds.bottom()) { edgePoint.setY(bounds.bottom() - 1); barrier = true; } break;
            case 3: if (edgePoint.x() < bounds.left()) { edgePoint.setX(bounds.left()); barrier = true; } break;
            }
        }
        const bool atEdge = GoshosDock::Policy::onEdge(edgePoint, bounds, work, settings.edge);
        const QPointF delta = event->deltaUnaccelerated;
        const double outward = GoshosDock::Policy::outwardDelta(delta, settings.edge);
        const double parallel = settings.edge % 2 ? delta.y() : delta.x();
        const bool revealed = dock->policy.motion(atEdge, outward, parallel, relative,
                                                 event->buttons != Qt::NoButton, dock->environment, now);
        apply(*dock, dock->policy.update(dock->environment, now));
        if (barrier && atEdge && !revealed) {
            m_warping = true;
            KWin::input()->pointer()->warp(edgePoint);
            m_warping = false;
            m_previousPointer = edgePoint;
            return true;
        }
    }
    return false;
}
