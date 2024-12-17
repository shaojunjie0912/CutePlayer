cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


project(CutePlayer)

find_package(FFMPEG REQUIRED)
find_package(SDL2 REQUIRED)

include_directories(${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS} ${SDL2_LIBRARY_DIRS})
link_libraries(${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})

add_subdirectory(src)

