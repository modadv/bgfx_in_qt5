cmake_minimum_required(VERSION 3.26)

project(bgfx_qt_widget_example LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)
find_package(nlohmann_json REQUIRED)

add_executable(${PROJECT_NAME}
    benchmark_runner.h
    benchmark_runner.cpp
    main.cpp
    demo_controller.h
    demo_controller.cpp
    qml.qrc
    ${CMAKE_SOURCE_DIR}/src/engine/quick/terrain_viewport_item.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/quick/terrain_viewport_item.h
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
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
    Qt5::Widgets
    nlohmann_json::nlohmann_json
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DEMO_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

if(UNIX AND NOT APPLE)
    find_package(Qt5 5.15 REQUIRED COMPONENTS X11Extras)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt5::X11Extras)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMENT "Copying compiled shaders for runtime loading"
)
