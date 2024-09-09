# Check parameters
message(STATUS "Checking OTCV parameters...")

if (OTCV_ENABLE_IMGUI)
    message(STATUS "OTCV_ENABLE_IMGUI -- ON")
    if (NOT OTCV_IMGUI_PATH)
        message(FATAL_ERROR "OTCV_IMGUI_PATH not provided")
    else()
        message(STATUS "OTCV_IMGUI_PATH=${OTCV_IMGUI_PATH}")
    endif()
else()
    message(STATUS "OTCV_ENABLE_IMGUI -- OFF")
endif()

if (NOT OTCV_GLFW_PATH)
    message(FATAL_ERROR "OTCV_GLFW_PATH not provided")
else()
    message(STATUS "OTCV_GLFW_PATH=${OTCV_GLFW_PATH}")
endif()

if (NOT OTCV_GLFW_LIB_VARIANT)
    message(FATAL_ERROR "OTCV_GLFW_LIB_VARIANT not provided, check under ${OTCV_GLFW_PATH} for available options")
else()
    message(STATUS "OTCV_GLFW_LIB_VARIANT=${OTCV_GLFW_LIB_VARIANT}")
endif()

if (OTCV_ENABLE_COMPILE_SHADERS)
    message(STATUS "OTCV_ENABLE_COMPILE_SHADER -- ON")
    if (NOT OTCV_GLSL_IN_PATH)
        message(FATAL_ERROR "OTCV_GLSL_IN_PATH not provided")
    else()
        message(STATUS "OTCV_GLSL_IN_PATH=${OTCV_GLSL_IN_PATH}")
    endif()
    if (NOT OTCV_SPIRV_OUT_PATH)
        message(FATAL_ERROR "OTCV_SPIRV_OUT_PATH not provided")
    else()
        message(STATUS "OTCV_SPIRV_OUT_PATH=${OTCV_SPIRV_OUT_PATH}")
    endif()
else()
    message(STATUS "OTCV_ENABLE_COMPILE_SHADER -- OFF")
endif()

cmake_minimum_required(VERSION 3.20)
project(OTCV)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

# base
add_subdirectory("base")

# imgui
if (OTCV_ENABLE_IMGUI)
    add_subdirectory("imgui")
endif()

# shader compilation
if (OTCV_ENABLE_COMPILE_SHADERS)
    add_subdirectory("compile_shaders")
endif()