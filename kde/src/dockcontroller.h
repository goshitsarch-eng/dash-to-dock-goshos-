/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KConfigGroup>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantMap>
#include <qqmlregistration.h>

namespace Plasma { class Applet; class Containment; }
namespace LayerShellQt { class Window; }

class DockController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration NOTIFY configurationChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool holdVisible READ holdVisible WRITE setHoldVisible NOTIFY holdVisibleChanged)
    Q_PROPERTY(bool urgent READ urgent WRITE setUrgent NOTIFY urgentChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool overviewVisible READ overviewVisible NOTIFY overviewVisibleChanged)
    Q_PROPERTY(bool overviewAvailable READ overviewAvailable NOTIFY overviewAvailableChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(uint panelId READ panelId NOTIFY identityChanged)
    Q_PROPERTY(uint appletId READ appletId NOTIFY identityChanged)

public:
    explicit DockController(QObject *parent = nullptr);
    ~DockController() override;

    QQuickItem *target() const { return m_target; }
    QVariantMap configuration() const { return m_configuration; }
    bool enabled() const { return m_enabled; }
    bool holdVisible() const { return m_holdVisible; }
    bool urgent() const { return m_urgent; }
    bool active() const { return m_active; }
    bool overviewVisible() const { return m_overviewVisible; }
    bool overviewAvailable() const { return m_overviewAvailable; }
    QString error() const { return m_error; }
    uint panelId() const { return m_panelId; }
    uint appletId() const { return m_appletId; }
    void setTarget(QQuickItem *target);
    void setConfiguration(const QVariantMap &configuration);
    void setEnabled(bool enabled);
    void setHoldVisible(bool holdVisible);
    void setUrgent(bool urgent);
    Q_INVOKABLE void hideOverview();
    Q_INVOKABLE void toggleOverview();

Q_SIGNALS:
    void targetChanged();
    void configurationChanged();
    void enabledChanged();
    void holdVisibleChanged();
    void urgentChanged();
    void activeChanged();
    void overviewVisibleChanged();
    void overviewAvailableChanged();
    void errorChanged();
    void identityChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private Q_SLOTS:
    void setOverviewVisible(bool visible);
    void setOverviewAvailable(bool available);
    void panelVisibilityModeChanged();

private:
    friend class DockControllerTest;
    void schedule();
    void synchronize();
    void setError(const QString &error);
    void setActive(bool active);
    void refreshOverview();
    bool resolveTarget();
    void registerDock(uint surfaceId);
    void updateDock();
    void applyPanelMode();
    void raiseManagedPanel();
    void suppressPanelBackground();
    void deactivate();
    bool savePanelMode();
    QVariantMap dockConfiguration() const;
    static QString panelModeName(int rawMode);

    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_window;
    QPointer<Plasma::Applet> m_applet;
    QPointer<Plasma::Containment> m_containment;
    QVariantMap m_configuration;
    QTimer m_schedule;
    QString m_token;
    QString m_error;
    QString m_appliedMode;
    uint m_panelId = 0;
    uint m_appletId = 0;
    bool m_enabled = false;
    bool m_holdVisible = false;
    bool m_urgent = false;
    bool m_active = false;
    bool m_overviewVisible = false;
    bool m_overviewAvailable = false;
    quint64 m_overviewRevision = 0;
    quint64 m_overviewAvailabilityRevision = 0;
    bool m_registering = false;
    bool m_registrationPending = false;
    int m_registrationAttempts = 0;
    bool m_modePending = false;
    quint64 m_generation = 0;
    bool m_savedModeValid = false;
    uint m_savedPanelId = 0;
    int m_savedMode = 0;
    KConfigGroup m_backup;
    QPointer<LayerShellQt::Window> m_layerWindow;
    QMetaObject::Connection m_layerConnection;
    QMetaObject::Connection m_visibilityConnection;
    int m_originalLayer = 0;
    QPointer<Plasma::Containment> m_backgroundContainment;
    QMetaObject::Connection m_backgroundConnection;
    int m_originalBackgroundHints = 0;
};
