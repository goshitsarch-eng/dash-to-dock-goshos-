/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QRect>
#include <qqmlregistration.h>

/** An applet-local receiver for process-wide, user-configurable KDE shortcuts. */
class GlobalShortcuts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QRect screenGeometry READ screenGeometry WRITE setScreenGeometry NOTIFY screenGeometryChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool allOutputs READ allOutputs WRITE setAllOutputs NOTIFY allOutputsChanged)

public:
    explicit GlobalShortcuts(QObject *parent = nullptr);
    ~GlobalShortcuts() override;

    bool isEnabled() const;
    void setEnabled(bool enabled);
    QRect screenGeometry() const;
    void setScreenGeometry(const QRect &geometry);
    QString error() const { return m_error; }
    // Shared registry status; not a user-writable QML property.
    void setError(const QString &error);
    Q_INVOKABLE bool nativeActivationEnabled(int index) const;
    bool allOutputs() const { return m_allOutputs; }
    void setAllOutputs(bool allOutputs);

Q_SIGNALS:
    // Index is zero-based, including index 9 for the physical 0 key.
    void taskRequested(int index, bool launch, bool shifted);
    void overlayRequested();
    void enabledChanged();
    void screenGeometryChanged();
    void errorChanged();
    void allOutputsChanged();

private:
    bool m_enabled = true;
    QRect m_screenGeometry;
    QString m_error;
    bool m_allOutputs = false;
};
