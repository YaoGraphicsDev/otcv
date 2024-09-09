set(OTCV_IMGUI_LIB "OTCVImGuiLib" CACHE INTERNAL "")

file(GLOB HEADERS
        "${OTCV_IMGUI_PATH}/imconfig.h"
        "${OTCV_IMGUI_PATH}/imgui.h"
        "${OTCV_IMGUI_PATH}/imgui_internal.h"
        "${OTCV_IMGUI_PATH}/imstb_rectpack.h"
        "${OTCV_IMGUI_PATH}/imstb_textedit.h"
        "${OTCV_IMGUI_PATH}/imstb_truetype.h")
file(GLOB SOURCES
        "${OTCV_IMGUI_PATH}/imgui.cpp"
        "${OTCV_IMGUI_PATH}/imgui.cpp"
        "${OTCV_IMGUI_PATH}/imgui_draw.cpp"
        "${OTCV_IMGUI_PATH}/imgui_tables.cpp"
        "${OTCV_IMGUI_PATH}/imgui_widgets.cpp")

set(BACKEND_PATH "${OTCV_IMGUI_PATH}/backends")
message(STATUS "BACKEND_PATH=${BACKEND_PATH}")
file(GLOB BACKEND_HEADERS
        "${BACKEND_PATH}/imgui_impl_glfw.h"
        "imgui_impl_otcv.h")
file(GLOB BACKEND_SOURCES
        "${BACKEND_PATH}/imgui_impl_glfw.cpp"
        "imgui_impl_otcv.cpp")
source_group("backends" FILES ${BACKEND_HEADERS} ${BACKEND_SOURCES})

add_library(${OTCV_IMGUI_LIB} STATIC
        ${HEADERS}
        ${BACKEND_HEADERS}
        ${SOURCES}
        ${BACKEND_SOURCES})

# product output directory
#set_target_properties(${OTCV_IMGUI_LIB}
#    PROPERTIES
#    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
#   ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
#)

# compile shaders into header files
set(SHADER_PATH "${BACKEND_PATH}/vulkan")
message(STATUS "SHADER_PATH=${SHADER_PATH}")
set(SPIRV_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/generated_spirv")
message(STATUS "SPIRV_OUTPUT_PATH=${SPIRV_OUTPUT_PATH}")

file(MAKE_DIRECTORY ${SPIRV_OUTPUT_PATH})
# vertex
execute_process(
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER_PATH}/glsl_shader.vert --vn imgui_vert_spv -o ${SPIRV_OUTPUT_PATH}/imgui_vert.h
        RESULT_VARIABLE result
        ERROR_VARIABLE compile_error)
if (NOT result EQUAL 0)
        message(FATAL_ERROR "Shader compilation failed: ${compile_error}")
endif()
# fragment
execute_process(
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER_PATH}/glsl_shader.frag --vn imgui_frag_spv -o ${SPIRV_OUTPUT_PATH}/imgui_frag.h
        RESULT_VARIABLE result
        ERROR_VARIABLE compile_error)
if (NOT result EQUAL 0)
        message(FATAL_ERROR "Shader compilation failed: ${compile_error}")
endif()

target_include_directories(${OTCV_IMGUI_LIB}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}        
        PUBLIC ${OTCV_IMGUI_PATH}
        PUBLIC ${BACKEND_PATH}
        PRIVATE ${SPIRV_OUTPUT_PATH} # imgui generated spirv headers should not be visible to any projects other than imgui itself
)
target_link_libraries(${OTCV_IMGUI_LIB} PRIVATE ${OTCV_BASE_LIB})

#[[ foreach(SHADER ${SHADER_FILES})
        #get_filename_component(FILE_NAME ${SHADER} NAME_WE)
        get_filename_component(SHADER_TYPE ${SHADER} EXT)
        set(SPIRV_HEADER "${SPIRV_OUTPUT_DIR}/imgui_${SHADER_TYPE}.h")
        set(COMPILE_COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER} --vn imgui_${SHADER_TYPE}_spv -o ${SPIRV_HEADER}")
        message(STATUS ${COMPILE_COMMAND})
        execute_process(
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -g -V ${SHADER} --vn imgui_${SHADER_TYPE}_spv -o ${SPIRV_HEADER}
                RESULT_VARIABLE result
                ERROR_VARIABLE compile_error)
        
        # Check if the compilation was successful

endforeach() ]]

#[[ #compile shaders
set(SHADER_FILES
    "${ImGui_SHADER_DIR}/glsl_shader.vert"
    "${ImGui_SHADER_DIR}/glsl_shader.frag"
)
set(SPIRV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/imgui/spirv")
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

# Command to compile shaders using glslangValidator
foreach(SHADER ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV_FILE "${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv")

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
    compile_imgui_shaders ALL
    DEPENDS ${SPIRV_FILES}
)

# Ensure shaders are compiled before building the static library
add_dependencies(${OTCV_IMGUI_LIB} compile_imgui_shaders) ]]