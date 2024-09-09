#include "otcv.h"
#include "otcv_globals.h"
#include "otcv_config.h"
#include "otcv_utils.h"

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <algorithm>
#include <set>
#include <fstream>
#include <glfw/glfw3.h>

namespace otcv {

std::set<std::shared_ptr<Fence>, RawPtrLess<Fence>> g_user_fences = {};
std::set<std::shared_ptr<Semaphore>, RawPtrLess<Semaphore>> g_user_semaphores = {};
std::set<std::shared_ptr<CommandPool>, RawPtrLess<CommandPool>> g_user_command_pools = {};
std::set<std::shared_ptr<Image>, RawPtrLess<Image>> g_user_images = {};
std::set<std::shared_ptr<Buffer>, RawPtrLess<Buffer>> g_user_buffers = {};
std::set<std::shared_ptr<ShaderModule>, RawPtrLess<ShaderModule>> g_user_shader_modules = {};
std::set<std::shared_ptr<RenderPass>, RawPtrLess<RenderPass>> g_user_render_passes = {};
std::set<std::shared_ptr<VertexBuffer>, RawPtrLess<VertexBuffer>> g_user_vertex_buffers = {};
std::set<std::shared_ptr<GraphicsPipeline>, RawPtrLess<GraphicsPipeline>> g_user_graphics_pipelines = {};
std::set<std::shared_ptr<ComputePipeline>, RawPtrLess<ComputePipeline>> g_user_compute_pipelines = {};
std::set<std::shared_ptr<DescriptorPool>, RawPtrLess<DescriptorPool>> g_user_descriptor_pools = {};
std::set<std::shared_ptr<Sampler>, RawPtrLess<Sampler>> g_user_samplers = {};
std::set<std::shared_ptr<Framebuffer>, RawPtrLess<Framebuffer>> g_user_framebuffers = {};

// Instance
Instance::Instance(VkInstanceCreateInfo& info) {
	this->info = info;
	VkResult result = vkCreateInstance(&info, nullptr, &vk_instance);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create instance, error code = " << result << std::endl;
		exit(1);
	}
}
Instance::~Instance() {
	vkDestroyInstance(vk_instance, nullptr);
}

// Surface
Surface::Surface(void* window_data) {
	this->window = window_data;
#ifdef OTCV_WINDOW == GLFW
	if (glfwCreateWindowSurface(g_instance->vk_instance, (GLFWwindow*)window_data, nullptr, &vk_surface) != VK_SUCCESS) {
		std::cout << "Cannot create window surface" << std::endl;
		exit(1);
	}
#endif
}
Surface::~Surface() {
	vkDestroySurfaceKHR(g_instance->vk_instance, vk_surface, nullptr);
}

// Device
Device::Device(VkDeviceCreateInfo& info) {
	this->info = info;
	VkResult result = vkCreateDevice(g_physical_device.vk_physical_device, &info, nullptr, &vk_device);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot logical device, error code = " << result << std::endl;
		exit(1);
	}
}
Device::~Device() {
	vkDestroyDevice(vk_device, nullptr);
}

Sampler::Sampler(SamplerBuilder& builder) {
	VkResult result = vkCreateSampler(g_device->vk_device, &builder._info, nullptr, &vk_sampler);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create sampler, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(builder);
}
Sampler::~Sampler() {
	vkDestroySampler(g_device->vk_device, vk_sampler, nullptr);
}
void Sampler::destroy() {
	std::shared_ptr<Sampler> ptr(std::shared_ptr<Sampler>(), this);
	g_user_samplers.erase(ptr);
}

// semaphore
Semaphore::Semaphore(VkSemaphoreCreateInfo& info) {
	this->info = info;

	VkResult result = vkCreateSemaphore(g_device->vk_device, &info, nullptr, &vk_semaphore);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create semaphore, error code = " << result;
		exit(1);
	}
}
Semaphore::~Semaphore() {
	vkDestroySemaphore(g_device->vk_device, vk_semaphore, nullptr);
}
Semaphore* Semaphore::create() {
	VkSemaphoreCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	Semaphore* semaphore = new Semaphore(create_info);
	g_user_semaphores.insert(std::shared_ptr<Semaphore>(semaphore));
	return semaphore;
}
void Semaphore::destroy() {
	std::shared_ptr<Semaphore> ptr(std::shared_ptr<Semaphore>(), this);
	g_user_semaphores.erase(ptr);
}
// Fence
Fence::Fence(VkFenceCreateInfo& info) {
	this->info = info;

	VkResult result = vkCreateFence(g_device->vk_device, &info, nullptr, &vk_fence);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create fence, error code = " << result;
		exit(1);
	}
}
Fence::~Fence() {
	vkDestroyFence(g_device->vk_device, vk_fence, nullptr);
}

Fence* Fence::create(bool signaled) {
	VkFenceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	create_info.flags |= signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
	Fence* fence = new Fence(create_info);
	g_user_fences.insert(std::shared_ptr<Fence>(fence));
	return fence;
}
void Fence::destroy() {
	std::shared_ptr<Fence> ptr(std::shared_ptr<Fence>(), this);
	g_user_fences.erase(ptr);
}

