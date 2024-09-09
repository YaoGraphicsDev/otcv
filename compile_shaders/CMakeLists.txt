set(OTCV_COMPILE_SHADERS_TASK "OTCVCompileShaders" CACHE INTERNAL "")
file(MAKE_DIRECTORY ${OTCV_SPIRV_OUT_PATH})

file(GLOB SHADER_FILES "${OTCV_GLSL_IN_PATH}/*.*")
foreach(SHADER ${SHADER_FILES})
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
endforeach()

# Create a custom target for compiling shaders
add_custom_target(
    ${OTCV_COMPILE_SHADERS_TASK} ALL
    DEPENDS ${SPIRV_FILES}
    SOURCES ${SHADER_FILES})

# Ensure shaders are compiled before building the static library
#add_dependencies(${OTCV_IMGUI_LIB} compile_imgui_shaders)