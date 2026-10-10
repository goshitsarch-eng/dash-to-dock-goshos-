# SPDX-License-Identifier: GPL-2.0-or-later

add_library(goshosapplicationfilter STATIC applicationfilter.cpp)
set_target_properties(goshosapplicationfilter PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(goshosapplicationfilter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(goshosapplicationfilter PUBLIC Qt::Core Qt::DBus Qt::Concurrent KF6::Service PkgConfig::Malcontent)

plasma_add_applet(org.gosh.goshosdock
    QML_SOURCES
        qml/AudioStream.qml
        qml/ConfigAppearance.qml
        qml/ConfigBehavior.qml
        qml/ConfigDock.qml
        qml/config.qml
        qml/ContextMenu.qml
        qml/ContextPreviewDialog.qml
        qml/DockExtras.qml
        qml/DockIndicator.qml
        qml/DockTaskViewport.qml
        qml/GroupDialog.qml
        qml/GroupExpanderOverlay.qml
        qml/IntegrationNotice.qml
        qml/main.qml
        qml/MagnifiedIcon.qml
        qml/LocationWindowsPopup.qml
        qml/LauncherContextMenu.qml
        qml/MouseHandler.qml
        qml/PipeWireThumbnail.qml
        qml/PlayerController.qml
        qml/PulseAudio.qml
        qml/ScrollableTextWrapper.qml
        qml/StackPopup.qml
        qml/TaskBadgeOverlay.qml
        qml/TaskList.qml
        qml/TaskProgressOverlay.qml
        qml/Task.qml
        qml/ToolTipDelegate.qml
        qml/ToolTipInstance.qml
        qml/ToolTipWindowMouseArea.qml
        qml/code/LayoutMetrics.js
        qml/code/TaskTools.js
    CPP_SOURCES
        backend.cpp
        dockcontroller.cpp
        panellayout.cpp
        applicationactions.cpp
        docktasksmodel.cpp
        locationbackend.cpp
        globalshortcuts.cpp
        quicklistbackend.cpp
        stackbackend.cpp
        remotemounts.cpp
        smartlauncherbackend.cpp
        smartlauncheritem.cpp
    RESOURCES
        main.xml
    GENERATE_APPLET_CLASS
)

# ECM flattens QML_SOURCES by default. These helpers are imported relative to
# their source files, so preserve the code/ directory in the embedded module.
ecm_target_qml_sources(org.gosh.goshosdock PATH code SOURCES
    qml/code/DockActions.js
    qml/code/DockGeometry.js
    qml/code/StackLogic.js
    qml/code/ConfigSnapshot.js
)

kconfig_add_kcfg_files(org.gosh.goshosdock
    ${CMAKE_CURRENT_SOURCE_DIR}/kactivitymanagerd_plugins_settings.kcfgc
)

target_include_directories(org.gosh.goshosdock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(org.gosh.goshosdock PRIVATE
    TRANSLATION_DOMAIN="plasma_applet_org.gosh.goshosdock"
)

ecm_qt_declare_logging_category(org.gosh.goshosdock
    HEADER log_settings.h
    IDENTIFIER TASKMANAGER_DEBUG
    CATEGORY_NAME org.gosh.goshosdock)

target_link_libraries(org.gosh.goshosdock PRIVATE
                      goshosapplicationfilter
                      PkgConfig::GIO
                      Qt::Core
                      Qt::DBus
                      Qt::Gui
                      Qt6::GuiPrivate
                      Wayland::Client
                      Plasma::PlasmaQuick
                      LayerShellQt::Interface
                      Qt::Qml
                      Qt::Quick
                      Qt::Widgets
                      Plasma::Activities
                      Plasma::ActivitiesStats
                      KF6::ConfigGui
                      KF6::I18n
                      KF6::GlobalAccel
                      KF6::KIOCore
                      KF6::KIOGui
                      KF6::KIOWidgets
                      KF6::KIOFileWidgets # KFilePlacesModel
                      KF6::Notifications # KNotificationJobUiDelegate
                      Plasma::Plasma
                      KSysGuard::ProcessCore
                      KF6::Service
                      KF6::Solid
                      KF6::WidgetsAddons
                      KF6::WindowSystem
                      KF6::JobWidgets
                      PW::LibTaskManager
                      PW::LibNotificationManager)
