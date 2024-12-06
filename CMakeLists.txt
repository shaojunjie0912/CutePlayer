cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_TYPE Debug)

project(CuteSTL LANGUAGES CXX)

if(WIN32)
  add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES -D_WINSOCKAPI_)
  if(MSVC)
    add_compile_options("/source-charset:utf-8")
  endif()
endif()

if(NOT MSVC)
  find_program(CCACHE_PROGRAM ccache) # 使用缓存，更快

  if(CCACHE_PROGRAM)
    message(STATUS "Found CCache: ${CCACHE_PROGRAM}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_PROGRAM})
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE_PROGRAM})
  endif()
endif()

include_directories(include)
add_subdirectory(src)
