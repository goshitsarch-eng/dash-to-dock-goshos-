/*
    SPDX-FileCopyrightText: 2026 Goshos Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "docktasksmodel.h"
#include "applicationfilter.h"

#include <QDebug>
#include <QUrl>
#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/taskfilterproxymodel.h>

using TaskManager::AbstractTasksModel;

DockTaskMaskProxy::DockTaskMaskProxy(QObject *parent)
    : QIdentityProxyModel(parent)
{
    connect(ApplicationFilter::instance(), &ApplicationFilter::changed, this, [this]() {
        if (rowCount() > 0) {
            Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0));
        }
    });
}

void DockTaskMaskProxy::setSourceModel(QAbstractItemModel *source)
{
    disconnect(m_sourceDataChanged);
    QIdentityProxyModel::setSourceModel(source);
    if (!source) {
        return;
    }
    m_sourceDataChanged = connect(source, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
            if (!roles.isEmpty() && (roles.contains(AbstractTasksModel::WinIdList) || roles.contains(AbstractTasksModel::IsWindow)
                    || roles.contains(AbstractTasksModel::LauncherUrlWithoutIcon) || roles.contains(AbstractTasksModel::AppId))) {
                // These roles also affect our derived SkipTaskbar value. Notify
                // all roles so the native proxy re-evaluates its filter predicate.
                Q_EMIT dataChanged(mapFromSource(first), mapFromSource(last));
            }
        });
}

QVariant DockTaskMaskProxy::data(const QModelIndex &index, int role) const
{
    if (role == AbstractTasksModel::SkipTaskbar && index.isValid() && index.model() == this) {
        auto identity = QIdentityProxyModel::data(index, AbstractTasksModel::LauncherUrlWithoutIcon).toUrl().toString();
        if (identity.isEmpty()) {
            identity = QIdentityProxyModel::data(index, AbstractTasksModel::AppId).toString();
        }
        if (!ApplicationFilter::instance()->isAllowed(identity)) {
            return true;
        }
        if (QIdentityProxyModel::data(index, AbstractTasksModel::IsWindow).toBool()) {
            const auto ids = QIdentityProxyModel::data(index, AbstractTasksModel::WinIdList).toList();
            for (const QVariant &id : ids) {
                if (m_excludedKeys.contains(id.toString())) {
                    return true;
                }
            }
        }
    }
    return QIdentityProxyModel::data(index, role);
}

QVariantList DockTaskMaskProxy::excludedWindowIds() const
{
    return m_excludedWindowIds;
}

void DockTaskMaskProxy::setExcludedWindowIds(const QVariantList &ids)
{
    if (m_excludedWindowIds == ids) {
        return;
    }
    m_excludedWindowIds = ids;
    m_excludedKeys.clear();
    for (const auto &id : ids) {
        const QString key = id.toString();
        if (!key.isEmpty()) {
            m_excludedKeys.insert(key);
        }
    }
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0));
    }
    Q_EMIT excludedWindowIdsChanged();
}

QModelIndex DockTaskMaskProxy::mapIfaceToSource(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this) {
        return {};
    }
    return mapToSource(index);
}

DockTasksModel::DockTasksModel(QObject *parent)
    : TasksModel(parent)
    , m_filter(findChild<TaskManager::TaskFilterProxyModel *>(QString(), Qt::FindDirectChildrenOnly))
    , m_mask(new DockTaskMaskProxy(this))
{
    if (!m_filter) {
        qCritical() << "Goshos Dock: the native task filter is unavailable in this Plasma version";
        return;
    }
    m_mask->setSourceModel(m_filter->sourceModel());
    m_filter->setSourceModel(m_mask);
    connect(m_filter, &TaskManager::TaskFilterProxyModel::demandingAttentionSkipsFiltersChanged,
        this, &DockTasksModel::demandingAttentionSkipsFiltersChanged);
    connect(m_mask, &DockTaskMaskProxy::excludedWindowIdsChanged, this, &DockTasksModel::excludedWindowIdsChanged);
}

bool DockTasksModel::demandingAttentionSkipsFilters() const
{
    return m_filter && m_filter->demandingAttentionSkipsFilters();
}

void DockTasksModel::setDemandingAttentionSkipsFilters(bool enabled)
{
    if (m_filter) {
        m_filter->setDemandingAttentionSkipsFilters(enabled);
    }
}

QVariantList DockTasksModel::excludedWindowIds() const
{
    return m_mask->excludedWindowIds();
}

void DockTasksModel::setExcludedWindowIds(const QVariantList &ids)
{
    m_mask->setExcludedWindowIds(ids);
}

#include "moc_docktasksmodel.cpp"
