cmake_minimum_required(VERSION 3.16)
project(bmw-e46-dash VERSION 1.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick SerialBus Network)
find_package(Qt6 OPTIONAL_COMPONENTS Bluetooth)

# Only data/track-db.json goes through plain qt_add_resources now — QML/JS
# sources moved to qt_add_qml_module below (see near the bottom), which
# compiles them to bytecode via qmlcachegen at build time instead of parsing
# source text on every launch.
qt_add_resources(RESOURCES resources.qrc)

add_executable(bmw-e46-dash
    main.cpp
    src/logging.cpp
    src/icanprovider.h
    src/candatamodel.cpp
    src/dashconfig.cpp
    src/mockcanprovider.cpp
    src/realcanprovider.cpp
    src/raceboxdata.h
    src/iraceprovider.h
    src/raceboxmodel.cpp
    src/mockraceboxprovider.cpp
    src/raceboxprovider.cpp
    src/sessionmodel.cpp
    src/trackmodel.cpp
    src/logbuffermodel.cpp
    src/devicestatsmodel.cpp
    src/updatemodel.cpp
    src/networkmodel.cpp
    src/sdnotify.cpp
    ${RESOURCES}
)

target_include_directories(bmw-e46-dash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(bmw-e46-dash PRIVATE DASH_VERSION="${PROJECT_VERSION}")

# PRIVATE (keyword signature) is required here: qt_add_qml_module() below also
# calls target_link_libraries() on this same target, and CMake forbids mixing
# plain and keyword signatures for one target.
target_link_libraries(bmw-e46-dash PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::SerialBus
    Qt6::Network
)

# :/qt/qml/ is the modern default QML module resource prefix (silences the
# QTP0001 dev warning by picking it explicitly rather than leaving it implicit).
qt_policy(SET QTP0001 NEW)
# OLD: qml/settings/*.qml files are reached via relative "file" imports
# (e.g. `import "settings"` in StatusBar.qml), not module imports, so the
# nested-submodule qmldir QTP0004 would otherwise ask for isn't needed here.
qt_policy(SET QTP0004 OLD)

# URI/VERSION match the "RaceDash 1.0" module already used in main.cpp's
# qmlRegisterUncreatableMetaObject() calls for the RaceBox/Updates/Net enums —
# C++-registered types and QML-file-provided types coexist fine under one
# module URI, so LapTimer.qml's existing `import RaceDash 1.0` is unaffected.
qt_add_qml_module(bmw-e46-dash
    URI RaceDash
    VERSION 1.0
    QML_FILES
        qml/TimeFormat.js
        qml/main.qml
        qml/DashboardView.qml
        qml/SettingsView.qml
        qml/SettingsMenu.qml
        qml/SettingsDetail.qml
        qml/settings/PlaceholderSetting.qml
        qml/settings/SessionsList.qml
        qml/settings/SessionGroupsList.qml
        qml/settings/SessionSummary.qml
        qml/settings/TrackMap.qml
        qml/settings/TracksBrowser.qml
        qml/settings/DeviceInfoMenu.qml
        qml/settings/AppVersionDetail.qml
        qml/settings/DeviceStats.qml
        qml/settings/DeviceLog.qml
        qml/settings/NetworkMenu.qml
        qml/settings/WifiIcon.qml
        qml/settings/WifiDetail.qml
        qml/settings/WifiPasswordModal.qml
        qml/settings/WiredDetail.qml
        qml/settings/PasswordEntryModal.qml
        qml/settings/PasswordModal.qml
        qml/settings/OnScreenKeyboard.qml
        qml/settings/TextButton.qml
        qml/settings/StatRow.qml
        qml/Gauge.qml
        qml/GearIndicator.qml
        qml/LedStrip.qml
        qml/LapTimer.qml
        qml/StatusBar.qml
)

if(Qt6Bluetooth_FOUND)
    target_compile_definitions(bmw-e46-dash PRIVATE HAVE_BLUETOOTH)
    target_link_libraries(bmw-e46-dash PRIVATE Qt6::Bluetooth)
    message(STATUS "Qt6 Bluetooth found — RaceBoxProvider enabled")
else()
    message(STATUS "Qt6 Bluetooth not found — only MockRaceBoxProvider available")
endif()
