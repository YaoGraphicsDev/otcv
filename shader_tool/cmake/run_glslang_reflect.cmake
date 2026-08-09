if(NOT DEFINED GLSLANG OR GLSLANG STREQUAL "")
    message(FATAL_ERROR "GLSLANG was not provided")
endif()

if(NOT DEFINED SHADER OR SHADER STREQUAL "")
    message(FATAL_ERROR "SHADER was not provided")
endif()

if(NOT DEFINED REFLECT_FILE OR REFLECT_FILE STREQUAL "")
    message(FATAL_ERROR "REFLECT_FILE was not provided")
endif()

set(ARGS
    -g
    -V
    --target-env vulkan1.3
)

if(DEFINED INCLUDE_DIR AND NOT INCLUDE_DIR STREQUAL "")
    list(APPEND ARGS "-I${INCLUDE_DIR}")
endif()

list(APPEND ARGS
    "${SHADER}"
    -o "${SPIRV_FILE}"
    -l
    -q
    -s
    --reflect-all-block-variables
)

execute_process(
    COMMAND "${GLSLANG}" ${ARGS}
    OUTPUT_FILE "${REFLECT_FILE}"
    RESULT_VARIABLE RESULT
)

if(NOT RESULT EQUAL 0)
    file(REMOVE "${REFLECT_FILE}")
    message(FATAL_ERROR
        "glslangValidator reflection failed for ${SHADER} with exit code ${RESULT}"
    )
endif()
