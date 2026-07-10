cmake_minimum_required(VERSION 3.16)

# Cache compiled objects by content+effective flags, not file timestamps.
# CMake's "Unix Makefiles" generator regenerates shared per-target bookkeeping
# files (depend.make/build.make) on ANY CMakeLists.txt edit, which makes Make
# treat every object file as stale and re-invoke the compiler for it — even
# for files whose actual compile command didn't change (e.g. every version-
# bump release commit, which only affects updatemodel.cpp's DASH_VERSION
# define). ccache intercepts those re-invocations and returns a cached object
# for any file whose real preprocessed source + flags are unchanged, instead
# of recompiling. No-op if ccache isn't installed.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

project(bmw-e46-dash VERSION 1.0.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick SerialBus Network)
find_package(Qt6 OPTIONAL_COMPONENTS Bluetooth)

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

# Scoped to updatemodel.cpp only (the sole consumer of DASH_VERSION), not the
# whole target: target_compile_definitions() would put DASH_VERSION on every
# translation unit's compile command, so every version-bump release commit
# (CMakeLists.txt's PROJECT_VERSION, bumped for every release per the
# versioning process) would force a full rebuild of all 18 .cpp files on
# every in-app update — recompiling files whose source hadn't changed at all.
set_source_files_properties(src/updatemodel.cpp PROPERTIES
    COMPILE_DEFINITIONS "DASH_VERSION=\"${PROJECT_VERSION}\""
)

target_link_libraries(bmw-e46-dash
    Qt6::Core
    Qt6::Quick
    Qt6::SerialBus
    Qt6::Network
)

if(Qt6Bluetooth_FOUND)
    target_compile_definitions(bmw-e46-dash PRIVATE HAVE_BLUETOOTH)
    target_link_libraries(bmw-e46-dash Qt6::Bluetooth)
    message(STATUS "Qt6 Bluetooth found — RaceBoxProvider enabled")
else()
    message(STATUS "Qt6 Bluetooth not found — only MockRaceBoxProvider available")
endif()
