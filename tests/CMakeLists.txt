function(add_engine_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    add_test(
        NAME ${target}
        COMMAND ${target}
    )
endfunction()

add_engine_test(terrain_heightmap_decoder_tests
    terrain_heightmap_decoder_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/terrain/terrain_heightmap_decoder.cpp
)

target_link_libraries(terrain_heightmap_decoder_tests PRIVATE
    bimg
)

add_engine_test(render_graph_tests
    render_graph_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/pipeline/render_graph.cpp
)

add_engine_test(render_scene_packet_tests
    render_scene_packet_tests.cpp
)

find_package(nlohmann_json REQUIRED)

add_engine_test(architecture_boundary_tests
    architecture_boundary_tests.cpp
)

target_link_libraries(architecture_boundary_tests PRIVATE
    nlohmann_json::nlohmann_json
)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui)

add_engine_test(orbit_camera_controller_tests
    orbit_camera_controller_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/scene/orbit_camera_controller.cpp
)

target_include_directories(orbit_camera_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine/common
)

target_link_libraries(orbit_camera_controller_tests PRIVATE
    Qt5::Core
    Qt5::Gui
    nlohmann_json::nlohmann_json
)

if(WIN32)
    add_custom_command(TARGET orbit_camera_controller_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:orbit_camera_controller_tests>
            $<TARGET_FILE_DIR:orbit_camera_controller_tests>
        COMMAND_EXPAND_LISTS
    )
endif()
