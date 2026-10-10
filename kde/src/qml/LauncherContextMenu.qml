/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import org.kde.taskmanager as TaskManager
import plasma.applet.org.gosh.goshosdock as Dock

ContextMenu {
    id: menu
    required property url launcherUrl
    rootTask: null
    detachedLauncher: true
    taskModel: launcherTasks
    modelIndex: modelReady ? launcherTasks.makePersistentModelIndex(launcherRow) : undefined
    launcherUpdating: launcherState.updating

    // This model is private to this menu. Recent applications obtain the same
    // launcher metadata/actions without changing the dock's pinned launchers.
    readonly property TaskManager.TasksModel launcherTasks: TaskManager.TasksModel {
        launcherList: [menu.launcherUrl.toString()]
        separateLaunchers: true
        hideActivatedLaunchers: false
        sortMode: TaskManager.TasksModel.SortManual
    }
    readonly property int launcherRow: {
        if (!modelReady) return -1;
        for (let row = 0; row < launcherTasks.count; ++row) {
            const index = launcherTasks.makeModelIndex(row);
            if (launcherTasks.data(index, TaskManager.AbstractTasksModel.IsLauncher)) return row;
        }
        return -1;
    }
    readonly property Dock.SmartLauncherItem launcherState: Dock.SmartLauncherItem {
        launcherUrl: menu.launcherUrl
    }
}
