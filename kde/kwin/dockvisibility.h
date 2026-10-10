// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "dockpolicy.h"
#include <input.h>
#include <plugin.h>
#include <QDBusContext>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>

namespace KWin { class Window; }
class QDBusServiceWatcher;
class QPropertyAnimation;

class DockVisibility final : public KWin::Plugin, public KWin::InputEventFilter, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gosh.GoshosDock.KWin1")
public:
    DockVisibility();
    ~DockVisibility() override;
    bool pointerMotion(KWin::PointerMotionEvent *event) override;
    bool pointerButton(KWin::PointerButtonEvent *event) override;
    bool keyboardKey(KWin::KeyboardKeyEvent *event) override;

public Q_SLOTS:
    QString RegisterDock(uint surfaceId, uint containmentId, const QVariantMap &configuration);
    bool UpdateDock(const QString &token, const QVariantMap &configuration, bool holdVisible, bool urgent);
    bool UnregisterDock(const QString &token);
    QVariantMap QueryDock(const QString &token);
    QVariantMap PointerOutputGeometry() const;
    QVariantList NormalizedAlternateShortcuts() const;
    bool OverviewVisible() const;
    bool OverviewAvailable() const;
    bool HideOverview();
    bool ToggleOverview();

Q_SIGNALS:
    void OverviewChanged(bool visible);
    void OverviewAvailableChanged(bool available);
    void AlternateShortcutsChanged();

private:
    struct Dock {
        QString owner;
        uint containmentId = 0;
        QPointer<KWin::Window> window;
        GoshosDock::Policy policy;
        GoshosDock::Environment environment;
        QVariantMap configuration;
        bool holdVisible = false;
        bool urgent = false;
        bool originallyHidden = false;
        qreal originalOpacity = 1;
        bool targetVisible = true;
        QPropertyAnimation *animation = nullptr;
    };
    using DockPtr = std::shared_ptr<Dock>;
    DockPtr ownedDock(const QString &token) const;
    GoshosDock::Environment environment(const Dock &dock) const;
    void refresh();
    void apply(Dock &dock, bool visible);
    void release(const QString &token);
    void cancelEdges();
    void observeOverview();
    void publishOverview();
    void observeKeyboardLayout();
    QHash<QString, DockPtr> m_docks;
    QDBusServiceWatcher *m_watcher = nullptr;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QPointF m_previousPointer;
    bool m_warping = false;
    bool m_overviewVisible = false;
    bool m_overviewAvailable = false;
    QMetaObject::Connection m_overviewConnection;
    QPointer<QObject> m_keyboardLayout;
    QMetaObject::Connection m_layoutConnection;
    QMetaObject::Connection m_keymapConnection;
};
