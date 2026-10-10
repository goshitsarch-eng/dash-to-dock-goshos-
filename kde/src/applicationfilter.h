// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

struct _MctAppFilter;

/** Uses the same system parental-control policy as GNOME Shell. */
class ApplicationFilter final : public QObject
{
    Q_OBJECT
public:
    static ApplicationFilter *instance();
    bool isAllowed(const QString &desktopId) const;
    static bool allows(_MctAppFilter *filter, const QString &desktopFile);
    ~ApplicationFilter() override;
Q_SIGNALS:
    void changed();
private Q_SLOTS:
    void reload();
    void propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &);
private:
    explicit ApplicationFilter(QObject *parent);
    struct Private;
    std::unique_ptr<Private> d;
};
