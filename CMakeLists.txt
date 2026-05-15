cmake_minimum_required(VERSION 3.16)
project(bmw-e46-dash VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Quick SerialBus)

qt5_add_resources(RESOURCES resources.qrc)

add_executable(bmw-e46-dash
    main.cpp
    src/icanprovider.h
    src/candatamodel.cpp
    src/dashconfig.cpp
    src/mockcanprovider.cpp
    src/realcanprovider.cpp
    ${RESOURCES}
)

target_include_directories(bmw-e46-dash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(bmw-e46-dash
    Qt5::Core
    Qt5::Quick
    Qt5::SerialBus
)
