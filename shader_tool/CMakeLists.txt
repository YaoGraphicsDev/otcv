set(OTCV_SHADER_TOOL "OTCVShaderTool" CACHE INTERNAL "")
file(MAKE_DIRECTORY ${OTCV_SPIRV_OUT_PATH})
message(STATUS "CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB SHADER_FILES "${OTCV_GLSL_IN_PATH}/*.*")
#[[ foreach(SHADER ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV_FILE "${OTCV_SPIRV_OUT_PATH}/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER} -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${SHADER} to ${SPIRV_FILE}"
        VERBATIM
    )

    # Add compiled SPIR-V file to library dependencies to ensure shaders are compiled before building the library
    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach() ]]

include(${OTCV_CMAKE_INCLUDE_PATH}/shader_compile.cmake)
compile_shaders("${SHADER_FILES}" "${OTCV_SPIRV_OUT_PATH}")

# Create a custom target for compiling shaders
add_custom_target(
    ${OTCV_SHADER_TOOL} ALL
    DEPENDS ${SPIRV_FILES}
    SOURCES ${SHADER_FILES})
