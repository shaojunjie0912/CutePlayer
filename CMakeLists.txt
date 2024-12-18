cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


project(CutePlayer)

find_package(FFMPEG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

add_subdirectory(src)

