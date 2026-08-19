# Over-The-Counter Vulkan

OTCV is a lightweight C++ rendering framework built on Vulkan 1.3. It reduces boilerplate without hiding Vulkan objects or execution details.

## Components

### Base

The Base module provides wrappers and builders for common Vulkan objects, command recording, synchronization and presentation. The underlying Vulkan handles remain accessible when direct API use is needed.

```cpp
GraphicsPipeline* pipeline = GraphicsPipelineBuilder()
    .vertex_shader(vertex_shader)
    .fragment_shader(fragment_shader)
    .color_attachment(VK_FORMAT_R16G16B16A16_SFLOAT)
    .depth_test(true)
    .build();
```

### Frame Graph

The frame graph orders
- graphics
- compute 
- transfer

passes from their declared resource access. It manages
- transient
- imported
- ping-pong history
  
resources while handling resource aliasing, state transitions and synchronization.

Its runtime layer also manages presentation, swapchain recreation, and on-the-fly graph rebuilding.

```cpp
fg::Application app(config);

auto build_graph = [](fg::Application* app) {
    auto graph = app->framegraph();

    auto output = graph->add_resource("Output", image_builder);
    auto& pass = graph->add_pass("ToneMapping", fg::PassType::Graphics);

    pass.access(fg::ResourceAccessType::TextureIn, hdr);
    pass.access(fg::ResourceAccessType::ColorOut, output);
    pass.execute_func(record_tone_mapping);

    graph->set_as_backbuffer(output);
};

app.framegraph_initial_build(build_graph);
app.synchronized_frame_update(update_scene);
app.run();
```

Client code declares resources, pass access and recording callbacks. The frame graph derives execution order and synchronization from those declarations.

### ImGui Backend

A custom Dear ImGui renderer that uploads draw data and records rendering commands through OTCV. It can be used directly or as a frame graph pass.

### Shader Tool

CMake discovers GLSL shaders, tracks their dependencies and compiles them to Vulkan 1.3 SPIR-V. It also generates matching C++ types from shader layouts.

At runtime, OTCV loads SPIR-V and reflects shader stages, resources, descriptor bindings and push constants for pipeline creation.

## Build

### Dependencies

- Vulkan 1.3
- GLFW 4.3
- Dear ImGui 1.90.0 source, if enabled

### CMake Options

| Option | Description |
| --- | --- |
| `OTCV_GLFW_PATH` | Absolute path to GLFW. |
| `OTCV_GLFW_LIB_VARIANT` | Subdirectory containing `glfw3.lib`. |
| `OTCV_ENABLE_IMGUI` | Enables the ImGui backend. |
| `OTCV_IMGUI_PATH` | Path to the ImGui source. |
| `OTCV_ENABLE_COMPILE_SHADERS` | Enables shader compilation. |
| `OTCV_GLSL_IN_PATH` | Directory containing GLSL shaders. |
| `OTCV_SPIRV_OUT_PATH` | Output directory for compiled SPIR-V. |