// Descriptor pool
DescriptorSet::DescriptorSet(VkDescriptorSetAllocateInfo& info,
	const std::vector<VkDescriptorSetLayoutBinding>& bindings, 
	bool free_required) {
	VkResult result = vkAllocateDescriptorSets(g_device->vk_device, &info, &vk_desc_set);
	if (result != VK_SUCCESS) {
		std::cout << "cannot allocate descriptor set" << std::endl;
		exit(1);
	}
	this->alloc_info = info;
	this->bindings = bindings;
	this->free_required = free_required;
}
DescriptorSet::~DescriptorSet() {
	if (free_required) {
		vkFreeDescriptorSets(g_device->vk_device, alloc_info.descriptorPool, 1, &vk_desc_set);
	}
}
void DescriptorSet::bind_image_sampler(uint32_t binding, Image** p_images, Sampler** p_samplers, uint32_t array_start, uint32_t array_count) {
	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = (*(p_samplers + i))->vk_sampler;
		image_info.imageView = (*(p_images + i))->vk_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[i] = image_info;
	}

	assert(bindings[binding].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = bindings[binding].descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}
void DescriptorSet::bind_storage_image(uint32_t binding, Image** p_images, uint32_t array_start, uint32_t array_count) {
	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = VK_NULL_HANDLE;
		image_info.imageView = (*(p_images + i))->vk_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_infos[i] = image_info;
	}

	assert(bindings[binding].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = bindings[binding].descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}
void DescriptorSet::bind_buffer(uint32_t binding, Buffer** p_buffers, uint32_t array_start, uint32_t array_count) {
	std::vector<VkDescriptorBufferInfo> buffer_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = (*(p_buffers + i))->vk_buffer;
		buffer_info.offset = 0;
		buffer_info.range = VK_WHOLE_SIZE;
		buffer_infos[i] = buffer_info;
	}

	assert(bindings[binding].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
		bindings[binding].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = bindings[binding].descriptorType;
	write.pBufferInfo = buffer_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

DescriptorPool::DescriptorPool(DescriptorPoolBuilder& builder) {
	builder._info.poolSizeCount = builder._pool_sizes.size();
	builder._info.pPoolSizes = builder._pool_sizes.data();

	VkResult result = vkCreateDescriptorPool(g_device->vk_device, &builder._info, nullptr, &vk_desc_pool);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create descriptor pool, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(builder);
}
DescriptorPool::~DescriptorPool() {
	vkDestroyDescriptorPool(g_device->vk_device, vk_desc_pool, nullptr);
}
void DescriptorPool::destroy() {
	std::shared_ptr<DescriptorPool> ptr(std::shared_ptr<DescriptorPool>(), this);
	g_user_descriptor_pools.erase(ptr);
}
void DescriptorPool::reset() {
	vkResetDescriptorPool(g_device->vk_device, vk_desc_pool, 0);
	desc_sets.clear();
}
DescriptorSet* DescriptorPool::allocate(DescriptorSetLayout* set_layout) {
	VkDescriptorSetAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = vk_desc_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &set_layout->vk_desc_set_layout;
	
	DescriptorSet* set = new DescriptorSet(alloc_info,
		set_layout->bindings,
		builder._info.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
	desc_sets.insert(set);
	return set;
}
void DescriptorPool::free(DescriptorSet* set) {
	auto iter = desc_sets.find(set);
	if (iter != desc_sets.end()) {
		delete (*iter);
		desc_sets.erase(iter);
	}
}

// swapchain
Swapchain::Swapchain(VkSwapchainCreateInfoKHR info) {
	VkResult result = vkCreateSwapchainKHR(g_device->vk_device, &info, nullptr, &swapchain);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create swap chain. Error code = " << result << std::endl;
		exit(1);
	}

	// Acquire swap chain images
	uint32_t swap_chain_image_count;
	vkGetSwapchainImagesKHR(g_device->vk_device, swapchain, &swap_chain_image_count, nullptr);
	if (swap_chain_image_count == 0) {
		std::cout << "No image acquired from swap chain" << std::endl;
		exit(1);
	}
	// images.resize(swap_chain_image_count);
	std::vector<VkImage> vk_images(swap_chain_image_count);
	result = vkGetSwapchainImagesKHR(g_device->vk_device, swapchain, &swap_chain_image_count, vk_images.data());
	if (result != VK_SUCCESS) {
		std::cout << "Cannot acquire " << swap_chain_image_count << " images from swap chain, error code = " << result << std::endl;
		exit(1);
	}

	// following fields are set according to
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCreateSwapchainKHR.html#_description
	this->image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	this->image_info.imageType = VK_IMAGE_TYPE_2D;
	this->image_info.format = info.imageFormat;
	this->image_info.extent = { info.imageExtent.width, info.imageExtent.height, 1 };
	this->image_info.mipLevels = 1;
	this->image_info.arrayLayers = info.imageArrayLayers;
	this->image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	this->image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	this->image_info.usage = info.imageUsage;
	this->image_info.sharingMode = info.imageSharingMode;
	this->image_info.queueFamilyIndexCount = info.queueFamilyIndexCount;
	this->image_info.pQueueFamilyIndices = info.pQueueFamilyIndices;
	this->image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	for (VkImage i : vk_images) {
		this->images.push_back(i);

		// create view
		VkImageView view;
		VkImageViewCreateInfo image_view_create_info{};
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.image = i;
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.format = info.imageFormat;
		image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;
		vkCreateImageView(g_device->vk_device, &image_view_create_info, nullptr, &view);

		this->views.push_back(view);

		// create mock image
		Image* mock_image = (Image*)malloc(sizeof(Image));
		mock_image->vk_image = i;
		mock_image->vk_view = view;
		mock_image->vk_memory = VK_NULL_HANDLE;
		mock_image->builder._view_info = image_view_create_info;
		mock_image->builder._image_info = this->image_info;

		this->mock_images.push_back(mock_image);
	}

	this->swapchain_info = info;
}
Swapchain::~Swapchain() {
	images.clear();
	for (VkImageView& v : views) {
		vkDestroyImageView(g_device->vk_device, v, nullptr);
	}
	for (Image* mock_image : mock_images) {
		free(mock_image);
	}
	vkDestroySwapchainKHR(g_device->vk_device, swapchain, nullptr);
}
Image* Swapchain::mock_image(uint32_t id) {
	return this->mock_images[id];
}

// Command pool
CommandPool::CommandPool(VkCommandPoolCreateInfo& info) {
	this->info = info;
	VkResult result = vkCreateCommandPool(g_device->vk_device, &info, nullptr, &vk_command_pool);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create command pool, error code = " << result << std::endl;
		exit(1);
	}
}
CommandPool::~CommandPool() {
	for (auto& b : command_buffers) {
		delete b;
	}
	command_buffers.clear();
	vkDestroyCommandPool(g_device->vk_device, vk_command_pool, nullptr);
}
CommandPool* CommandPool::create(bool transient, bool allow_reset, bool user) {
	VkCommandPoolCreateFlags flags = 0;
	if (transient) {
		flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	}
	if (allow_reset) {
		flags |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	}

	VkCommandPoolCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	create_info.flags = flags;
	create_info.queueFamilyIndex = g_physical_device.queue_family_index;
	CommandPool* command_pool = new CommandPool(create_info);
	if (user) {
		g_user_command_pools.insert(std::shared_ptr<CommandPool>(command_pool));
	}
	else {
		g_command_pool.reset(command_pool);
	}
	return command_pool;
}
void CommandPool::destroy() {
	std::shared_ptr<CommandPool> ptr(std::shared_ptr<CommandPool>(), this);
	g_user_command_pools.erase(ptr);
}

