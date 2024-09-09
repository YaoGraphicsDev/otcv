# Over-The-Counter Vulkan
## Dependencies
Vulkan 1.3 installed on system, visible to CMake `find_package()`.

GLFW 4.3

ImGui v1.90.0 source code


## CMake Parameters
`OTCV_GLFW_PATH` GLFW path, absolute path.

`OTCV_GLFW_LIB_VARIANT` GLFW library variant. Expression `${OTCV_GLFW_PATH}/${OTCV_GLFW_LIB_VARIANT}` makes up the absolute path that contains glfw3.lib.

`OTCV_ENABLE_IMGUI` Whether to enable ImGui.

`OTCV_IMGUI_PATH` ImGui path if `OTCV_ENABLE_IMGUI=ON`, otherwise ignored.

`OTCV_ENABLE_COMPILE_SHADERS` Whether to compile shaders.

`OTCV_GLSL_IN_PATH` Absolute path containing GLSL shader files if `OTCV_ENABLE_COMPILE_SHADERS=ON`, otherwise ignored.

`OTCV_SPIRV_OUT_PATH` Absolute path that contains compiled SPIRV files if `OTCV_ENABLE_COMPILE_SHADERS=ON`, otherwise ignored.

## Components
### Base
### ImGui Rendering Implementaion


