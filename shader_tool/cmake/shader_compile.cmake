find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(_otcv_to_pascal_case INPUT OUTPUT_VAR)
    string(REGEX REPLACE "[^A-Za-z0-9]+" ";" NAME_PARTS "${INPUT}")
    set(RESULT "")

    foreach(PART IN LISTS NAME_PARTS)
        if(PART STREQUAL "")
            continue()
        endif()

        string(LENGTH "${PART}" PART_LENGTH)
        string(SUBSTRING "${PART}" 0 1 FIRST_CHAR)
        string(TOUPPER "${FIRST_CHAR}" FIRST_CHAR)

        if(PART_LENGTH GREATER 1)
            string(SUBSTRING "${PART}" 1 -1 REST)
        else()
            set(REST "")
        endif()

        string(APPEND RESULT "${FIRST_CHAR}${REST}")
    endforeach()

    if(RESULT MATCHES "^[0-9]")
        string(PREPEND RESULT "Shader")
    endif()

    set(${OUTPUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

function(compile_shaders INPUT_DIR SPIRV_OUTPUT_DIR INCLUDE_DIR CPP_REFLECT_OUTPUT_DIR)
    get_filename_component(INPUT_DIR "${INPUT_DIR}" ABSOLUTE)
    get_filename_component(SPIRV_OUTPUT_DIR "${SPIRV_OUTPUT_DIR}" ABSOLUTE)
    get_filename_component(CPP_REFLECT_OUTPUT_DIR "${CPP_REFLECT_OUTPUT_DIR}" ABSOLUTE)

    if(INCLUDE_DIR)
        get_filename_component(INCLUDE_DIR "${INCLUDE_DIR}" ABSOLUTE)
    endif()

    # The generator is next to the CMakeLists.txt which calls this function.
    set(REFLECT_GENERATOR
        "${CMAKE_CURRENT_SOURCE_DIR}/generate_reflect_header.py"
    )

    set(REFLECT_RUNNER
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/run_glslang_reflect.cmake"
    )

    if(NOT EXISTS "${REFLECT_GENERATOR}")
        message(FATAL_ERROR
            "Reflection generator was not found: ${REFLECT_GENERATOR}"
        )
    endif()

    if(NOT EXISTS "${REFLECT_RUNNER}")
        message(FATAL_ERROR
            "Reflection runner was not found: ${REFLECT_RUNNER}"
        )
    endif()

    message(STATUS "Shader input directory: ${INPUT_DIR}")
    message(STATUS "SPIR-V output directory: ${SPIRV_OUTPUT_DIR}")
    message(STATUS "C++ reflection output directory: ${CPP_REFLECT_OUTPUT_DIR}")

    if(INCLUDE_DIR)
        message(STATUS "Shader include directory: ${INCLUDE_DIR}")
    endif()

    file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
        "${INPUT_DIR}/*.vert"
        "${INPUT_DIR}/*.frag"
        "${INPUT_DIR}/*.comp"
        "${INPUT_DIR}/*.geom"
        "${INPUT_DIR}/*.tesc"
        "${INPUT_DIR}/*.tese"
        "${INPUT_DIR}/*.rgen"
        "${INPUT_DIR}/*.rchit"
        "${INPUT_DIR}/*.rmiss"
        "${INPUT_DIR}/*.rahit"
        "${INPUT_DIR}/*.rint"
        "${INPUT_DIR}/*.rcall"
        "${INPUT_DIR}/*.mesh"
        "${INPUT_DIR}/*.task"
    )

    set(SHADER_INCLUDE_FILES "")
    if(INCLUDE_DIR)
        file(GLOB_RECURSE SHADER_INCLUDE_FILES CONFIGURE_DEPENDS
            "${INCLUDE_DIR}/*.h"
            "${INCLUDE_DIR}/*.inc"
            "${INCLUDE_DIR}/*.glsl"
            "${INCLUDE_DIR}/*.glslinc"
        )
    endif()

    set(SPIRV_FILES "")
    set(REFLECT_FILES "")
    set(CPP_REFLECT_FILES "")

    foreach(SHADER IN LISTS SHADER_FILES)
        file(RELATIVE_PATH REL_PATH "${INPUT_DIR}" "${SHADER}")
        get_filename_component(SUB_DIR "${REL_PATH}" DIRECTORY)
        get_filename_component(FILE_NAME "${SHADER}" NAME)

        if(SUB_DIR)
            set(SPIRV_SUB_DIR "${SPIRV_OUTPUT_DIR}/${SUB_DIR}")
            set(CPP_SUB_DIR "${CPP_REFLECT_OUTPUT_DIR}/${SUB_DIR}")
        else()
            set(SPIRV_SUB_DIR "${SPIRV_OUTPUT_DIR}")
            set(CPP_SUB_DIR "${CPP_REFLECT_OUTPUT_DIR}")
        endif()

        set(SPIRV_FILE "${SPIRV_SUB_DIR}/${FILE_NAME}.spv")
        set(REFLECT_FILE "${SPIRV_SUB_DIR}/${FILE_NAME}.reflect")
        set(CPP_REFLECT_FILE "${CPP_SUB_DIR}/${FILE_NAME}.hpp")

        _otcv_to_pascal_case("${FILE_NAME}" CPP_NAMESPACE)

        set(GLSLANG_ARGUMENTS -g -V)
        if(INCLUDE_DIR)
            list(APPEND GLSLANG_ARGUMENTS "-I${INCLUDE_DIR}")
        endif()
        list(APPEND GLSLANG_ARGUMENTS
            "${SHADER}"
            -o "${SPIRV_FILE}"
        )

        set(REFLECT_GENERATOR_ARGUMENTS
            "${SHADER}"
            "${REFLECT_FILE}"
            "${CPP_REFLECT_FILE}"
            --namespace "${CPP_NAMESPACE}"
            --glslang-validator "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
        )

        if(INCLUDE_DIR)
            list(APPEND REFLECT_GENERATOR_ARGUMENTS
                --include-dir "${INCLUDE_DIR}"
            )
        endif()

        add_custom_command(
            OUTPUT
                "${SPIRV_FILE}"
                "${REFLECT_FILE}"
                "${CPP_REFLECT_FILE}"

            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPIRV_SUB_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CPP_SUB_DIR}"

            # First invocation keeps normal compiler diagnostics visible.
            COMMAND
                "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
                ${GLSLANG_ARGUMENTS}

            # Second invocation writes only glslang stdout to the .reflect file.
            COMMAND
                "${CMAKE_COMMAND}"
                "-DGLSLANG=${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
                "-DSHADER=${SHADER}"
                "-DINCLUDE_DIR=${INCLUDE_DIR}"
                "-DSPIRV_FILE=${SPIRV_FILE}"
                "-DREFLECT_FILE=${REFLECT_FILE}"
                -P "${REFLECT_RUNNER}"

            # Generate the C++ header from both the GLSL source and
            # glslang reflection output.
            COMMAND
                "${Python3_EXECUTABLE}"
                "${REFLECT_GENERATOR}"
                ${REFLECT_GENERATOR_ARGUMENTS}

            DEPENDS
                "${SHADER}"
                ${SHADER_INCLUDE_FILES}
                "${REFLECT_GENERATOR}"
                "${REFLECT_RUNNER}"

            COMMENT
                "Compiling and reflecting shader: ${REL_PATH}"

            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        list(APPEND SPIRV_FILES "${SPIRV_FILE}")
        list(APPEND REFLECT_FILES "${REFLECT_FILE}")
        list(APPEND CPP_REFLECT_FILES "${CPP_REFLECT_FILE}")
    endforeach()

    source_group(TREE "${INPUT_DIR}" FILES ${SHADER_FILES})
    if(SHADER_INCLUDE_FILES)
        source_group(TREE "${INPUT_DIR}" FILES ${SHADER_INCLUDE_FILES})
    endif()

    set(SHADER_FILES "${SHADER_FILES}" PARENT_SCOPE)
    set(SHADER_INCLUDE_FILES "${SHADER_INCLUDE_FILES}" PARENT_SCOPE)
    set(SPIRV_FILES "${SPIRV_FILES}" PARENT_SCOPE)
    set(REFLECT_FILES "${REFLECT_FILES}" PARENT_SCOPE)
    set(CPP_REFLECT_FILES "${CPP_REFLECT_FILES}" PARENT_SCOPE)
endfunction()