CommandBuffer* CommandPool::allocate() {
	VkCommandBufferAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = vk_command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	CommandBuffer* buffer = new CommandBuffer(alloc_info);
	command_buffers.insert(buffer);
	return buffer;
}
void CommandPool::free(CommandBuffer* buffer) {
	auto iter = command_buffers.find(buffer);
	if (iter != command_buffers.end()) {
		delete (*iter);
		command_buffers.erase(iter);
	}
}

// command buffer
CommandBuffer::CommandBuffer(VkCommandBufferAllocateInfo& alloc_info) {
	VkResult result = vkAllocateCommandBuffers(g_device->vk_device, &alloc_info, &vk_command_buffer);
	if (result != VK_SUCCESS) {
		std::cout << "cannot allocate graphics command buffers, error code = " << result << std::endl;
		exit(1);
	}
	this->alloc_info = alloc_info;
}
CommandBuffer::~CommandBuffer() {
	vkFreeCommandBuffers(g_device->vk_device, alloc_info.commandPool, 1, &vk_command_buffer);
}

void CommandBuffer::begin(bool one_time) {
	VkCommandBufferBeginInfo begin_info{};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (one_time) {
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	}

	VkResult result = vkBeginCommandBuffer(vk_command_buffer, &begin_info);
	if (result != VK_SUCCESS) {
		std::cout << "cannot begin command buffer, error code = " << result << std::endl;
		exit(1);
	}
	this->begin_info = begin_info;
}
void CommandBuffer::end() {
	vkEndCommandBuffer(vk_command_buffer);
	begin_info = {};
}
void CommandBuffer::reset() {
	vkResetCommandBuffer(vk_command_buffer, 0);
}
void CommandBuffer::record(std::function<void(CommandBuffer*)> func, bool one_time) {
	this->begin(one_time);
	func(this);
	this->end();
}
//void RenderPass::cmd_begin(CommandBuffer* command_buffer, RenderPassBegin& begin) {
//	begin._info.renderPass = this->vk_render_pass;
//	begin._info.clearValueCount = begin._clear_values.size();
//	begin._info.pClearValues = begin._clear_values.data();
//
//	vkCmdBeginRenderPass(command_buffer->vk_command_buffer, &begin._info, VK_SUBPASS_CONTENTS_INLINE);
//	this->vk_begin = std::move(begin);
//}
//void RenderPass::cmd_end(CommandBuffer* command_buffer) {
//	vkCmdEndRenderPass(command_buffer->vk_command_buffer);
//	this->vk_begin = {};
//}

