cmake_minimum_required(VERSION 3.27)
project(llhfte VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Release" "Debug" "RelWithDebInfo")

add_compile_options(
    # Warnings
    -Wall -Wextra -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wcast-align
    -Wold-style-cast
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2

    # Hard enforcements
    -fno-exceptions
    -fno-rtti

    # CPU target
    -march=native
    -mtune=native

    # Release
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Release>:-funroll-loops>
    $<$<CONFIG:Release>:-fomit-frame-pointer>
    $<$<CONFIG:Release>:-fvisibility=hidden>
    $<$<CONFIG:Release>:-flto>

    # Debug — sanitizers + full symbols, no optim
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    $<$<CONFIG:Debug>:-fno-optimize-sibling-calls>

    # RelWithDebInfo — profiling-friendly
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g>
    $<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
)

add_link_options(
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(llhfte ${SOURCES})
target_include_directories(llhfte PRIVATE include src)

find_library(URING_LIB uring)
if(URING_LIB)
    target_link_libraries(llhfte PRIVATE ${URING_LIB})
    target_compile_definitions(llhfte PRIVATE HAVE_URING)
endif()
