/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <qqmlregistration.h>

class QScreen;

// Configure only a dedicated panel whose identity was supplied by DockController.
class PanelLayout : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(uint panelId READ panelId WRITE setPanelId NOTIFY identityChanged)
    Q_PROPERTY(uint appletId READ appletId WRITE setAppletId NOTIFY identityChanged)
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration NOTIFY configurationChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)

public:
    explicit PanelLayout(QObject *parent = nullptr);
    uint panelId() const { return m_panelId; }
    void setPanelId(uint id);
    uint appletId() const { return m_appletId; }
    void setAppletId(uint id);
    QVariantMap configuration() const { return m_configuration; }
    void setConfiguration(const QVariantMap &configuration);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool active() const { return m_active; }
    QString error() const { return m_error; }

    static QString scriptFor(const QVariantMap &request);

Q_SIGNALS:
    void identityChanged();
    void configurationChanged();
    void enabledChanged();
    void statusChanged();

private:
    void schedule();
    void apply();
    void watchScreen(QScreen *screen);
    void setStatus(bool active, const QString &error);

    uint m_panelId = 0;
    uint m_appletId = 0;
    QVariantMap m_configuration;
    QTimer m_debounce;
    bool m_enabled = true;
    bool m_active = false;
    bool m_inFlight = false;
    bool m_pending = false;
    QString m_error;
};