void CommandBuffer::cmd_begin_render_pass(RenderPass* pass, RenderPassBegin& begin) {
	begin._info.renderPass = pass->vk_render_pass;
	begin._info.clearValueCount = begin._clear_values.size();
	begin._info.pClearValues = begin._clear_values.data();
	
	vkCmdBeginRenderPass(this->vk_command_buffer, &begin._info, VK_SUBPASS_CONTENTS_INLINE);
	pass->vk_begin = std::move(begin);
}
void CommandBuffer::cmd_end_render_pass(RenderPass* pass) {
	vkCmdEndRenderPass(this->vk_command_buffer);
	pass->vk_begin = {};
}
void CommandBuffer::cmd_bind_graphics_pipeline(GraphicsPipeline* pipeline) {
	vkCmdBindPipeline(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vk_pipeline);
}
void CommandBuffer::cmd_bind_compute_pipeline(ComputePipeline* pipeline) {
	vkCmdBindPipeline(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->vk_pipeline);
}
void CommandBuffer::cmd_bind_vertex_buffer(VertexBuffer* vb, std::vector<VkDeviceSize> offsets) {
	uint32_t binding_count = vb->buffers.size();
	std::vector<VkBuffer> vk_buffers;
	for (Buffer* b : vb->buffers) {
		vk_buffers.push_back(b->vk_buffer);
	}
	if (offsets.size() < binding_count) {
		offsets.resize(binding_count, 0);
	}
	vkCmdBindVertexBuffers(this->vk_command_buffer, 0, binding_count, vk_buffers.data(), offsets.data());
}
void CommandBuffer::cmd_bind_index_buffer(Buffer* ib, VkIndexType type, VkDeviceSize offset) {
	vkCmdBindIndexBuffer(this->vk_command_buffer, ib->vk_buffer, offset, type);
}
void CommandBuffer::cmd_set_viewport(float width, float height, float x, float y, float min_depth, float max_depth) {
	VkViewport viewport{};
	viewport.x = x;
	viewport.y = y;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = min_depth;
	viewport.maxDepth = max_depth;
	vkCmdSetViewport(this->vk_command_buffer, 0, 1, &viewport);
}
void CommandBuffer::cmd_push_constant(GraphicsPipeline* pipeline, const void* data) {
	for (VkPushConstantRange& range : pipeline->pipeline_layout->push_const_ranges) {
		vkCmdPushConstants(this->vk_command_buffer,
			pipeline->pipeline_layout->vk_pipeline_layout,
			range.stageFlags,
			range.offset,
			range.size,
			(char*)data + range.offset);
	}
}
void CommandBuffer::cmd_bind_descriptor_set(GraphicsPipeline* pipeline, DescriptorSet* set) {
	vkCmdBindDescriptorSets(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->pipeline_layout->vk_pipeline_layout, 0, 1, &(set->vk_desc_set), 0, nullptr);
}
void CommandBuffer::cmd_draw_indexed(uint32_t index_count,
	uint32_t first_index,
	int32_t vertex_offset,
	uint32_t instance_count,
	uint32_t first_instance) {
	vkCmdDrawIndexed(this->vk_command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}
void CommandBuffer::cmd_dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
	vkCmdDispatch(vk_command_buffer,
		group_count_x,
		group_count_y,
		group_count_z);
}
void CommandBuffer::cmd_image_memory_barrier(Image* image, ResourceState from_state, ResourceState to_state, uint32_t mip, uint32_t layer) {
	transition_image_state(this, image, from_state, to_state, mip, layer);
}
void CommandBuffer::cmd_buffer_memory_barrier(Buffer* buffer, ResourceState from_state, ResourceState to_state) {
	transition_buffer_state(this, buffer, from_state, to_state);
}

// queue
void Queue::submit(QueueSubmit& info) {
	std::vector<VkSubmitInfo> submit_batches(info._batches.size());
	for (size_t i = 0; i < submit_batches.size(); ++i) {
		VkSubmitInfo& si = submit_batches[i];
		QueueSubmit::Batch& batch = info._batches[i];

		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.waitSemaphoreCount = batch._wait_semaphores.size();
		si.pWaitSemaphores = batch._wait_semaphores.data();
		si.pWaitDstStageMask = batch._wait_stages.data();
		si.commandBufferCount = batch._cmd_buffers.size();
		si.pCommandBuffers = batch._cmd_buffers.data();
		si.signalSemaphoreCount = batch._signal_semaphores.size();
		si.pSignalSemaphores = batch._signal_semaphores.data();
	}
	VkResult result = vkQueueSubmit(g_queue.vk_queue, submit_batches.size(), submit_batches.data(),
		info._fence ? info._fence->vk_fence : VK_NULL_HANDLE);

	if (result != VK_SUCCESS) {
		std::cout << "cannot submit to blit queue, error code = " << result << std::endl;
		exit(1);
	}
}

void Queue::idle_wait() {
	vkQueueWaitIdle(vk_queue);
}


// shader module
ShaderModule::ShaderModule(ShaderModuleBuilder& b, const char* spirv_code, size_t byte_size) {

	VkShaderModuleCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = byte_size;
	create_info.pCode = (uint32_t*)spirv_code;

	VkResult result = vkCreateShaderModule(g_device->vk_device, &create_info, nullptr, &vk_shader);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create shader module, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(b);
}
ShaderModule::~ShaderModule() {
	vkDestroyShaderModule(g_device->vk_device, vk_shader, nullptr);
}
void ShaderModule::destroy() {
	std::shared_ptr<ShaderModule> ptr(std::shared_ptr<ShaderModule>(), this);
	g_user_shader_modules.erase(ptr);
}

