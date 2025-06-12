# export SPIRV_FILES

#file(GLOB SHADER_FILES "${OTCV_GLSL_IN_PATH}/*.*")

function(compile_shaders INPUT_DIR OUTPUT_DIR)
    message(STATUS "INPUT_DIR = ${INPUT_DIR}")
    message(STATUS "OUTPUT_DIR = ${OUTPUT_DIR}")

    file(GLOB_RECURSE SHADER_FILES "${INPUT_DIR}/*.*")

    set(SPIRV_FILES "")

    foreach(SHADER ${SHADER_FILES})
        # compute relative path and filename
        file(RELATIVE_PATH REL_PATH ${INPUT_DIR} ${SHADER})
        get_filename_component(SUB_DIR ${REL_PATH} DIRECTORY)
        get_filename_component(FILE_NAME ${SHADER} NAME)

        file(MAKE_DIRECTORY ${OUTPUT_DIR}/${SUB_DIR})

        set(SPIRV_FILE "${OUTPUT_DIR}/${SUB_DIR}/${FILE_NAME}.spv")

        add_custom_command(
            OUTPUT ${SPIRV_FILE}
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER} -o ${SPIRV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling shader: ${SHADER} to ${SPIRV_FILE}"
            VERBATIM
        )
    
        # Add compiled SPIR-V file to library dependencies to ensure shaders are compiled before building the library
        list(APPEND SPIRV_FILES ${SPIRV_FILE})
        string(REPLACE "/" "\\" GROUP_NAME "${SUB_DIR}")
        source_group("${GROUP_NAME}" FILES "${SHADER}")
    endforeach()

    # Expose the list of compiled shaders to the caller
    set(SPIRV_FILES ${SPIRV_FILES} PARENT_SCOPE)

endfunction()