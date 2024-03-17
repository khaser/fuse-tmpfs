cmake_minimum_required(VERSION 3.6)
project(fuse-tmpfs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TARGET_NAME "${PROJECT_NAME}")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
file(GLOB SRCS "src/*.c")

find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED fuse)

add_executable(${TARGET_NAME} ${SRCS})
target_include_directories(${TARGET_NAME} PUBLIC "include" ${FUSE_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PUBLIC ${FUSE_LIBRARIES})
target_compile_options(${TARGET_NAME} PUBLIC ${FUSE_CFLAGS_OTHER})
