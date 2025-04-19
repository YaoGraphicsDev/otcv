# export SPIRV_FILES
function(compile_shaders SHADER_FILES OUTPUT_DIR)
    message(STATUS "OUTPUT_DIR = ${OUTPUT_DIR}")

    foreach(SHADER ${SHADER_FILES})
        get_filename_component(FILE_NAME ${SHADER} NAME)
        set(SPIRV_FILE "${OUTPUT_DIR}/${FILE_NAME}.spv")

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

    # Expose the list of compiled shaders to the caller
    set(SPIRV_FILES ${SPIRV_FILES} PARENT_SCOPE)

endfunction()