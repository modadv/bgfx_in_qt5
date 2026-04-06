cmake_minimum_required(VERSION 3.26)

project(terrain_headless_benchmark LANGUAGES CXX)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui)
find_package(nlohmann_json REQUIRED)

add_executable(${PROJECT_NAME}
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/quick/terrain_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/quick/terrain_scene.h
    ${CMAKE_SOURCE_DIR}/src/engine/render/scene/orbit_camera_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/scene/orbit_camera_controller.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/common
    ${CMAKE_SOURCE_DIR}/src/engine/quick
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    engine_core
    Qt5::Core
    Qt5::Gui
    nlohmann_json::nlohmann_json
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMENT "Copying compiled shaders for runtime loading"
)
