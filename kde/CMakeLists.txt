# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.22)

project(goshos-dock VERSION 0.1.0 LANGUAGES CXX)

# The native applet is based on plasma-desktop 6.7.5. Keep its actual
# toolchain baseline: lowering these numbers does not make older Plasma
# releases compatible with its QML and private workspace APIs.
set(QT_MIN_VERSION "6.10.0")
set(KF6_MIN_VERSION "6.26.0")
set(PLASMA_MIN_VERSION "6.7.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(ECMQmlModule)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core Concurrent DBus Gui Qml Quick QuickControls2 Widgets Core5Compat
)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Config CoreAddons GlobalAccel I18n KIO JobWidgets Notifications Service Solid WindowSystem
    Kirigami KCMUtils Svg WidgetsAddons
)
find_package(Plasma ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(PlasmaQuick ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(LayerShellQt ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(Qt6GuiPrivate ${QT_MIN_VERSION} CONFIG REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
find_package(PlasmaActivities ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(PlasmaActivitiesStats ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(LibNotificationManager ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(LibTaskManager ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(KSysGuard CONFIG REQUIRED)

add_compile_definitions(QT_NO_CONTEXTLESS_CONNECT)
include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Malcontent REQUIRED IMPORTED_TARGET malcontent-0 gio-unix-2.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)

add_subdirectory(src)
add_subdirectory(kwin)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
