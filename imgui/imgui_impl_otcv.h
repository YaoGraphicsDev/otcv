#pragma once
#include "otcv.h"

struct ImGui_ImplOTCV_InitInfo
{
    // user configurations
    otcv::Queue* queue = nullptr;
    std::string font_path = "";
    float font_size_pixels = 20.0f; // takes effect only when font_path is specified
    VkFormat target_format;
};
struct ImGui_ImplOTCV_Data
{
    ImGui_ImplOTCV_InitInfo     OTCVInitInfo = {};
    otcv::Sampler*              sampler = nullptr;
    otcv::ShaderModule*         vertex_shader = nullptr;
    otcv::ShaderModule*         fragment_shader = nullptr;
    otcv::VertexBuffer*         vertex_buffer = nullptr;
    otcv::Buffer*               index_buffer = nullptr;
    otcv::GraphicsPipeline*     pipeline = nullptr;
    otcv::Image*                font_image = nullptr;
    otcv::DescriptorPool*       descriptor_pool = nullptr;
    otcv::DescriptorSet*        descriptor_set = nullptr;
    otcv::CommandPool*          command_pool = nullptr;
    otcv::CommandBuffer*        command_buffer = nullptr;

    ImGui_ImplOTCV_Data() {
        memset((void*)this, 0, sizeof(*this));
    }
};

struct ImGui_ImplOTCV_SynchronizationInfo {
    // memory barrier in the form of memory barrier commands, optional
    otcv::ResourceState target_pre_render_state = otcv::ResourceState::Null;
    otcv::ResourceState target_post_render_state = otcv::ResourceState::Null;

    std::vector<otcv::Semaphore*> wait_for_semaphores;
    std::vector<VkPipelineStageFlags> wait_for_stages;
    std::vector<otcv::Semaphore*> signal_semaphores;
    otcv::Fence* signal_fence = nullptr;
};

void ImGui_ImplOTCV_Init(ImGui_ImplOTCV_InitInfo* info);
void ImGui_ImplOTCV_Shutdown();
void ImGui_ImplOTCV_RenderDrawData(otcv::Image* target, ImGui_ImplOTCV_SynchronizationInfo* info);

// internal use
void ImGui_ImplOTCV_CreateOTCVObjects();
void ImGui_ImplOTCV_DestroyDeviceObjects();
ImGui_ImplOTCV_Data* ImGui_ImplOTCV_GetBackendData();
void ImGui_ImplOTCV_BuildBuffers();
void ImGui_ImplOTCV_Commands(otcv::CommandBuffer* command_buffer, otcv::Image* render_target, ImGui_ImplOTCV_SynchronizationInfo* info);

