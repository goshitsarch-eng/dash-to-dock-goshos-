# SPDX-License-Identifier: GPL-2.0-or-later
# Native KWin extensions intentionally use its versioned plugin factory IID.
# Rebuild together with every KWin update; loading another ABI is rejected.
find_package(KWin ${PLASMA_MIN_VERSION} REQUIRED CONFIG)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
add_library(goshosdockpolicy STATIC dockpolicy.cpp)
target_link_libraries(goshosdockpolicy PUBLIC Qt::Core Qt::Gui)
target_include_directories(goshosdockpolicy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(goshosdockpolicy PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(goshosdockshortcuts STATIC shortcutnormalization.cpp)
target_link_libraries(goshosdockshortcuts PUBLIC Qt::Gui PkgConfig::XKBCOMMON PRIVATE Qt::GuiPrivate)
target_include_directories(goshosdockshortcuts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(goshosdockshortcuts PROPERTIES POSITION_INDEPENDENT_CODE ON)
kcoreaddons_add_plugin(goshosdockvisibility INSTALL_NAMESPACE "kwin/plugins"
    SOURCES main.cpp dockvisibility.cpp)
target_link_libraries(goshosdockvisibility PRIVATE KWin::kwin Qt::DBus goshosdockpolicy goshosdockshortcuts)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_dockpolicy tst_dockpolicy.cpp)
    target_link_libraries(tst_dockpolicy PRIVATE goshosdockpolicy Qt::Test)
    add_test(NAME dockpolicy COMMAND tst_dockpolicy)
    add_executable(tst_shortcutnormalization tst_shortcutnormalization.cpp)
    target_link_libraries(tst_shortcutnormalization PRIVATE goshosdockshortcuts Qt::Test)
    add_test(NAME shortcutnormalization COMMAND tst_shortcutnormalization)

    # Requires an already running disposable Wayland KWin session. Kept out of
    # ordinary ctest: it creates real windows and must never run on a user's
    # interactive desktop by accident.
    find_package(Qt6 REQUIRED COMPONENTS WaylandClient WaylandClientPrivate)
    find_package(LayerShellQt ${PLASMA_MIN_VERSION} REQUIRED CONFIG)
    find_package(Wayland REQUIRED COMPONENTS Client)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBEI REQUIRED IMPORTED_TARGET libei-1.0)
    add_executable(tst_dockvisibility tst_dockvisibility.cpp)
    target_link_libraries(tst_dockvisibility PRIVATE
        Qt::Test Qt::Quick Qt::DBus Qt::WaylandClientPrivate
        LayerShellQt::Interface Wayland::Client PkgConfig::LIBEI)
endif()