// vertex buffer
VertexBuffer::VertexBuffer(VertexBufferBuilder& b) {
	assert(b._data_handles.size() == b._buffer_builders.size());

	for (size_t i = 0; i < b._data_handles.size(); ++i) {
		auto& bb = b._buffer_builders[i];
		Buffer* buffer = new Buffer(bb);
		void* data = b._data_handles[i];
		if (data) {
			buffer->populate(data);
		}
		this->buffers.push_back(buffer);
	}
	b._buffer_builders.clear();
	b._data_handles.clear();

	this->builder = std::move(b);
}
VertexBuffer::~VertexBuffer() {
	for (auto& b : this->buffers) {
		delete b;
	}
	this->buffers.clear();
}
void VertexBuffer::destroy() {
	std::shared_ptr<VertexBuffer> ptr(std::shared_ptr<VertexBuffer>(), this);
	g_user_vertex_buffers.erase(ptr);
}

void VertexBuffer::resize(uint32_t binding, size_t size) {
	Buffer*& tgt_buffer = this->buffers[binding];
	if (size == tgt_buffer->builder._info.size) {
		return;
	}
	BufferBuilder b_builder = tgt_buffer->builder;
	delete tgt_buffer;
	b_builder.size(size);
	tgt_buffer = new Buffer(b_builder);
}

//void VertexBuffer::cmd_bind(CommandBuffer* command_buffer, std::vector<VkDeviceSize> offsets) {
//	uint32_t binding_count = this->buffers.size();
//	std::vector<VkBuffer> vk_buffers;
//	for (Buffer* b : buffers) {
//		vk_buffers.push_back(b->vk_buffer);
//	}
//	if (offsets.size() < binding_count) {
//		offsets.resize(binding_count, 0);
//	}
//	vkCmdBindVertexBuffers(command_buffer->vk_command_buffer, 0, binding_count, vk_buffers.data(), offsets.data());
//}


// image
Image::Image(ImageBuilder& builder) {
	// Check is physical device support this particular type of image memory requested by user
	VkImageFormatProperties props{};
	VkResult result = vkGetPhysicalDeviceImageFormatProperties(g_physical_device.vk_physical_device,
		builder._image_info.format, builder._image_info.imageType,
		VK_IMAGE_TILING_OPTIMAL, builder._image_info.usage, 0, &props);
	if (result != VK_SUCCESS) {
		std::cout << "Physical device is not capable of creating such image" << std::endl;
		exit(1);
	}

	// create image
	result = vkCreateImage(g_device->vk_device, &builder._image_info, nullptr, &vk_image);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create image, error code = " << result << std::endl;
		exit(1);
	}

	// allocate memory
	VkMemoryRequirements mem_requirements{};
	vkGetImageMemoryRequirements(g_device->vk_device, vk_image, &mem_requirements);
	VkMemoryAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_requirements.size;
	// device local only
	alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(g_device->vk_device, &alloc_info, nullptr, &vk_memory);
	if (result != VK_SUCCESS) {
		std::cout << "cannot allocate memory, error code = " << result << std::endl;
		exit(1);
	}
	vkBindImageMemory(g_device->vk_device, vk_image, vk_memory, 0);

	builder._view_info.image = this->vk_image;
	result = vkCreateImageView(g_device->vk_device, &builder._view_info, nullptr, &vk_view);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create image view, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(builder);
}
Image::~Image() {
	vkDestroyImage(g_device->vk_device, vk_image, nullptr);
	vkFreeMemory(g_device->vk_device, vk_memory, nullptr);
	vkDestroyImageView(g_device->vk_device, vk_view, nullptr);
}
void Image::destroy() {
	std::shared_ptr<Image> ptr(std::shared_ptr<Image>(), this);
	g_user_images.erase(ptr);
}
void Image::populate(void* data, size_t byte_size, 
	ResourceState target_state, ResourceState current_state) {
	staging_queued_copy(data, byte_size, this, current_state, target_state);
}
void Image::initialize_state(ResourceState target_state, ResourceState current_state) {
	CommandBuffer* command_buffer = otcv::begin_single_time_command_buffer();
	command_buffer->cmd_image_memory_barrier(this, current_state, target_state);
	end_single_time_command_buffer(command_buffer);
}

// buffer
Buffer::Buffer(BufferBuilder& builder) {
	VkResult result = vkCreateBuffer(g_device->vk_device, &builder._info, nullptr, &vk_buffer);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create buffer, error code = " << result << std::endl;
		exit(1);
	}

	VkMemoryRequirements mem_requirements;
	vkGetBufferMemoryRequirements(g_device->vk_device, vk_buffer, &mem_requirements);

	VkMemoryAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, builder._mem_props);
	result = vkAllocateMemory(g_device->vk_device, &alloc_info, nullptr, &vk_memory);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot allocate memory, error code = " << result << std::endl;
		exit(1);
	}

	result = vkBindBufferMemory(g_device->vk_device, vk_buffer, vk_memory, 0);
	if (result != VK_SUCCESS) {
		std::cout << "cannot bind memory, error code = " << result << std::endl;
		exit(1);
	}

	if (builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(g_device->vk_device, vk_memory, 0, builder._info.size, 0, &mapped);
	}

	this->builder = std::move(builder);
}

