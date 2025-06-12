set(OTCV_SHADER_TOOL "OTCVShaderTool" CACHE INTERNAL "")
file(MAKE_DIRECTORY ${OTCV_SPIRV_OUT_PATH})
message(STATUS "CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE SHADER_FILES "${OTCV_GLSL_IN_PATH}/*.*")

include(${OTCV_CMAKE_INCLUDE_PATH}/shader_compile.cmake)
compile_shaders("${OTCV_GLSL_IN_PATH}" "${OTCV_SPIRV_OUT_PATH}")

# Create a custom target for compiling shaders
add_custom_target(
    ${OTCV_SHADER_TOOL} ALL
    DEPENDS ${SPIRV_FILES}
    SOURCES ${SHADER_FILES})
