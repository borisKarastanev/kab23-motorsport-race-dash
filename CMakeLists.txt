cmake_minimum_required(VERSION 3.16)
project(bmw-e46-dash VERSION 1.0.1 LANGUAGES CXX)

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
    ${RESOURCES}
)

target_include_directories(bmw-e46-dash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(bmw-e46-dash PRIVATE DASH_VERSION="${PROJECT_VERSION}")

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