Buffer::~Buffer() {
	if ((builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (mapped != nullptr)) {
		vkUnmapMemory(g_device->vk_device, vk_memory);
		mapped = nullptr;
	}
	vkDestroyBuffer(g_device->vk_device, vk_buffer, nullptr);
	vkFreeMemory(g_device->vk_device, vk_memory, nullptr);
}
void Buffer::destroy() {
	std::shared_ptr<Buffer> ptr(std::shared_ptr<Buffer>(), this);
	g_user_buffers.erase(ptr);
}
void Buffer::populate(void* data) {
	if (!data) {
		return;
	}
	if (builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		copy_host_mapped(data, 0, builder._info.size)->flush();
	}
	else {
		staging_queued_copy(data, this);
	}
}
Buffer* Buffer::copy_host_mapped(void* data, uint32_t offset, uint32_t size) {
	// cannot perform mapped memory copy on host invisible memory
	assert(builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	assert(mapped);
	memcpy((char*)(mapped)+ offset, data, size);
	return this;
}
void Buffer::flush() {
	if (builder._mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
		return;
	}
	VkMappedMemoryRange range{};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = vk_memory;
	range.offset = 0;
	range.size = builder._info.size;
	vkFlushMappedMemoryRanges(g_device->vk_device, 1, &range);
}

// render pass
RenderPass::RenderPass(RenderPassBuilder& builder) {
	std::vector<VkAttachmentDescription> attachment_descs;
	for (auto& a : builder._attachments) {
		attachment_descs.push_back(a._desc);
	}

	std::vector<VkSubpassDescription> subpass_descs;
	for (auto& s : builder._subpasses) {
		VkSubpassDescription desc{};
		desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		desc.inputAttachmentCount = s._refs_input.size();
		desc.pInputAttachments = s._refs_input.data();
		desc.colorAttachmentCount = s._refs_color.size();
		desc.pColorAttachments = s._refs_color.data();
		desc.pDepthStencilAttachment = s._ref_depth_stencil.data();
		subpass_descs.push_back(desc);
	}

	std::vector<VkSubpassDependency> dep_descs;
	for (auto& d : builder._dependencies) {
		dep_descs.push_back(d._dep);
	}

	VkRenderPassCreateInfo render_pass_info{};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = attachment_descs.size();
	render_pass_info.pAttachments = attachment_descs.data();
	render_pass_info.subpassCount = subpass_descs.size();
	render_pass_info.pSubpasses = subpass_descs.data();
	render_pass_info.dependencyCount = dep_descs.size();
	render_pass_info.pDependencies = dep_descs.data();

	VkResult result = vkCreateRenderPass(g_device->vk_device, &render_pass_info, nullptr, &vk_render_pass);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create render pass, error code = " << result << std::endl;
	}

	this->builder = std::move(builder);
}
RenderPass::~RenderPass() {
	vkDestroyRenderPass(g_device->vk_device, vk_render_pass, nullptr);
}
void RenderPass::destroy() {
	std::shared_ptr<RenderPass> ptr(std::shared_ptr<RenderPass>(), this);
	g_user_render_passes.erase(ptr);
}

// pipeline
DescriptorSetLayout::DescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
	VkDescriptorSetLayoutCreateInfo set_layout_create{};
	set_layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout_create.bindingCount = bindings.size();
	set_layout_create.pBindings = bindings.data();
	VkResult result = vkCreateDescriptorSetLayout(g_device->vk_device, &set_layout_create, nullptr, &this->vk_desc_set_layout);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create descriptor set layout, error code = " << result << std::endl;
		exit(1);
	}

	this->create_info = set_layout_create;
	this->bindings = bindings;
}
DescriptorSetLayout::~DescriptorSetLayout() {
	vkDestroyDescriptorSetLayout(g_device->vk_device, vk_desc_set_layout, nullptr);
}

