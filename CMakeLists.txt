cmake_minimum_required(VERSION 3.25)

project(pass VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Sql Widgets Test Charts)

enable_testing()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
