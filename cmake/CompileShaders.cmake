# CompileShaders.cmake
# Finds glslangValidator and compiles GLSL shaders to SPIR-V,
# then generates a C++ header with embedded constexpr arrays.

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    HINTS $ENV{VULKAN_SDK}/bin
          ${Vulkan_INCLUDE_DIR}/../bin
          /usr/bin
          /usr/local/bin
)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found — shaders cannot be compiled. "
                    "Install glslang-tools package or Vulkan SDK.")
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/spirv)
set(SHADER_HEADER     ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_spirv.hpp)

file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Explicit shader list — every shader must be listed here.
# Using an explicit list instead of GLOB ensures new shaders are never silently skipped.
set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/arrow3d.frag
    ${SHADER_SOURCE_DIR}/arrow3d.vert
    ${SHADER_SOURCE_DIR}/grid.frag
    ${SHADER_SOURCE_DIR}/grid.vert
    ${SHADER_SOURCE_DIR}/grid3d.frag
    ${SHADER_SOURCE_DIR}/grid3d.vert
    ${SHADER_SOURCE_DIR}/image3d.frag
    ${SHADER_SOURCE_DIR}/image3d.vert
    ${SHADER_SOURCE_DIR}/line.frag
    ${SHADER_SOURCE_DIR}/line.vert
    ${SHADER_SOURCE_DIR}/line3d.frag
    ${SHADER_SOURCE_DIR}/line3d.vert
    ${SHADER_SOURCE_DIR}/marker3d.frag
    ${SHADER_SOURCE_DIR}/marker3d.vert
    ${SHADER_SOURCE_DIR}/mesh3d.frag
    ${SHADER_SOURCE_DIR}/mesh3d.vert
    ${SHADER_SOURCE_DIR}/pointcloud.frag
    ${SHADER_SOURCE_DIR}/pointcloud.vert
    ${SHADER_SOURCE_DIR}/scatter.frag
    ${SHADER_SOURCE_DIR}/scatter.vert
    ${SHADER_SOURCE_DIR}/scatter_colormap.vert
    ${SHADER_SOURCE_DIR}/scatter3d.frag
    ${SHADER_SOURCE_DIR}/scatter3d.vert
    ${SHADER_SOURCE_DIR}/stat_fill.frag
    ${SHADER_SOURCE_DIR}/stat_fill.vert
    ${SHADER_SOURCE_DIR}/surface3d.frag
    ${SHADER_SOURCE_DIR}/surface3d.vert
    ${SHADER_SOURCE_DIR}/text.frag
    ${SHADER_SOURCE_DIR}/text.vert
)

# Verify all listed shaders exist at configure time
foreach(SHADER_SRC ${SHADER_SOURCES})
    if(NOT EXISTS ${SHADER_SRC})
        message(FATAL_ERROR "Shader source not found: ${SHADER_SRC}")
    endif()
endforeach()

set(SPIRV_OUTPUTS "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SPIRV_OUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2 -o ${SPIRV_OUT} ${SHADER_SRC}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader: ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND SPIRV_OUTPUTS ${SPIRV_OUT})
endforeach()

# Generate C++ header embedding all SPIR-V bytecode.
# The DEPENDS on all SPIRV_OUTPUTS ensures every .spv must exist before this runs.
add_custom_command(
    OUTPUT ${SHADER_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSPIRV_DIR=${SHADER_OUTPUT_DIR}
        -DOUTPUT_HEADER=${SHADER_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake
    DEPENDS ${SPIRV_OUTPUTS}
    COMMENT "Generating embedded shader header: shader_spirv.hpp"
    VERBATIM
)

add_custom_target(spectra_shaders DEPENDS ${SHADER_HEADER} ${SPIRV_OUTPUTS})
