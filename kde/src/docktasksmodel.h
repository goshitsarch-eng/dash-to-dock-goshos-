/*
    SPDX-FileCopyrightText: 2026 Goshos Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QIdentityProxyModel>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <qqmlregistration.h>
#include <taskmanager/abstracttasksproxymodeliface.h>
#include <taskmanager/tasksmodel.h>

namespace TaskManager
{
class TaskFilterProxyModel;
}

/**
 * Adds the location exclusion flag without removing or reordering source rows.
 * TasksModel's manual sort map uses the original concatenated row numbers, so
 * a filtering proxy inserted at this point would corrupt reordering/grouping.
 */
class DockTaskMaskProxy : public QIdentityProxyModel, public TaskManager::AbstractTasksProxyModelIface
{
    Q_OBJECT

public:
    explicit DockTaskMaskProxy(QObject *parent = nullptr);
    void setSourceModel(QAbstractItemModel *source) override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariantList excludedWindowIds() const;
    void setExcludedWindowIds(const QVariantList &ids);

Q_SIGNALS:
    void excludedWindowIdsChanged();

protected:
    QModelIndex mapIfaceToSource(const QModelIndex &index) const override;

private:
    QVariantList m_excludedWindowIds;
    QSet<QString> m_excludedKeys;
    QMetaObject::Connection m_sourceDataChanged;
};

/** Native tasks model with the GNOME dock's urgency and location policies. */
class DockTasksModel : public TaskManager::TasksModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool demandingAttentionSkipsFilters READ demandingAttentionSkipsFilters WRITE setDemandingAttentionSkipsFilters
                   NOTIFY demandingAttentionSkipsFiltersChanged)
    Q_PROPERTY(QVariantList excludedWindowIds READ excludedWindowIds WRITE setExcludedWindowIds NOTIFY excludedWindowIdsChanged)

public:
    explicit DockTasksModel(QObject *parent = nullptr);
    bool demandingAttentionSkipsFilters() const;
    void setDemandingAttentionSkipsFilters(bool enabled);
    QVariantList excludedWindowIds() const;
    void setExcludedWindowIds(const QVariantList &ids);

Q_SIGNALS:
    void demandingAttentionSkipsFiltersChanged();
    void excludedWindowIdsChanged();

private:
    QPointer<TaskManager::TaskFilterProxyModel> m_filter;
    DockTaskMaskProxy *m_mask;
};
