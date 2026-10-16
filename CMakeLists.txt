cmake_minimum_required(VERSION 3.19)
project(batmobile_scratch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets DBus)
qt_standard_project_setup()

# BlueZ via D-Bus : liste des appareils appairés + appairage automatique (partagé GUI / CLI)
qt_add_library(batmobile_bluez STATIC
    src/config.h
    src/bluez.h src/bluez.cpp
    src/pairing.h src/pairing.cpp
)
target_link_libraries(batmobile_bluez PUBLIC Qt6::Core Qt6::DBus)

# Programmation par blocs (GUI)
qt_add_executable(batmobile_scratch
    src/main.cpp
    src/window.h src/window.cpp
    src/link.h src/link.cpp
    src/blocks.h src/blocks.cpp
    src/blockview.h src/blockview.cpp
    src/palette.h src/palette.cpp
    src/canvas.h src/canvas.cpp
    src/runner.h src/runner.cpp
)
target_link_libraries(batmobile_scratch PRIVATE batmobile_bluez Qt6::Widgets)

# Appairage en ligne de commande
qt_add_executable(bt_pairing src/bt_pairing.cpp)
target_link_libraries(bt_pairing PRIVATE batmobile_bluez)
