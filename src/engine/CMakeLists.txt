cmake_minimum_required(VERSION 3.26)

project(engine_core LANGUAGES CXX)

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
set(VARYING_DEF ${SHADER_SOURCE_DIR}/varying.def.sc)

set(SHADER_INCLUDE_PATHS
    ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bgfx/src
    ${SHADER_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

set(INCLUDE_ARGS "")
foreach(INCLUDE_PATH ${SHADER_INCLUDE_PATHS})
    list(APPEND INCLUDE_ARGS -i "${INCLUDE_PATH}")
endforeach()

set(VERTEX_SHADERS
    vs_terrain_render
    vs_color
    vs_rect_wire
)

set(FRAGMENT_SHADERS
    fs_terrain_render
    fs_terrain_render_normal
    fs_color
    fs_rect_wire
)

set(COMPUTE_SHADERS
    cs_generate_smap
    cs_terrain_init
    cs_terrain_lod
    cs_terrain_update_draw
    cs_terrain_update_indirect
    cs_rect_max_height
)

function(compile_shader SHADER_NAME SHADER_TYPE RENDERER OUTPUT_DIR)
    if(${SHADER_TYPE} STREQUAL "VERTEX")
        set(TYPE_FLAG vertex)
    elseif(${SHADER_TYPE} STREQUAL "FRAGMENT")
        set(TYPE_FLAG fragment)
    elseif(${SHADER_TYPE} STREQUAL "COMPUTE")
        set(TYPE_FLAG compute)
    endif()

    if(${RENDERER} STREQUAL "dx11")
        set(PLATFORM_NAME windows)
        if(${SHADER_TYPE} STREQUAL "VERTEX")
            set(PROFILE_VERSION vs_5_0)
        elseif(${SHADER_TYPE} STREQUAL "FRAGMENT")
            set(PROFILE_VERSION ps_5_0)
        else()
            set(PROFILE_VERSION cs_5_0)
        endif()
    elseif(${RENDERER} STREQUAL "glsl")
        if(WIN32)
            set(PLATFORM_NAME windows)
        elseif(APPLE)
            set(PLATFORM_NAME osx)
        else()
            set(PLATFORM_NAME linux)
        endif()

        if(${SHADER_TYPE} STREQUAL "COMPUTE")
            set(PROFILE_VERSION 430)
        elseif(APPLE)
            set(PROFILE_VERSION 410)
        else()
            set(PROFILE_VERSION 440)
        endif()
    elseif(${RENDERER} STREQUAL "spirv")
        if(WIN32)
            set(PLATFORM_NAME windows)
        elseif(APPLE)
            set(PLATFORM_NAME osx)
        else()
            set(PLATFORM_NAME linux)
        endif()
        set(PROFILE_VERSION spirv)
    elseif(${RENDERER} STREQUAL "metal")
        set(PLATFORM_NAME osx)
        set(PROFILE_VERSION metal)
    endif()

    set(INPUT_FILE ${SHADER_SOURCE_DIR}/${SHADER_NAME}.sc)
    set(OUTPUT_BIN ${OUTPUT_DIR}/${RENDERER}/${SHADER_NAME}.bin)

    file(MAKE_DIRECTORY ${OUTPUT_DIR}/${RENDERER})

    if(NOT ${SHADER_TYPE} STREQUAL "COMPUTE")
        set(VARYINGDEF_ARG --varyingdef "${VARYING_DEF}")
    else()
        set(VARYINGDEF_ARG "")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_BIN}
        COMMAND $<TARGET_FILE:shaderc>
            -f "${INPUT_FILE}"
            -o "${OUTPUT_BIN}"
            --type ${TYPE_FLAG}
            --platform ${PLATFORM_NAME}
            --profile ${PROFILE_VERSION}
            ${INCLUDE_ARGS}
            ${VARYINGDEF_ARG}
            $<$<CONFIG:Debug>:--debug>
            $<$<CONFIG:Release>:-O 3>
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_TYPE} shader ${SHADER_NAME} for ${RENDERER}"
        VERBATIM
    )

    set(${SHADER_NAME}_${RENDERER}_OUTPUTS ${OUTPUT_BIN} PARENT_SCOPE)
endfunction()

if(WIN32)
    set(RENDERERS dx11 glsl spirv)
elseif(APPLE)
    set(RENDERERS metal glsl spirv)
else()
    set(RENDERERS glsl spirv)
endif()

set(ALL_SHADER_OUTPUTS "")

foreach(SHADER ${VERTEX_SHADERS})
    foreach(RENDERER ${RENDERERS})
        compile_shader(${SHADER} VERTEX ${RENDERER} ${SHADER_OUTPUT_DIR})
        list(APPEND ALL_SHADER_OUTPUTS ${${SHADER}_${RENDERER}_OUTPUTS})
    endforeach()
endforeach()

foreach(SHADER ${FRAGMENT_SHADERS})
    foreach(RENDERER ${RENDERERS})
        compile_shader(${SHADER} FRAGMENT ${RENDERER} ${SHADER_OUTPUT_DIR})
        list(APPEND ALL_SHADER_OUTPUTS ${${SHADER}_${RENDERER}_OUTPUTS})
    endforeach()
endforeach()

foreach(SHADER ${COMPUTE_SHADERS})
    foreach(RENDERER ${RENDERERS})
        set(COMPUTE_SUPPORTED TRUE)
        if(${RENDERER} STREQUAL "glsl" AND APPLE)
            set(COMPUTE_SUPPORTED FALSE)
        endif()

        if(COMPUTE_SUPPORTED)
            compile_shader(${SHADER} COMPUTE ${RENDERER} ${SHADER_OUTPUT_DIR})
            list(APPEND ALL_SHADER_OUTPUTS ${${SHADER}_${RENDERER}_OUTPUTS})
        endif()
    endforeach()
endforeach()

add_custom_target(engine_shaders ALL DEPENDS ${ALL_SHADER_OUTPUTS})

add_library(${PROJECT_NAME}
    common/bgfx_utils.cpp
    render/core/render_device_bgfx.cpp
    render/pipeline/render_graph.cpp
    render/pipeline/render_pipeline.cpp
    render/features/terrain/terrain_render_feature.cpp
    render/terrain/terrain_height_field_loader.cpp
    render/terrain/terrain_heightmap_decoder.cpp
    render/terrain/terrain_render_pipeline.cpp
    render/terrain/terrain_patch_tables.cpp
    render/terrain/terrain_uniform_set.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bx/include
    ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bimg/include
)

if(WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bx/include/compat/msvc
    )
elseif(APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bx/include/compat/osx
    )
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC
    bgfx
    bx
    bimg
)

if(TARGET meshoptimizer)
    target_link_libraries(${PROJECT_NAME} PUBLIC meshoptimizer)
endif()

if(TARGET dear-imgui)
    target_link_libraries(${PROJECT_NAME} PUBLIC dear-imgui)
endif()

add_dependencies(${PROJECT_NAME} engine_shaders)
