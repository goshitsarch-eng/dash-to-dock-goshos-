/*
    SPDX-FileCopyrightText: 2026 Goshos Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "docktasksmodel.h"
#include "applicationfilter.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFile>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWindow>
#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/taskfilterproxymodel.h>

using TaskManager::AbstractTasksModel;

class FakeTaskSource : public AbstractTasksModel
{
    Q_OBJECT
public:
    struct Task {
        QVariantList ids;
        bool window = true;
        bool launcher = false;
        bool urgent = false;
        bool skipTaskbar = false;
        QVariantList desktops = {};
        QStringList activities = {};
    };

    QList<Task> tasks;
    QString lastRequest;
    QPersistentModelIndex lastIndex;
    QList<QUrl> lastUrls;
    QRect lastGeometry;
    QObject *lastDelegate = nullptr;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : tasks.size(); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= tasks.size()) return {};
        const auto &task = tasks.at(index.row());
        switch (role) {
        case Qt::DisplayRole: return QStringLiteral("Task %1").arg(index.row());
        case IsWindow: return task.window;
        case IsLauncher: return task.launcher;
        case IsStartup: return !task.window && !task.launcher;
        case WinIdList: return task.ids;
        case IsDemandingAttention: return task.urgent;
        case SkipTaskbar: return task.skipTaskbar;
        case VirtualDesktops: return task.desktops;
        case Activities: return task.activities;
        default: return AbstractTasksModel::data(index, role);
        }
    }
    void replaceIds(int row, const QVariantList &ids)
    {
        tasks[row].ids = ids;
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {WinIdList});
    }
    void append(const Task &task)
    {
        beginInsertRows({}, tasks.size(), tasks.size());
        tasks.append(task);
        endInsertRows();
    }
    void removeFirst()
    {
        beginRemoveRows({}, 0, 0);
        tasks.removeFirst();
        endRemoveRows();
    }
    void requestActivate(const QModelIndex &index) override { lastRequest = QStringLiteral("activate"); lastIndex = index; }
    void requestClose(const QModelIndex &index) override { lastRequest = QStringLiteral("close"); lastIndex = index; }
    void requestNewInstance(const QModelIndex &index) override { lastRequest = QStringLiteral("new"); lastIndex = index; }
    void requestOpenUrls(const QModelIndex &index, const QList<QUrl> &urls) override
    {
        lastRequest = QStringLiteral("open"); lastIndex = index; lastUrls = urls;
    }
    void requestPublishDelegateGeometry(const QModelIndex &index, const QRect &geometry, QObject *delegate) override
    {
        lastRequest = QStringLiteral("geometry"); lastIndex = index; lastGeometry = geometry; lastDelegate = delegate;
    }
};

class DockTasksModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QTRY_VERIFY(ApplicationFilter::instance()->isAllowed(QString()));
    }

    void exclusionsPreserveRowsAndOnlyMaskWindows()
    {
        FakeTaskSource source;
        source.tasks = {
            {{42}, true, false, false, false, {}, {}},
            {{42}, false, true, false, false, {}, {}},
            {{42}, false, false, false, false, {}, {}},
            {{77}, true, false, false, false, {}, {}},
            {{88}, true, false, false, true, {}, {}},
        };
        DockTaskMaskProxy mask;
        mask.setSourceModel(&source);
        QAbstractItemModelTester tester(&mask, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy removed(&mask, &QAbstractItemModel::rowsRemoved);
        QPersistentModelIndex preserved(mask.index(3, 0));
        TaskManager::TaskFilterProxyModel filter;
        filter.setSourceModel(&mask);
        QCOMPARE(filter.rowCount(), 4);

        // Numeric X11 IDs survive QVariant type differences in QML bindings.
        mask.setExcludedWindowIds({QStringLiteral("42")});
        QCOMPARE(mask.rowCount(), 5);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(preserved.row(), 3);
        QCOMPARE(filter.rowCount(), 3);
        QVERIFY(mask.index(0, 0).data(AbstractTasksModel::SkipTaskbar).toBool());
        QVERIFY(!mask.index(1, 0).data(AbstractTasksModel::SkipTaskbar).toBool());
        QVERIFY(!mask.index(2, 0).data(AbstractTasksModel::SkipTaskbar).toBool());
        QVERIFY(mask.index(4, 0).data(AbstractTasksModel::SkipTaskbar).toBool());
        for (int row = 0; row < mask.rowCount(); ++row) {
            QCOMPARE(mask.mapToSource(mask.index(row, 0)), source.index(row, 0));
        }
        mask.setExcludedWindowIds({});
        QCOMPARE(filter.rowCount(), 4);
        QVERIFY(mask.index(4, 0).data(AbstractTasksModel::SkipTaskbar).toBool());
    }

    void changingWindowIdsAndSourceRowsReevaluatesMask()
    {
        FakeTaskSource source;
        source.tasks = {{{QStringLiteral("window-a")}}, {{QStringLiteral("window-b")}}};
        DockTaskMaskProxy mask;
        mask.setSourceModel(&source);
        mask.setExcludedWindowIds({QStringLiteral("window-a")});
        TaskManager::TaskFilterProxyModel filter;
        filter.setSourceModel(&mask);
        QCOMPARE(filter.rowCount(), 1);
        source.replaceIds(1, {QStringLiteral("window-a")});
        QCOMPARE(filter.rowCount(), 0);
        source.append({{QStringLiteral("window-c")}});
        QCOMPARE(mask.rowCount(), 3);
        QCOMPARE(filter.rowCount(), 1);
        source.removeFirst();
        QCOMPARE(mask.rowCount(), 2);
        QCOMPARE(mask.mapToSource(mask.index(1, 0)), source.index(1, 0));
        QCOMPARE(filter.rowCount(), 1);
    }

    void requestsReachTheOriginalTaskThroughNativeFilter()
    {
        FakeTaskSource source;
        source.tasks = {{{1}}, {{2}}, {{3}}};
        DockTaskMaskProxy mask;
        mask.setSourceModel(&source);
        mask.setExcludedWindowIds({1});
        TaskManager::TaskFilterProxyModel filter;
        filter.setSourceModel(&mask);
        const auto secondVisible = filter.index(1, 0);
        filter.requestActivate(secondVisible);
        QCOMPARE(source.lastRequest, QStringLiteral("activate"));
        QCOMPARE(source.lastIndex, source.index(2, 0));
        filter.requestClose(secondVisible);
        QCOMPARE(source.lastRequest, QStringLiteral("close"));
        QCOMPARE(source.lastIndex, source.index(2, 0));
        filter.requestNewInstance(secondVisible);
        QCOMPARE(source.lastRequest, QStringLiteral("new"));
        const QList<QUrl> urls{QUrl(QStringLiteral("file:///tmp/document.txt"))};
        filter.requestOpenUrls(secondVisible, urls);
        QCOMPARE(source.lastUrls, urls);
        QObject delegate;
        filter.requestPublishDelegateGeometry(secondVisible, QRect(10, 20, 48, 48), &delegate);
        QCOMPARE(source.lastIndex, source.index(2, 0));
        QCOMPARE(source.lastGeometry, QRect(10, 20, 48, 48));
        QCOMPARE(source.lastDelegate, &delegate);
    }

    void strictUrgencyAndLocationExclusionsRemainIndependent()
    {
        FakeTaskSource source;
        source.tasks = {
            {{1}, true, false, true, false, {QStringLiteral("elsewhere")}, {QStringLiteral("other-activity")}},
            {{2}, true, false, false, false, {QStringLiteral("here")}, {QStringLiteral("current-activity")}},
        };
        DockTaskMaskProxy mask;
        mask.setSourceModel(&source);
        TaskManager::TaskFilterProxyModel filter;
        filter.setSourceModel(&mask);
        filter.setVirtualDesktop(QStringLiteral("here"));
        filter.setFilterByVirtualDesktop(true);
        filter.setActivity(QStringLiteral("current-activity"));
        filter.setFilterByActivity(true);
        QCOMPARE(filter.rowCount(), 2); // Urgent windows bypass desktop/activity filters by default.
        filter.setDemandingAttentionSkipsFilters(false);
        QCOMPARE(filter.rowCount(), 1);
        filter.setDemandingAttentionSkipsFilters(true);
        QCOMPARE(filter.rowCount(), 2);
        mask.setExcludedWindowIds({1});
        QCOMPARE(filter.rowCount(), 1); // An urgent excluded location never leaks back into app groups.
        mask.setExcludedWindowIds({});
        QCOMPARE(filter.rowCount(), 2);
    }

    void nativeTasksHierarchyRetainsLaunchersAndSortMap()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList launchers;
        for (int number = 0; number < 2; ++number) {
            const auto path = directory.filePath(QStringLiteral("dock-test-%1.desktop").arg(number));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Dock Test %1\nExec=true\nIcon=application-x-executable\n").arg(number).toUtf8());
            file.close();
            launchers.append(QUrl::fromLocalFile(path).toString());
        }
        TaskManager::TasksModel baseline;
        baseline.classBegin();
        baseline.setSortMode(TaskManager::TasksModel::SortManual);
        baseline.setSeparateLaunchers(true);
        baseline.setLaunchInPlace(true);
        baseline.setLauncherList(launchers);
        baseline.componentComplete();
        DockTasksModel model;
        model.classBegin();
        model.setSortMode(TaskManager::TasksModel::SortManual);
        model.setSeparateLaunchers(true);
        model.setLaunchInPlace(true);
        model.setLauncherList(launchers);
        model.componentComplete();
        auto *filter = model.findChild<TaskManager::TaskFilterProxyModel *>();
        auto *mask = model.findChild<DockTaskMaskProxy *>();
        QVERIFY(filter);
        QVERIFY(mask);
        QCOMPARE(filter->sourceModel(), mask);
        QVERIFY(mask->sourceModel());
        QVERIFY(QString::fromLatin1(mask->sourceModel()->metaObject()->className()).contains(QStringLiteral("ConcatenateTasksProxyModel")));
        QCOMPARE(model.launcherList(), launchers);
        QVERIFY(mask->sourceModel()->rowCount() >= 2);
        QCOMPARE(mask->rowCount(), mask->sourceModel()->rowCount());
        if (mask->columnCount() == 0) {
            QSKIP("Native window tasks need an X11/Wayland session; run this case in the Plasma integration session.");
        }
        QVERIFY(model.sourceModel());
        const auto visibleLaunchers = [&model]() {
            int count = 0;
            for (int row = 0; row < model.rowCount(); ++row) {
                count += model.index(row, 0).data(AbstractTasksModel::IsLauncher).toBool();
            }
            return count;
        };
        QTRY_COMPARE(visibleLaunchers(), 2);
        QCOMPARE(model.rowCount(), baseline.rowCount());
        QSignalSpy urgency(&model, &DockTasksModel::demandingAttentionSkipsFiltersChanged);
        model.setDemandingAttentionSkipsFilters(false);
        QVERIFY(!filter->demandingAttentionSkipsFilters());
        QCOMPARE(urgency.size(), 1);
        model.setExcludedWindowIds({1, 2, QStringLiteral("unrelated-window")});
        QCOMPARE(visibleLaunchers(), 2);
        QCOMPARE(mask->rowCount(), mask->sourceModel()->rowCount());
        QVERIFY(model.index(0, 0).data(AbstractTasksModel::IsLauncher).toBool());
        QVERIFY(model.move(0, 1));
        model.syncLaunchers();
        QCOMPARE(model.launcherList(), (QStringList{launchers.at(1), launchers.at(0)}));
    }

    void excludedRunningWindowRestoresItsPinnedLauncher()
    {
        if (!qEnvironmentVariableIsSet("GOSHOS_DOCK_INTEGRATION")) {
            QSKIP("Creates a real window; enable only in the disposable Plasma integration session.");
        }
        const QString launcher = QStringLiteral("applications:org.gosh.goshosdock-task-tests.desktop");
        DockTasksModel model;
        model.classBegin();
        model.setGroupMode(TaskManager::TasksModel::GroupDisabled);
        model.setSortMode(TaskManager::TasksModel::SortManual);
        model.setLaunchInPlace(true);
        model.setHideActivatedLaunchers(true);
        model.componentComplete();
        model.setLauncherList({launcher});

        const auto ownWindow = [&model]() {
            for (int row = 0; row < model.rowCount(); ++row) {
                const auto index = model.index(row, 0);
                if (index.data(AbstractTasksModel::IsWindow).toBool()
                    && index.data(AbstractTasksModel::AppPid).toLongLong() == QCoreApplication::applicationPid()) {
                    return index;
                }
            }
            return QModelIndex();
        };
        const auto pinned = [&model, &launcher]() {
            for (int row = 0; row < model.rowCount(); ++row) {
                const auto index = model.index(row, 0);
                if (index.data(AbstractTasksModel::IsLauncher).toBool()
                    && index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl().toString() == launcher) {
                    return index;
                }
            }
            return QModelIndex();
        };
        QTRY_VERIFY(pinned().isValid());
        QWindow window;
        window.setTitle(QStringLiteral("Goshos Dock location isolation test"));
        window.resize(300, 180);
        window.show();
        QTRY_VERIFY(window.isExposed());
        QTRY_VERIFY(ownWindow().isValid());
        QTRY_VERIFY(!pinned().isValid());
        const QVariantList ids = ownWindow().data(AbstractTasksModel::WinIdList).toList();
        QVERIFY(!ids.isEmpty());
        model.setExcludedWindowIds(ids);
        QTRY_VERIFY(!ownWindow().isValid());
        QTRY_VERIFY(pinned().isValid());
        model.setExcludedWindowIds({});
        QTRY_VERIFY(ownWindow().isValid());
        QTRY_VERIFY(!pinned().isValid());
        window.close();
        QTRY_VERIFY(!ownWindow().isValid());
        QTRY_VERIFY(pinned().isValid());
    }
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.gosh.goshosdock-task-tests"));
    DockTasksModelTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_docktasksmodel.moc"
