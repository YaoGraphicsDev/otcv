#pragma once
#include "otcv.h"

#include <memory>
#include <set>

namespace otcv {

// boilerplate 
extern std::shared_ptr<Instance> g_instance;
extern std::shared_ptr<Surface> g_surface;
extern PhysicalDevice g_physical_device;
extern std::shared_ptr<Device> g_device;
extern Queue g_queue;
extern std::shared_ptr<CommandPool> g_command_pool;
extern std::shared_ptr<Swapchain> g_swapchain;

// objects
extern std::set<std::shared_ptr<Semaphore>, RawPtrLess<Semaphore>> g_user_semaphores;
extern std::set<std::shared_ptr<Fence>, RawPtrLess<Fence>> g_user_fences;
extern std::set<std::shared_ptr<CommandPool>, RawPtrLess<CommandPool>> g_user_command_pools;
extern std::set<std::shared_ptr<RenderPass>, RawPtrLess<RenderPass>> g_user_render_passes;
extern std::set<std::shared_ptr<Image>, RawPtrLess<Image>> g_user_images;
extern std::set<std::shared_ptr<Buffer>, RawPtrLess<Buffer>> g_user_buffers;
extern std::set<std::shared_ptr<ShaderModule>, RawPtrLess<ShaderModule>> g_user_shader_modules;
extern std::set<std::shared_ptr<VertexBuffer>, RawPtrLess<VertexBuffer>> g_user_vertex_buffers;
extern std::set<std::shared_ptr<GraphicsPipeline>, RawPtrLess<GraphicsPipeline>> g_user_graphics_pipelines;
extern std::set<std::shared_ptr<ComputePipeline>, RawPtrLess<ComputePipeline>> g_user_compute_pipelines;
extern std::set<std::shared_ptr<DescriptorPool>, RawPtrLess<DescriptorPool>> g_user_descriptor_pools;
extern std::set<std::shared_ptr<Sampler>, RawPtrLess<Sampler>> g_user_samplers;
extern std::set<std::shared_ptr<Framebuffer>, RawPtrLess<Framebuffer>> g_user_framebuffers;
}