PipelineLayout::PipelineLayout(const std::vector<DescriptorSetLayout>& set_layouts, const std::vector<VkPushConstantRange>& ranges) {
	std::vector<VkDescriptorSetLayout> vk_desc_set_layouts;
	for (auto& layout : set_layouts) {
		vk_desc_set_layouts.push_back(layout.vk_desc_set_layout);
	}
	
	VkPipelineLayoutCreateInfo pipeline_layout_create{};
	pipeline_layout_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create.setLayoutCount = vk_desc_set_layouts.size();
	pipeline_layout_create.pSetLayouts = vk_desc_set_layouts.data();
	pipeline_layout_create.pushConstantRangeCount = ranges.size();
	pipeline_layout_create.pPushConstantRanges = ranges.data();
	VkResult result = vkCreatePipelineLayout(g_device->vk_device, &pipeline_layout_create, nullptr, &this->vk_pipeline_layout);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create pipeline layout, error code = " << result << std::endl;
		exit(1);
	}

	this->create_info = pipeline_layout_create;
	this->push_const_ranges = ranges;
}
PipelineLayout::~PipelineLayout() {
	vkDestroyPipelineLayout(g_device->vk_device, vk_pipeline_layout, nullptr);
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipelineBuilder& builder) {
	// generate shader create info
	std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
	if (builder._vertex_shader) {
		VkPipelineShaderStageCreateInfo stage_create{};
		stage_create.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create.stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create.module = builder._vertex_shader->vk_shader;
		stage_create.pName = "main";
		shader_stages.push_back(stage_create);
	}
	if (builder._fragment_shader) {
		VkPipelineShaderStageCreateInfo stage_create{};
		stage_create.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create.module = builder._fragment_shader->vk_shader;
		stage_create.pName = "main";
		shader_stages.push_back(stage_create);
	}

	// generate vertex state
	builder._vertex_state.vertexBindingDescriptionCount = builder._vertex_bindings.size();
	builder._vertex_state.pVertexBindingDescriptions = builder._vertex_bindings.data();
	builder._vertex_state.vertexAttributeDescriptionCount = builder._vertex_attributes.size();
	builder._vertex_state.pVertexAttributeDescriptions = builder._vertex_attributes.data();

	// generate color blend attachment state
	VkPipelineColorBlendAttachmentState no_blend_state{};
	no_blend_state.blendEnable = VK_FALSE;
	no_blend_state.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;
	std::vector<VkPipelineColorBlendAttachmentState> blend_states(
		builder._render_pass->builder._subpasses[builder._subpass]._refs_color.size(),
		no_blend_state);
	for (auto& ele : builder._attachment_blend_states_map) {
		uint32_t attachment_idx = ele.first;
		assert(attachment_idx < blend_states.size());
		VkPipelineColorBlendAttachmentState& blend_state = ele.second._attachment_blend;
		blend_states[attachment_idx] = blend_state;
	}
	builder._blend_state.attachmentCount = blend_states.size();
	builder._blend_state.pAttachments = blend_states.data();

	// generate dynamic state
	builder._dynamic_state.dynamicStateCount = builder._dynamic_states.size();
	builder._dynamic_state.pDynamicStates = builder._dynamic_states.data();

	// generate descriptor set layout
	// cache these layouts in the future

	std::vector<std::vector<VkDescriptorSetLayoutBinding>> layout_binding_set;
	auto collect_uniforms = [&](ShaderModuleBuilder& shader_builder, VkShaderStageFlags stage) {
		for (auto& p : shader_builder._uniforms) {
			uint16_t set;
			uint16_t binding;
			otcv::unpack(p.first, set, binding);
			if (set >= layout_binding_set.size()) {
				layout_binding_set.resize(set + 1);
			}
			if (binding >= layout_binding_set[set].size()) {
				layout_binding_set[set].resize(binding + 1);
			}
			VkDescriptorSetLayoutBinding vk_layout_binding = {};
			vk_layout_binding.binding = binding;
			vk_layout_binding.descriptorType = p.second._type;
			vk_layout_binding.descriptorCount = p.second._array_count;
			vk_layout_binding.stageFlags = stage;
			layout_binding_set[set][binding] = vk_layout_binding;
		}
	};

	std::vector<VkPushConstantRange> push_constant_ranges;
	auto collect_push_constants = [&](ShaderModuleBuilder& shader_builder, VkShaderStageFlags stage) {
		uint16_t pc_offset;
		uint16_t pc_size;
		otcv::unpack(shader_builder._push_constant_offset_size, pc_offset, pc_size);
		if (pc_size == 0) {
			return;
		}
		VkPushConstantRange range{};
		range.offset = pc_offset;
		range.size = pc_size;
		range.stageFlags = stage;
		push_constant_ranges.push_back(range);
	};

	if (builder._vertex_shader) {
		collect_uniforms(builder._vertex_shader->builder, VK_SHADER_STAGE_VERTEX_BIT);
		collect_push_constants(builder._vertex_shader->builder, VK_SHADER_STAGE_VERTEX_BIT);
	}
	if (builder._fragment_shader) {
		collect_uniforms(builder._fragment_shader->builder, VK_SHADER_STAGE_FRAGMENT_BIT);
		collect_push_constants(builder._fragment_shader->builder, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	
	// create descriptor set layouts and pipeline layout
	for (auto& set_bindings : layout_binding_set) {
		desc_set_layouts.emplace_back(set_bindings);
	}
	pipeline_layout = new PipelineLayout(desc_set_layouts, push_constant_ranges);

	VkGraphicsPipelineCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.stageCount = shader_stages.size();
	create_info.pStages = shader_stages.data();
	create_info.pVertexInputState = &builder._vertex_state;
	create_info.pInputAssemblyState = &builder._assembly_state;
	create_info.pViewportState = &builder._viewport_state;
	create_info.pRasterizationState = &builder._rast_state;
	create_info.pMultisampleState = &builder._ms_state;
	create_info.pDepthStencilState = &builder._depth_stencil_state;
	create_info.pColorBlendState = &builder._blend_state;
	create_info.pDynamicState = &builder._dynamic_state;
	create_info.layout = pipeline_layout->vk_pipeline_layout;
	create_info.renderPass = builder._render_pass->vk_render_pass;
	create_info.subpass = builder._subpass;
	VkResult result = vkCreateGraphicsPipelines(g_device->vk_device, VK_NULL_HANDLE, 1, &create_info, nullptr, &vk_pipeline);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create pipeline, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(builder);
}
GraphicsPipeline::~GraphicsPipeline() {
	delete pipeline_layout;
	vkDestroyPipeline(g_device->vk_device, vk_pipeline, nullptr);
}
void GraphicsPipeline::destroy() {
	std::shared_ptr<GraphicsPipeline> ptr(std::shared_ptr<GraphicsPipeline>(), this);
	g_user_graphics_pipelines.erase(ptr);
}

void GraphicsPipeline::cmd_bind(CommandBuffer* cmd_buffer) {
	vkCmdBindPipeline(cmd_buffer->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);
}
//void GraphicsPipeline::cmd_bind_descriptor_set(CommandBuffer* cmd_buffer, DescriptorSet* set) {
//	vkCmdBindDescriptorSets(cmd_buffer->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
//		pipeline_layout->vk_pipeline_layout, 0, 1, &(set->vk_desc_set), 0, nullptr);
//}
//void GraphicsPipeline::cmd_push_constant(CommandBuffer* cmd_buffer, const void* data, VkShaderStageFlags stage) {
//	auto iter = std::find_if(
//		pipeline_layout->push_const_ranges.begin(),
//		pipeline_layout->push_const_ranges.end(),
//		[&](VkPushConstantRange& range) {
//		return range.stageFlags == stage;
//	});
//
//	if (iter != pipeline_layout->push_const_ranges.end()) {
//		vkCmdPushConstants(cmd_buffer->vk_command_buffer, pipeline_layout->vk_pipeline_layout, stage,
//			iter->offset,
//			iter->size, data);
//	}
//}

ComputePipeline::ComputePipeline(ShaderModule* compute_shader) {
	std::vector<std::vector<VkDescriptorSetLayoutBinding>> layout_binding_set;
	auto collect_uniforms = [&]() {
		for (auto& p : compute_shader->builder._uniforms) {
			uint16_t set;
			uint16_t binding;
			otcv::unpack(p.first, set, binding);
			if (set >= layout_binding_set.size()) {
				layout_binding_set.resize(set + 1);
			}
			if (binding >= layout_binding_set[set].size()) {
				layout_binding_set[set].resize(binding + 1);
			}
			VkDescriptorSetLayoutBinding vk_layout_binding = {};
			vk_layout_binding.binding = binding;
			vk_layout_binding.descriptorType = p.second._type;
			vk_layout_binding.descriptorCount = p.second._array_count;
			vk_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			layout_binding_set[set][binding] = vk_layout_binding;
		}
	};

	collect_uniforms();

	for (auto& set_bindings : layout_binding_set) {
		desc_set_layouts.emplace_back(set_bindings);
	}

	uint16_t pc_offset;
	uint16_t pc_size;
	otcv::unpack(compute_shader->builder._push_constant_offset_size, pc_offset, pc_size);
	if (pc_size > 0) {
		VkPushConstantRange push_constant_range;
		push_constant_range.offset = pc_offset;
		push_constant_range.size = pc_size;
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeline_layout = new PipelineLayout(desc_set_layouts, { push_constant_range });
	}
	else {
		pipeline_layout = new PipelineLayout(desc_set_layouts, {});
	}

	VkPipelineShaderStageCreateInfo stage_create{};
	stage_create.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage_create.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_create.module = compute_shader->vk_shader;
	stage_create.pName = "main";

	VkComputePipelineCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	create_info.stage = stage_create;
	create_info.layout = pipeline_layout->vk_pipeline_layout;
	VkResult result = vkCreateComputePipelines(g_device->vk_device, VK_NULL_HANDLE, 1, &create_info, nullptr, &vk_pipeline);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create compute pipeline" << std::endl;
		exit(1);
	}

	this->compute_shader = compute_shader;
	this->info = create_info;
}
ComputePipeline::~ComputePipeline() {
	delete pipeline_layout;
	vkDestroyPipeline(g_device->vk_device, vk_pipeline, nullptr);
}

ComputePipeline* ComputePipeline::create(ShaderModule* compute_shader) {
	ComputePipeline* pipeline = new ComputePipeline(compute_shader);
	g_user_compute_pipelines.insert(std::shared_ptr<ComputePipeline>(pipeline));
	return pipeline;
}
void ComputePipeline::destroy() {
	std::shared_ptr<ComputePipeline> ptr(std::shared_ptr<ComputePipeline>(), this);
	g_user_compute_pipelines.erase(ptr);
}
void ComputePipeline::cmd_bind(CommandBuffer* cmd_buffer) {
	vkCmdBindPipeline(cmd_buffer->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline);
}
void ComputePipeline::cmd_bind_descriptor_set(CommandBuffer* cmd_buffer, DescriptorSet* set) {
	vkCmdBindDescriptorSets(cmd_buffer->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout->vk_pipeline_layout, 0, 1, &(set->vk_desc_set), 0, nullptr);
}
void ComputePipeline::cmd_push_constant(CommandBuffer* cmd_buffer, const void* data) {	
	if (pipeline_layout->push_const_ranges.empty()) {
		return;
	}
	vkCmdPushConstants(cmd_buffer->vk_command_buffer, pipeline_layout->vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
		this->pipeline_layout->push_const_ranges[0].offset,
		this->pipeline_layout->push_const_ranges[0].size, data);
}

Framebuffer::Framebuffer(FramebufferBuilder& builder) {
	builder._info.attachmentCount = builder._attachments.size();
	builder._info.pAttachments = builder._attachments.data();

	VkResult result = vkCreateFramebuffer(g_device->vk_device, &builder._info, nullptr, &vk_framebuffer);
	if (result != VK_SUCCESS) {
		std::cout << "cannor create framebuffer, error code = " << result << std::endl;
		exit(1);
	}

	this->builder = std::move(builder);
}
Framebuffer::~Framebuffer() {
	vkDestroyFramebuffer(g_device->vk_device, vk_framebuffer, nullptr);
}
void Framebuffer::destroy() {
	std::shared_ptr<Framebuffer> ptr(std::shared_ptr<Framebuffer>(), this);
	g_user_framebuffers.erase(ptr);
}

}