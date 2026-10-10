# SPDX-License-Identifier: GPL-2.0-or-later
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test QuickTest)

add_executable(tst_qmlcomponents tst_qmlcomponents.cpp)
target_link_libraries(tst_qmlcomponents PRIVATE Qt::Widgets Qt::Qml Qt::Test)
target_compile_definitions(tst_qmlcomponents PRIVATE APPLET_LIBRARY_PATH="$<TARGET_FILE:org.gosh.goshosdock>")
add_dependencies(tst_qmlcomponents org.gosh.goshosdock)
add_test(NAME qmlcomponents COMMAND tst_qmlcomponents)
set_tests_properties(qmlcomponents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")

add_executable(tst_stackbackend tst_stackbackend.cpp ../src/stackbackend.cpp ../src/remotemounts.cpp)
target_include_directories(tst_stackbackend PRIVATE ../src)
target_link_libraries(tst_stackbackend PRIVATE goshosapplicationfilter PkgConfig::GIO
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Test
    KF6::ConfigCore KF6::CoreAddons KF6::I18n KF6::Service KF6::Solid KF6::WidgetsAddons
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::KIOFileWidgets KF6::JobWidgets
    Plasma::Activities Plasma::ActivitiesStats)
add_test(NAME stackbackend COMMAND tst_stackbackend)
set_tests_properties(stackbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_quicklistbackend tst_quicklistbackend.cpp ../src/quicklistbackend.cpp)
target_include_directories(tst_quicklistbackend PRIVATE ../src ${CMAKE_CURRENT_BINARY_DIR})
ecm_qt_declare_logging_category(tst_quicklistbackend
    HEADER log_settings.h IDENTIFIER TASKMANAGER_DEBUG
    CATEGORY_NAME org.gosh.goshosdock.tests)
target_link_libraries(tst_quicklistbackend PRIVATE
    Qt::Core Qt::DBus Qt::Gui Qt::Widgets Qt::Qml Qt::Test
    KF6::ConfigCore KF6::Service KF6::CoreAddons)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME quicklistbackend COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_quicklistbackend>)
set_tests_properties(quicklistbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_program(QMLTESTRUNNER NAMES qmltestrunner qmltestrunner6
    HINTS ${QT6_INSTALL_PREFIX}/lib/qt6/bin ${QT6_INSTALL_PREFIX}/bin REQUIRED)
add_test(NAME dockgeometry COMMAND ${QMLTESTRUNNER} -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_DockGeometry.qml)
set_tests_properties(dockgeometry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME package COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check-package.py)
find_program(NODE_EXECUTABLE NAMES node nodejs REQUIRED)
add_test(NAME docklogic COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_CURRENT_SOURCE_DIR}/dock-logic.test.cjs)

add_executable(tst_globalshortcuts tst_globalshortcuts.cpp ../src/globalshortcuts.cpp)
target_include_directories(tst_globalshortcuts PRIVATE ../src)
target_link_libraries(tst_globalshortcuts PRIVATE
    Qt::Core Qt::DBus Qt::Gui Qt::Widgets Qt::Qml Qt::Test KF6::GlobalAccel KF6::I18n KF6::ConfigCore)
add_test(NAME globalshortcuts COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_globalshortcuts>)
set_tests_properties(globalshortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_notifications tst_notifications.cpp
    ../src/smartlauncherbackend.cpp ../src/smartlauncheritem.cpp)
target_include_directories(tst_notifications PRIVATE ../src ${CMAKE_CURRENT_BINARY_DIR})
ecm_qt_declare_logging_category(tst_notifications
    HEADER log_settings.h IDENTIFIER TASKMANAGER_DEBUG
    CATEGORY_NAME org.gosh.goshosdock.tests)
target_link_libraries(tst_notifications PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::DBus Qt::Test
    KF6::ConfigCore KF6::Service KF6::CoreAddons PW::LibNotificationManager)
add_test(NAME notifications COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_notifications>)
set_tests_properties(notifications PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_docktasksmodel tst_docktasksmodel.cpp ../src/docktasksmodel.cpp)
target_include_directories(tst_docktasksmodel PRIVATE ../src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tst_docktasksmodel PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Test PW::LibTaskManager goshosapplicationfilter)
add_test(NAME docktasksmodel COMMAND tst_docktasksmodel)
set_tests_properties(docktasksmodel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_applicationactions tst_applicationactions.cpp ../src/applicationactions.cpp)
target_include_directories(tst_applicationactions PRIVATE ../src)
target_link_libraries(tst_applicationactions PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::DBus Qt::Test
    KF6::Service KF6::ConfigCore KF6::CoreAddons KF6::KIOCore KF6::KIOGui
    KF6::KIOWidgets KF6::I18n KF6::Notifications KF6::JobWidgets)
add_test(NAME applicationactions COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_applicationactions>)
set_tests_properties(applicationactions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME migration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_migration.py)

add_executable(tst_applicationfilter tst_applicationfilter.cpp)
target_link_libraries(tst_applicationfilter PRIVATE goshosapplicationfilter Qt::Test)
add_test(NAME applicationfilter COMMAND tst_applicationfilter)

add_executable(tst_panellayout tst_panellayout.cpp ../src/panellayout.cpp)
target_include_directories(tst_panellayout PRIVATE ../src)
target_link_libraries(tst_panellayout PRIVATE Qt::Core Qt::Gui Qt::Qml Qt::DBus Qt::Test)
add_test(NAME panellayout COMMAND tst_panellayout)
set_tests_properties(panellayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_locationbackend tst_locationbackend.cpp ../src/locationbackend.cpp)
target_include_directories(tst_locationbackend PRIVATE ../src)
target_link_libraries(tst_locationbackend PRIVATE Qt::Core Qt::DBus Qt::Qml Qt::Test)
add_test(NAME locationbackend COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_locationbackend>)

add_executable(tst_dockcontroller tst_dockcontroller.cpp ../src/dockcontroller.cpp)
target_include_directories(tst_dockcontroller PRIVATE ../src)
target_link_libraries(tst_dockcontroller PRIVATE Qt::Core Qt::DBus Qt::Gui Qt::Widgets Qt::Qml Qt::Quick Qt::Test
    Qt6::GuiPrivate Plasma::Plasma Plasma::PlasmaQuick KF6::ConfigCore Wayland::Client LayerShellQt::Interface)
add_test(NAME dockcontroller COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_dockcontroller>)
set_tests_properties(dockcontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_remotemounts tst_remotemounts.cpp ../src/remotemounts.cpp)
target_include_directories(tst_remotemounts PRIVATE ../src)
target_link_libraries(tst_remotemounts PRIVATE Qt::Core Qt::Widgets Qt::Test KF6::WidgetsAddons PkgConfig::GIO)
add_test(NAME remotemounts COMMAND tst_remotemounts)
add_test(NAME remotemounts_without_glib_dispatcher COMMAND tst_remotemounts)
set_tests_properties(remotemounts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(remotemounts_without_glib_dispatcher PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_NO_GLIB=1")
