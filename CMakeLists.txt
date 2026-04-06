cmake_minimum_required(VERSION 3.26)

project(bgfx_in_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Build bgfx tools so shaderc target is available for shader compilation.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(external/bgfx.cmake)
add_subdirectory(src/engine)
add_subdirectory(examples)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
