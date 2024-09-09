#pragma once
#include "otcv.h"

struct ImGui_ImplOTCV_InitInfo
{
    // user configurations
    otcv::Queue* queue = nullptr;
    std::string font_path = "";
    float font_size_pixels = 20.0f; // takes effect only when font_path is specified
    std::vector<otcv::Image*> color_attachments;

    // memory barrier in the form of subpass dependencies, optional
    VkImageLayout intial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkPipelineStageFlags pre_pass_wait_stage = VK_PIPELINE_STAGE_NONE;
    VkAccessFlags pre_pass_access = VK_ACCESS_NONE;
    VkPipelineStageFlags post_pass_wait_stage = VK_PIPELINE_STAGE_NONE;
    VkAccessFlags post_pass_access = VK_ACCESS_NONE;
};
struct ImGui_ImplOTCV_Data
{
    ImGui_ImplOTCV_InitInfo     OTCVInitInfo = {};
    otcv::Sampler*              sampler = nullptr;
    otcv::RenderPass*           render_pass = nullptr;
    otcv::ShaderModule*         vertex_shader = nullptr;
    otcv::ShaderModule*         fragment_shader = nullptr;
    otcv::VertexBuffer*         vertex_buffer = nullptr;
    otcv::Buffer*               index_buffer = nullptr;
    std::vector<otcv::Framebuffer*> frambuffers;
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
    otcv::ResourceState attachments_pre_pass_state = otcv::ResourceState::Null;
    otcv::ResourceState attachments_post_pass_state = otcv::ResourceState::Null;

    std::vector<otcv::Semaphore*> wait_for_semaphores;
    std::vector<VkPipelineStageFlags> wait_for_stages;
    std::vector<otcv::Semaphore*> signal_semaphores;
    otcv::Fence* signal_fence = nullptr;
};

void ImGui_ImplOTCV_Init(ImGui_ImplOTCV_InitInfo* info);
void ImGui_ImplOTCV_Shutdown();
void ImGui_ImplOTCV_RenderDrawData(uint32_t attachment_id, ImGui_ImplOTCV_SynchronizationInfo* info);

// internal use
void ImGui_ImplOTCV_CreateOTCVObjects();
void ImGui_ImplOTCV_DestroyDeviceObjects();
ImGui_ImplOTCV_Data* ImGui_ImplOTCV_GetBackendData();
void ImGui_ImplOTCV_BuildBuffers();
void ImGui_ImplOTCV_Commands(otcv::CommandBuffer* command_buffer, uint32_t image_index, ImGui_ImplOTCV_SynchronizationInfo* info);

