#include "otcv.h"
#include "otcv_globals.h"
#include "otcv_config.h"
#include "otcv_utils_internal.h"

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
std::set<std::shared_ptr<AccelerationStructure>, RawPtrLess<AccelerationStructure>> g_user_acc_structs = {};
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
#if OTCV_WINDOW == GLFW
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

void Fence::wait_reset() {
	vkWaitForFences(g_device->vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(g_device->vk_device, 1, &vk_fence);
}

void Fence::wait() {
	vkWaitForFences(g_device->vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX);
}

void Fence::reset() {
	vkResetFences(g_device->vk_device, 1, &vk_fence);
}

bool Fence::is_signaled() {
	VkResult result = vkGetFenceStatus(g_device->vk_device, vk_fence);
	// 3 possible outcomes: VK_SUCCESS, VK_NOT_READY, VK_ERROR_DEVICE_LOST
	if (result != VK_SUCCESS && result != VK_NOT_READY) {
		std::cout << "Unexpected fence status, error code = " << result << std::endl;
		exit(1);
	}
	return result == VK_SUCCESS;
}

// Descriptor pool
DescriptorSet::DescriptorSet(VkDescriptorSet vk_desc_set, VkDescriptorSetAllocateInfo alloc_info,
	const std::vector<VkDescriptorSetLayoutBinding>& bindings, 
	bool free_required) {
	this->vk_desc_set = vk_desc_set;
	this->alloc_info = alloc_info;
	this->bindings = bindings;
	this->free_required = free_required;
}
DescriptorSet::~DescriptorSet() {
	if (free_required) {
		vkFreeDescriptorSets(g_device->vk_device, alloc_info.descriptorPool, 1, &vk_desc_set);
	}
}
void DescriptorSet::bind_image_sampler(uint32_t binding, Image** p_images, Sampler** p_samplers, uint32_t array_start, uint32_t array_count) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for image sampler" << std::endl;
		assert(false);
		return;
	}

	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = (*(p_samplers + i))->vk_sampler;
		image_info.imageView = (*(p_images + i))->vk_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[i] = image_info;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = iter->descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}
void DescriptorSet::bind_storage_image(uint32_t binding, Image** p_images, uint32_t array_start, uint32_t array_count) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for storage image" << std::endl;
		assert(false);
		return;
	}

	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = VK_NULL_HANDLE;
		image_info.imageView = (*(p_images + i))->vk_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_infos[i] = image_info;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = iter->descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

void DescriptorSet::bind_sampler(uint32_t binding, Sampler** p_samplers, uint32_t array_start, uint32_t array_count) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for sampler" << std::endl;
		assert(false);
		return;
	}

	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = (*(p_samplers + i))->vk_sampler;
		image_info.imageView = VK_NULL_HANDLE;
		image_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_infos[i] = image_info;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = iter->descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

void DescriptorSet::bind_sampled_image(uint32_t binding, Image** p_images, uint32_t array_start, uint32_t array_count) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for sampler" << std::endl;
		assert(false);
		return;
	}

	std::vector<VkDescriptorImageInfo> image_infos(array_count);
	for (uint32_t i = 0; i < array_count; ++i) {
		VkDescriptorImageInfo image_info{};
		image_info.sampler = VK_NULL_HANDLE;
		image_info.imageView = (*(p_images + i))->vk_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[i] = image_info;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_start;
	write.descriptorCount = array_count;
	write.descriptorType = iter->descriptorType;
	write.pImageInfo = image_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

void DescriptorSet::bind_buffer(uint32_t binding, Buffer* buffer, VkDeviceSize offset, VkDeviceSize range) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for sampler" << std::endl;
		assert(false);
		return;
	}

	VkDescriptorBufferInfo buffer_info{};
	buffer_info.buffer = buffer->vk_buffer;
	buffer_info.offset = offset;
	buffer_info.range = range;

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
		iter->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
		iter->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = iter->descriptorType;
	write.pBufferInfo = &buffer_info;

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

void DescriptorSet::bind_buffer_array(uint32_t binding, Buffer* buffer, VkDeviceSize offset, VkDeviceSize stride, uint32_t count) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for sampler" << std::endl;
		assert(false);
		return;
	}

	std::vector<VkDescriptorBufferInfo> buffer_infos(count);
	for (uint32_t i = 0; i < count; ++i) {
		buffer_infos[i].buffer = buffer->vk_buffer;
		buffer_infos[i].offset = offset + stride * i;
		buffer_infos[i].range = stride;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = count;
	write.descriptorType = iter->descriptorType;
	write.pBufferInfo = buffer_infos.data();

	vkUpdateDescriptorSets(g_device->vk_device, 1, &write, 0, nullptr);
}

/*
* bind something like this with one update call
* set = 1, binding = 0, buffer {}
*		....
* set = 1, bindind = 4, buffer {}
*/
void DescriptorSet::bind_consecutive_buffers(uint32_t binding_start, uint32_t binding_count, Buffer** p_buffers) {
	std::vector<VkWriteDescriptorSet> writes(binding_count);
	std::vector<VkDescriptorBufferInfo> infos(binding_count);

	// find the starting binding
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding_start; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate start binding = " << binding_start << " for buffer" << std::endl;
		assert(false);
		return;
	}

	for (uint32_t i = 0; i < binding_count; ++i, ++iter) {
		// check if consecutive bindings exist
		if (iter->binding != i + binding_start) {
			std::cout << "incontinuous binding at " << i + binding_start << " for buffer" << std::endl;
			assert(false);
			return;
		}

		VkDescriptorBufferInfo& info = infos[i];
		info.buffer = (*(p_buffers + i))->vk_buffer;
		info.offset = 0;
		info.range = VK_WHOLE_SIZE;
		
		assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
			iter->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

		VkWriteDescriptorSet& write = writes[i];
		write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk_desc_set;
		write.dstBinding = i + binding_start;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = iter->descriptorType;
		write.pBufferInfo = &info;
	}

	vkUpdateDescriptorSets(g_device->vk_device, writes.size(), writes.data(), 0, nullptr);
}

void DescriptorSet::bind_consecutive_sampled_images(uint32_t binding_start, uint32_t binding_count, Image** p_images) {
	std::vector<VkWriteDescriptorSet> writes(binding_count);
	std::vector<VkDescriptorImageInfo> infos(binding_count);

	// find the starting binding
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding_start; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate start binding = " << binding_start << " for sampled images" << std::endl;
		assert(false);
		return;
	}

	for (uint32_t i = 0; i < binding_count; ++i, ++iter) {
		// check if consecutive bindings exist
		if (iter->binding != i + binding_start) {
			std::cout << "incontinuous binding at " << i + binding_start << " for smapled images" << std::endl;
			assert(false);
			return;
		}

		VkDescriptorImageInfo& info = infos[i];
		info.sampler = VK_NULL_HANDLE;
		info.imageView = (*(p_images + i))->vk_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

		VkWriteDescriptorSet& write = writes[i];
		write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk_desc_set;
		write.dstBinding = i + binding_start;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &info;
	}

	vkUpdateDescriptorSets(g_device->vk_device, writes.size(), writes.data(), 0, nullptr);
}

/*
* bind something like this with one update call
* set = 1, binding = 0, texture2D
*		....
* set = 1, bindind = 4, texture2D
*/
void DescriptorSet::bind_consecutive_storage_images(uint32_t binding_start, uint32_t binding_count, Image** p_images) {
	std::vector<VkWriteDescriptorSet> writes(binding_count);
	std::vector<VkDescriptorImageInfo> infos(binding_count);

	// find the starting binding
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding_start; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate start binding = " << binding_start << " for storage images" << std::endl;
		assert(false);
		return;
	}

	for (uint32_t i = 0; i < binding_count; ++i, ++iter) {
		// check if consecutive bindings exist
		if (iter->binding != i + binding_start) {
			std::cout << "incontinuous binding at " << i + binding_start << " for storage images" << std::endl;
			assert(false);
			return;
		}

		VkDescriptorImageInfo& info = infos[i];
		info.sampler = VK_NULL_HANDLE;
		info.imageView = (*(p_images + i))->vk_view;
		info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		VkWriteDescriptorSet& write = writes[i];
		write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk_desc_set;
		write.dstBinding = i + binding_start;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		write.pImageInfo = &info;
	}

	vkUpdateDescriptorSets(g_device->vk_device, writes.size(), writes.data(), 0, nullptr);
}

void DescriptorSet::bind_acceleration_structure(uint32_t binding, AccelerationStructure* as) {
	auto iter = std::find_if(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b) {return b.binding == binding; });
	if (iter == bindings.end()) {
		std::cout << "Cannot locate binding = " << binding << " for acceleration structure" << std::endl;
		assert(false);
		return;
	}

	assert(iter->descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);

	VkWriteDescriptorSetAccelerationStructureKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	info.pNext = NULL;
	info.accelerationStructureCount = 1;
	info.pAccelerationStructures = &as->vk_as;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = &info;
	write.dstSet = vk_desc_set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = iter->descriptorType;

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
DescriptorSet* DescriptorPool::allocate(DescriptorSetLayout* set_layout, std::function<void()> oom_callback) {
	VkDescriptorSetAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = vk_desc_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &set_layout->vk_desc_set_layout;
	
	VkDescriptorSet vk_desc_set;
	VkResult result = vkAllocateDescriptorSets(g_device->vk_device, &alloc_info, &vk_desc_set);
	if (result != VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
			oom_callback();
			return nullptr;
		}
		else {
			std::cout << "cannot allocate descriptor set" << std::endl;
			exit(1);
		}
	}

	DescriptorSet* set = new DescriptorSet(vk_desc_set, alloc_info,
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
	VkResult result = vkCreateSwapchainKHR(g_device->vk_device, &info, nullptr, &vk_swapchain);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create swap chain. Error code = " << result << std::endl;
		exit(1);
	}

	// Acquire swap chain images
	uint32_t swap_chain_image_count;
	vkGetSwapchainImagesKHR(g_device->vk_device, vk_swapchain, &swap_chain_image_count, nullptr);
	if (swap_chain_image_count == 0) {
		std::cout << "No image acquired from swap chain" << std::endl;
		exit(1);
	}
	// images.resize(swap_chain_image_count);
	std::vector<VkImage> vk_images(swap_chain_image_count);
	result = vkGetSwapchainImagesKHR(g_device->vk_device, vk_swapchain, &swap_chain_image_count, vk_images.data());
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
		VkResult view_result = vkCreateImageView(g_device->vk_device, &image_view_create_info, nullptr, &view);
		if (view_result != VK_SUCCESS) {
			std::cout << "Cannot create image view for mock image, error code = " << view_result << std::endl;
			exit(1);
		}

		this->views.push_back(view);

		// create mock image
		Image* mock_image = new Image();
		mock_image->vk_image = i;
		mock_image->vk_view = view;
		mock_image->builder._view_info = image_view_create_info;
		mock_image->builder._image_info = this->image_info;

		this->mock_images.push_back(mock_image);
	}

	this->swapchain_info = info;
}
Swapchain::~Swapchain() {
	images.clear();
	for (Image* mock_image : mock_images) {
		// mock_image->wait_for_async(); there is no chance a swapchain image will require async populate
		assert(!mock_image->async_ctx);
		mock_image->vk_image = VK_NULL_HANDLE;
		delete mock_image;
	}
	vkDestroySwapchainKHR(g_device->vk_device, vk_swapchain, nullptr);
}
Image* Swapchain::mock_image(uint32_t id) {
	return this->mock_images[id];
}

void Swapchain::recreate(void* window_data) {
	VkSurfaceCapabilitiesKHR surface_caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &surface_caps);

	// Get the window size, measured in pixels
	// WIDTH and HEIGHT values set at glfw initialization are measured in screen coordinates.
	// Screen coordinates and pixels may not be the same on MacOS. Very likely to take the same value on Windows.
	int width_pixels;
	int height_pixels;
#if OTCV_WINDOW == GLFW
	glfwGetFramebufferSize((GLFWwindow*)window_data, &width_pixels, &height_pixels);
#else
	std::cout << "Window systems other that glfw not supported" << std::endl;
	exit(1);
#endif

	VkExtent2D window_extent;
	// TODO: windows extent may not be of the exact value as that of window dimensions. Deal with this
	window_extent.width = std::clamp((uint32_t)width_pixels, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
	window_extent.height = std::clamp((uint32_t)height_pixels, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);

	// update swapchain_info
	if (surface_caps.maxImageCount == 0) {
		this->swapchain_info.minImageCount = surface_caps.minImageCount + 1;
	}
	else {
		this->swapchain_info.minImageCount = std::min(surface_caps.minImageCount + 1, surface_caps.maxImageCount);
	}
	this->swapchain_info.imageExtent = window_extent;
	this->swapchain_info.preTransform = surface_caps.currentTransform;
	/*
	oldSwapchain provides a deferred mechanism that allows acquired image to be destroyed later even though vkDestroySwapchainKHR is called immediately
	https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html#:~:text=Upon%20calling%20vkCreateSwapchainKHR%20with%20an%20oldSwapchain%20that%20is%20not%20VK_NULL_HANDLE%2C%20any,can%20destroy%20oldSwapchain%20to%20free%20all%20memory%20associated%20with%20oldSwapchain.
	this field doesn't matter if the main loop waits idle,
	*/
	this->swapchain_info.oldSwapchain = vk_swapchain; 

	// update image_info
	this->image_info.format = this->swapchain_info.imageFormat;
	this->image_info.extent = { this->swapchain_info.imageExtent.width, this->swapchain_info.imageExtent.height, 1 };

	// destroy views and mock images
	this->images.clear(); // these images are managed by swapchain
	this->views.clear();
	for (Image* mock_image : this->mock_images) {
		// mock_image->wait_for_async(); there is no chance a swapchain image will require async populate
		assert(!mock_image->async_ctx);
		mock_image->vk_image = VK_NULL_HANDLE;
		delete mock_image;
	}
	this->mock_images.clear();

	// recreate swapchain
	VkSwapchainKHR new_swapchain;
	VkResult result = vkCreateSwapchainKHR(g_device->vk_device, &swapchain_info, nullptr, &new_swapchain);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create swap chain. Error code = " << result << std::endl;
		exit(1);
	}
	vkDestroySwapchainKHR(g_device->vk_device, this->vk_swapchain, nullptr);
	this->vk_swapchain = new_swapchain;

	// reacquire swapchain images
	uint32_t swap_chain_image_count;
	vkGetSwapchainImagesKHR(g_device->vk_device, vk_swapchain, &swap_chain_image_count, nullptr);
	if (swap_chain_image_count == 0) {
		std::cout << "No image acquired from swap chain" << std::endl;
		exit(1);
	}
	std::vector<VkImage> vk_images(swap_chain_image_count);
	result = vkGetSwapchainImagesKHR(g_device->vk_device, vk_swapchain, &swap_chain_image_count, vk_images.data());
	if (result != VK_SUCCESS) {
		std::cout << "Cannot acquire " << swap_chain_image_count << " images from swap chain, error code = " << result << std::endl;
		exit(1);
	}

	// recreate images, views and mock_images
	for (VkImage i : vk_images) {
		this->images.push_back(i);

		// create view
		VkImageView view;
		VkImageViewCreateInfo image_view_create_info{};
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.image = i;
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.format = this->swapchain_info.imageFormat;
		image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;
		VkResult view_result = vkCreateImageView(g_device->vk_device, &image_view_create_info, nullptr, &view);
		if (view_result != VK_SUCCESS) {
			std::cout << "Cannot create image view for mock image, error code = " << view_result << std::endl;
			exit(1);
		}

		this->views.push_back(view);

		// create mock image
		Image* mock_image = new Image();
		mock_image->vk_image = i;
		mock_image->vk_view = view;
		mock_image->builder._view_info = image_view_create_info;
		mock_image->builder._image_info = this->image_info;

		this->mock_images.push_back(mock_image);
	}
}

// Command pool
CommandPool::CommandPool(bool transient, bool allow_individual_reset) {
	VkCommandPoolCreateFlags flags = 0;
	if (transient) {
		flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	}
	if (allow_individual_reset) {
		flags |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	}

	VkCommandPoolCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	create_info.flags = flags;
	create_info.queueFamilyIndex = g_physical_device.queue_family_index;
	this->info = create_info;
	VkResult result = vkCreateCommandPool(g_device->vk_device, &info, nullptr, &vk_command_pool);
	if (result != VK_SUCCESS) {
		std::cout << "Cannot create command pool, error code = " << result << std::endl;
		exit(1);
	}
}
CommandPool::~CommandPool() {
	vkDestroyCommandPool(g_device->vk_device, vk_command_pool, nullptr);
}
CommandPool* CommandPool::create(bool transient, bool allow_individual_reset, bool user) {
	CommandPool* command_pool = new CommandPool(transient, allow_individual_reset);
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

	return new CommandBuffer(alloc_info);
}
void CommandPool::reset(bool release) {
	vkResetCommandPool(g_device->vk_device, vk_command_pool, release ? VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT : 0);
}
void CommandPool::free(CommandBuffer* buffer) {
	delete buffer;
	buffer = nullptr;
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
void CommandBuffer::reset(bool release) {
	vkResetCommandBuffer(vk_command_buffer, release ? VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT : 0);
}
void CommandBuffer::record(std::function<void(CommandBuffer*)> func, bool one_time) {
	this->begin(one_time);
	if(func) func(this);
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
	pass->begin = std::move(begin);
}
void CommandBuffer::cmd_end_render_pass(RenderPass* pass) {
	vkCmdEndRenderPass(this->vk_command_buffer);
	pass->begin = {};
}
void CommandBuffer::cmd_begin_rendering(RenderingBegin& begin) {
	std::vector<VkRenderingAttachmentInfo> vk_color_attachments(begin._color_attachments.size());
	for (size_t i = 0; i < vk_color_attachments.size(); ++i) {
		vk_color_attachments[i] = begin._color_attachments[i]._info;
	}
	//for (auto& attachment : begin._color_attachments) {
	//	vk_color_attachments.push_back(attachment._info);
	//}
	begin._info.colorAttachmentCount = begin._color_attachments.size();
	begin._info.pColorAttachments = vk_color_attachments.data();
	if (begin._depth_stencil_attachment) {
		begin._info.pDepthAttachment = &begin._depth_stencil_attachment->_info;
	}
	vkCmdBeginRendering(this->vk_command_buffer, &begin._info);
}
void CommandBuffer::cmd_end_rendering() {
	vkCmdEndRendering(this->vk_command_buffer);
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
void CommandBuffer::cmd_bind_raw_buffer_as_vertex(Buffer* buf, uint32_t binding, std::vector<VkDeviceSize> offset) {
	vkCmdBindVertexBuffers(this->vk_command_buffer, binding, 1, &buf->vk_buffer, offset.data());
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
void CommandBuffer::cmd_set_scissor(float width, float height, float x, float y) {
	VkRect2D scissor{ {x, y}, {width, height} };
	vkCmdSetScissor(this->vk_command_buffer, 0, 1, &scissor);
}

void CommandBuffer::cmd_set_depth_compare_op(VkCompareOp op) {
	vkCmdSetDepthCompareOp(this->vk_command_buffer, op);
}

void CommandBuffer::cmd_push_constant(PipelineLayout* layout, const std::string& name, const void* data) {
	auto iter = layout->push_consts.find(name);
	if (iter == layout->push_consts.end()) {
		return;
	}
	VkPushConstantRange& range = iter->second;

	for (auto& member : layout->push_consts) {
		vkCmdPushConstants(this->vk_command_buffer,
			layout->vk_pipeline_layout,
			range.stageFlags,
			range.offset,
			range.size,
			data);
	}
}
void CommandBuffer::cmd_push_constant(GraphicsPipeline* pipeline, const std::string& name, const void* data) {
	cmd_push_constant(pipeline->pipeline_layout, name, data);
}
void CommandBuffer::cmd_push_constant(ComputePipeline* pipeline, const std::string& name, const void* data) {
	cmd_push_constant(pipeline->pipeline_layout, name, data);
}
void CommandBuffer::cmd_bind_descriptor_set(GraphicsPipeline* pipeline, DescriptorSet* set, uint32_t target_set, std::vector<uint32_t> dynamic_offsets) {
	vkCmdBindDescriptorSets(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->pipeline_layout->vk_pipeline_layout, target_set, 1, &(set->vk_desc_set),
		dynamic_offsets.size(), dynamic_offsets.data());
}
void CommandBuffer::cmd_bind_graphics_descriptor_set(PipelineLayout* layout, DescriptorSet* set, uint32_t target_set, std::vector<uint32_t> dynamic_offsets) {
	vkCmdBindDescriptorSets(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		layout->vk_pipeline_layout, target_set, 1, &(set->vk_desc_set),
		dynamic_offsets.size(), dynamic_offsets.data());
}
void CommandBuffer::cmd_bind_descriptor_set(ComputePipeline* pipeline, DescriptorSet* set, uint32_t target_set, std::vector<uint32_t> dynamic_offsets) {
	vkCmdBindDescriptorSets(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline->pipeline_layout->vk_pipeline_layout, target_set, 1, &(set->vk_desc_set),
		dynamic_offsets.size(), dynamic_offsets.data());
}
void CommandBuffer::cmd_bind_compute_descriptor_set(PipelineLayout* layout, DescriptorSet* set, uint32_t target_set, std::vector<uint32_t> dynamic_offsets) {
	vkCmdBindDescriptorSets(this->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		layout->vk_pipeline_layout, target_set, 1, &(set->vk_desc_set),
		dynamic_offsets.size(), dynamic_offsets.data());
}
void CommandBuffer::cmd_draw(uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance) {
	vkCmdDraw(this->vk_command_buffer, vertex_count, instance_count, first_vertex, first_instance);
}
void CommandBuffer::cmd_draw_indexed(uint32_t index_count,
	uint32_t first_index,
	int32_t vertex_offset,
	uint32_t instance_count,
	uint32_t first_instance) {
	vkCmdDrawIndexed(this->vk_command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}
void CommandBuffer::cmd_draw_indexed_indirect_count(
	Buffer* commands,
	VkDeviceSize command_offset,
	Buffer* counts,
	VkDeviceSize count_offset,
	uint32_t max_draw,
	uint32_t commands_stride) {
	vkCmdDrawIndexedIndirectCount(vk_command_buffer,
		commands->vk_buffer,
		command_offset,
		counts->vk_buffer,
		count_offset,
		max_draw,
		commands_stride);
}
void CommandBuffer::cmd_draw_indexed_indirect(Buffer* commands,
	VkDeviceSize offset,
	uint32_t draw_count,
	uint32_t stride) {
	vkCmdDrawIndexedIndirect(vk_command_buffer,
		commands->vk_buffer,
		offset,
		draw_count,
		stride);
}
void CommandBuffer::cmd_dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
	vkCmdDispatch(vk_command_buffer,
		group_count_x,
		group_count_y,
		group_count_z);
}
void CommandBuffer::cmd_copy_buffer(Buffer* src, Buffer* dst) {
	assert(dst->builder._info.size == src->builder._info.size);
	VkBufferCopy region{};
	region.srcOffset = 0;
	region.dstOffset = 0;
	region.size = dst->builder._info.size;
	vkCmdCopyBuffer(vk_command_buffer, src->vk_buffer, dst->vk_buffer, 1, &region);
}
void CommandBuffer::cmd_image_memory_barrier(Image* image, ResourceState from_state, ResourceState to_state, VkImageSubresourceRange sub_range) {
	transition_image_state(this, image, from_state, to_state, sub_range);
}
void CommandBuffer::cmd_buffer_memory_barrier(Buffer* buffer, ResourceState from_state, ResourceState to_state) {
	transition_buffer_state(this, buffer, from_state, to_state);
}
void CommandBuffer::cmd_image_blit(Image* src, Image* dst, ImageBlit& region, VkFilter filter) {
	vkCmdBlitImage(vk_command_buffer,
		src->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region._image_blit, filter);
}
void CommandBuffer::cmd_image_copy(Image* src, Image* dst, ImageCopy& region) {
	vkCmdCopyImage(vk_command_buffer,
		src->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region._image_copy);
}

void CommandBuffer::cmds_image_full_copy(Image* src, Image* dst, VkImageAspectFlags aspect) {
	assert(src->builder._image_info.arrayLayers == dst->builder._image_info.arrayLayers);
	assert(src->builder._image_info.mipLevels == dst->builder._image_info.mipLevels);
	assert(src->builder._image_info.extent.width == dst->builder._image_info.extent.width);
	assert(src->builder._image_info.extent.height == dst->builder._image_info.extent.height);
	assert(src->builder._image_info.extent.depth == 1);
	assert(dst->builder._image_info.extent.depth == 1);
	uint32_t n_layers = src->builder._image_info.arrayLayers;
	uint32_t n_mips = src->builder._image_info.mipLevels;
	uint32_t base_width = src->builder._image_info.extent.width;
	uint32_t base_height = src->builder._image_info.extent.height;
	
	std::vector<VkImageCopy> vk_regions(n_mips);
	for (uint32_t m = 0; m < n_mips; ++m) {
		ImageCopy region;
		region
			.src_layer(0, n_layers)
			.dst_layer(0, n_layers)
			.extent(base_width >> m, base_height >> m)
			.src_aspect(aspect)
			.src_mip(m)
			.dst_aspect(aspect)
			.dst_mip(m);
		vk_regions[m] = region._image_copy;
	}

	vkCmdCopyImage(vk_command_buffer,
		src->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		vk_regions.size(), vk_regions.data());
}

void CommandBuffer::cmd_fill_buffer(Buffer* dst_buffer, uint32_t data, VkDeviceSize dst_offset, VkDeviceSize size) {
	vkCmdFillBuffer(vk_command_buffer, dst_buffer->vk_buffer, dst_offset,
		(size == VK_WHOLE_SIZE ? dst_buffer->builder._info.size : size), data);
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
		std::cout << "cannot submit to queue, error code = " << result << std::endl;
		exit(1);
	}
}


VkResult Queue::present(QueuePresent& info) {
	VkResult result;
	VkPresentInfoKHR present_info{};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = info._wait_semaphores.size();
	present_info.pWaitSemaphores = info._wait_semaphores.data();
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &g_swapchain->vk_swapchain;
	present_info.pImageIndices = &info._image_index;
	present_info.pResults = &result;
	vkQueuePresentKHR(vk_queue, &present_info);
	if (result != VK_SUCCESS) {
		std::cout << "present queue submission failed, error code = " << result << std::endl;
	}
	return result;
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
VertexBuffer::VertexBuffer(VertexBufferBuilder& b, BufferDataUpload upload_option) {
	assert(b._data_handles.size() == b._buffer_builders.size());

	for (size_t i = 0; i < b._data_handles.size(); ++i) {
		auto& bb = b._buffer_builders[i];
		Buffer* buffer = new Buffer(bb);
		const void* data = b._data_handles[i];
		this->buffers.push_back(buffer);
		if (!data) {
			continue;
		}
		if (upload_option == BufferDataUpload::Sync) {
			buffer->populate(data);
		}
		else if(upload_option == BufferDataUpload::AsyncCPUWait) {
			buffer->populate_async(data);
		}
		else if (upload_option == BufferDataUpload::AsyncGPUBarrier) {
			buffer->populate_async(data, Buffer::SyncType::GPUBarrier, ResourceState::VertexRead, ResourceState::Created);
		}
	}
	// b._buffer_builders.clear();
	// b._data_handles.clear();
	for (const void*& ptr : b._data_handles) {
		ptr = nullptr;
	}

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

void VertexBuffer::wait_for_async_upload() {
	for (Buffer* buf : buffers) {
		buf->wait_for_async();
	}
}

AccelerationStructure::AccelerationStructure(AccelerationStructureBuilder& builder) {
	this->builder = std::move(builder); // this should be safe as only trivial fields in the builder matter.

	// create
	if (this->builder._vk_create_info.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
		if (this->builder._vk_build_geo_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) {
			std::cout << "updatable BLAS not supported" << std::endl;
			assert(false);
			exit(1);
		}
		create_build_as_blas();
	} else if (this->builder._vk_create_info.type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) {
		if (!this->builder._tri_geos.empty()) {
			std::cout << "cannot create TLAS with triangle geometry" << std::endl;
			assert(false);
			exit(1);
		}
		create_build_as_tlas();
	} else {
		std::cout << "Has to be either BLAS or TLAS" << std::endl;
		assert(false);
		exit(1);
	}
}

void AccelerationStructure::create_build_as_blas() {
	// 1. create
	// fill out VkAccelerationStructureBuildGeometryInfoKHR's non-trivial fields
	std::vector<VkAccelerationStructureGeometryKHR*> geo_ptrs(builder._tri_geos.size());
	for (uint32_t i = 0; i < builder._tri_geos.size(); ++i) {
		geo_ptrs[i] = &builder._tri_geos[i]._vk_geo;
	}
	builder._vk_build_geo_info.geometryCount = builder._tri_geos.size();
	builder._vk_build_geo_info.pGeometries = nullptr;
	builder._vk_build_geo_info.ppGeometries = geo_ptrs.data();

	std::vector<uint32_t> prim_counts;
	for (auto& tri_geo : builder._tri_geos) {
		prim_counts.push_back(tri_geo.n_tris);
	}

	// this is actually a valid use case
	//if (prim_counts.empty()) {
	//	std::cout << "No primitives to build" << std::endl;
	//	assert(false);
	//	exit(1);
	//}

	// calculate various buffer sizes for this acceleration structure
	VkAccelerationStructureBuildSizesInfoKHR build_size_info = {};
	build_size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	build_size_info.pNext = nullptr;
	build_size_info.accelerationStructureSize = 0;
	build_size_info.updateScratchSize = 0;
	build_size_info.buildScratchSize = 0;

	g_device->fn<PFN_vkGetAccelerationStructureBuildSizesKHR>("vkGetAccelerationStructureBuildSizesKHR") (
		g_device->vk_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&builder._vk_build_geo_info, prim_counts.data(),
		&build_size_info);

	// fill out VkAccelerationStructureCreateInfoKHR's missing size info
	builder._vk_create_info.size = build_size_info.accelerationStructureSize;

	backing_buf = new Buffer(BufferBuilder()
		.size(build_size_info.accelerationStructureSize)
		.usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.host_access(BufferBuilder::Access::Invisible));
	builder._vk_create_info.buffer = backing_buf->vk_buffer;

	VkResult result = g_device->fn<PFN_vkCreateAccelerationStructureKHR>("vkCreateAccelerationStructureKHR")(g_device->vk_device, &builder._vk_create_info, VK_NULL_HANDLE, &vk_as);
	if (result != VK_SUCCESS) {
		assert(false);
		std::cout << "cannot create BLAS, error code = " << result << std::endl;
		exit(1);
	}


	// 2. build
	builder._vk_build_geo_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	builder._vk_build_geo_info.srcAccelerationStructure = VK_NULL_HANDLE;
	builder._vk_build_geo_info.dstAccelerationStructure = vk_as;
	VkDeviceSize scratch_size = 
		builder._vk_build_geo_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR ? 
		std::max(build_size_info.buildScratchSize, build_size_info.updateScratchSize) :
		build_size_info.buildScratchSize;

	scratch_buf = new Buffer(BufferBuilder()
		.size(scratch_size)
		.usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.host_access(BufferBuilder::Access::Invisible));
	builder._vk_build_geo_info.scratchData.deviceAddress = scratch_buf->device_address();

	std::vector<Buffer*> temp_bufs;
	for (AccelerationStructureBuilder::TrianglesGeometry& tri_geo : builder._tri_geos) {
		uint32_t vertex_count = tri_geo._vk_geo.geometry.triangles.maxVertex + 1;
		VkDeviceSize vertex_stride = tri_geo._vk_geo.geometry.triangles.vertexStride;
		Buffer* vb = BufferBuilder()
			.size(vertex_count * vertex_stride)
			.usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
			.host_access(BufferBuilder::Access::Coherent)
			.build();
		assert(tri_geo._vertex_data);
		vb->populate(tri_geo._vertex_data);
		tri_geo._vk_geo.geometry.triangles.vertexData.deviceAddress = vb->device_address();
		temp_bufs.push_back(vb);

		uint32_t index_count = tri_geo.n_tris * 3;
		uint32_t index_size = 0 << (tri_geo._vk_geo.geometry.triangles.indexType + 1);
		switch (tri_geo._vk_geo.geometry.triangles.indexType) {
		case VK_INDEX_TYPE_UINT16:
			index_size = 2;
			break;
		case VK_INDEX_TYPE_UINT32:
			index_size = 4;
			break;
		default:
			assert(false);
		}

		Buffer* ib = BufferBuilder()
			.size(index_count * index_size)
			.usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
			.host_access(BufferBuilder::Access::Coherent)
			.build();
		assert(tri_geo._index_data);
		ib->populate(tri_geo._index_data);
		tri_geo._vk_geo.geometry.triangles.indexData.deviceAddress = ib->device_address();
		temp_bufs.push_back(ib);
	}
	
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> build_ranges(builder._tri_geos.size());
	for (uint32_t i = 0; i < builder._tri_geos.size(); ++i) {
		build_ranges[i] = { builder._tri_geos[i].n_tris, 0, 0, 0 };
	}
	const VkAccelerationStructureBuildRangeInfoKHR* build_ranges_p = build_ranges.data();

	// build commands
	CommandBuffer* cmd = begin_single_time_command_buffer();
	g_device->fn<PFN_vkCmdBuildAccelerationStructuresKHR>("vkCmdBuildAccelerationStructuresKHR")(
		cmd->vk_command_buffer, 1, &builder._vk_build_geo_info, &build_ranges_p);
	end_single_time_command_buffer(cmd);

	for (Buffer*& buf : temp_bufs) {
		buf->destroy();
	}
}

void AccelerationStructure::create_build_as_tlas() {
	// 1. create
	// fill out VkAccelerationStructureBuildGeometryInfoKHR's non-trivial fields
	builder._vk_build_geo_info.geometryCount = 1;
	builder._vk_build_geo_info.pGeometries = &builder._instance_geo._vk_geo;
	builder._vk_build_geo_info.ppGeometries = nullptr;

	// calculate various buffer sizes for this acceleration structure
	VkAccelerationStructureBuildSizesInfoKHR build_size_info = {};
	build_size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	build_size_info.pNext = nullptr;
	build_size_info.accelerationStructureSize = 0;
	build_size_info.updateScratchSize = 0;
	build_size_info.buildScratchSize = 0;

	uint32_t prim_count = builder._instance_geo._instances.size();
	assert(prim_count > 0);
	g_device->fn<PFN_vkGetAccelerationStructureBuildSizesKHR>("vkGetAccelerationStructureBuildSizesKHR") (
		g_device->vk_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&builder._vk_build_geo_info, &prim_count,
		&build_size_info);

	// fill out VkAccelerationStructureCreateInfoKHR's missing size info
	builder._vk_create_info.size = build_size_info.accelerationStructureSize;

	backing_buf = new Buffer(BufferBuilder()
		.size(build_size_info.accelerationStructureSize)
		.usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.host_access(BufferBuilder::Access::Invisible));
	builder._vk_create_info.buffer = backing_buf->vk_buffer;

	VkResult result = g_device->fn<PFN_vkCreateAccelerationStructureKHR>("vkCreateAccelerationStructureKHR")(g_device->vk_device, &builder._vk_create_info, VK_NULL_HANDLE, &vk_as);
	if (result != VK_SUCCESS) {
		assert(false);
		std::cout << "cannot create TLAS, error code = " << result << std::endl;
		exit(1);
	}


	// 2. build
	builder._vk_build_geo_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	builder._vk_build_geo_info.srcAccelerationStructure = VK_NULL_HANDLE;
	builder._vk_build_geo_info.dstAccelerationStructure = vk_as;
	VkDeviceSize scratch_size =
		builder._vk_build_geo_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR ?
		std::max(build_size_info.buildScratchSize, build_size_info.updateScratchSize) :
		build_size_info.buildScratchSize;

	scratch_buf = new Buffer(BufferBuilder()
		.size(scratch_size)
		.usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.host_access(BufferBuilder::Access::Invisible));
	builder._vk_build_geo_info.scratchData.deviceAddress = scratch_buf->device_address();

	std::vector<VkAccelerationStructureInstanceKHR> vk_instances(builder._instance_geo._instances.size());
	for (uint32_t i = 0; i < builder._instance_geo._instances.size(); ++i) {
		vk_instances[i] = builder._instance_geo._instances[i]._vk_instance;
	}

	Buffer* ib = BufferBuilder() // instance buffer, not index buffer
		.size(vk_instances.size() * sizeof(VkAccelerationStructureInstanceKHR))
		.usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.host_access(BufferBuilder::Access::Coherent)
		.build();
	ib->populate(vk_instances.data());
	builder._instance_geo._vk_geo.geometry.instances.data.deviceAddress = ib->device_address();

	VkAccelerationStructureBuildRangeInfoKHR build_range = { builder._instance_geo._instances.size(), 0, 0, 0 };
	const VkAccelerationStructureBuildRangeInfoKHR* build_range_p = &build_range;

	// build commands
	CommandBuffer* cmd = begin_single_time_command_buffer();
	g_device->fn<PFN_vkCmdBuildAccelerationStructuresKHR>("vkCmdBuildAccelerationStructuresKHR")(
		cmd->vk_command_buffer, 1, &builder._vk_build_geo_info, &build_range_p);
	end_single_time_command_buffer(cmd);

	ib->destroy();
}

AccelerationStructure::~AccelerationStructure() {
	delete backing_buf;
	delete scratch_buf;
	g_device->fn<PFN_vkDestroyAccelerationStructureKHR>("vkDestroyAccelerationStructureKHR")(g_device->vk_device, vk_as, nullptr);
}

void AccelerationStructure::destroy() {
	std::shared_ptr<AccelerationStructure> ptr(std::shared_ptr<AccelerationStructure>(), this);
	g_user_acc_structs.erase(ptr);
}

VkDeviceAddress AccelerationStructure::device_address() {
	VkAccelerationStructureDeviceAddressInfoKHR address_info;
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.pNext = nullptr;
	address_info.accelerationStructure = this->vk_as;
	return g_device->fn<PFN_vkGetAccelerationStructureDeviceAddressKHR>("vkGetAccelerationStructureDeviceAddressKHR")(g_device->vk_device, &address_info);
}

// image
Image::Image(ImageBuilder& builder) {
	if (builder._has_mips) {
		uint32_t levels = (uint32_t)(floor(log2(std::max(builder._image_info.extent.width, builder._image_info.extent.height))) + 1);
		builder._image_info.mipLevels = levels;
		builder._view_info.subresourceRange.levelCount = levels;
	}


	// Check is physical device support this particular type of image memory requested by user
	VkImageFormatProperties props{};
	VkResult result = vkGetPhysicalDeviceImageFormatProperties(g_physical_device.vk_physical_device,
		builder._image_info.format, builder._image_info.imageType,
		VK_IMAGE_TILING_OPTIMAL, builder._image_info.usage, 0, &props);
	if (result != VK_SUCCESS) {
		std::cout << "Physical device is not capable of creating such image" << std::endl;
		exit(1);
	}
	//VkExtent3D            maxExtent;
	//uint32_t              maxMipLevels;
	//uint32_t              maxArrayLayers;
	//VkSampleCountFlags    sampleCounts;
	//VkDeviceSize          maxResourceSize;
	if (props.maxExtent.width < builder._image_info.extent.width ||
		props.maxExtent.height < builder._image_info.extent.height ||
		props.maxExtent.depth < builder._image_info.extent.depth) {
		std::cout << "Image extent exceeds maximum value allowed for its type" << std::endl;
		exit(1);
	}
	if (props.maxMipLevels < builder._image_info.mipLevels) {
		std::cout << "Image mip levels exceed maximum value allowed for its type" << std::endl;
		exit(1);
	}
	if (props.maxArrayLayers < builder._image_info.arrayLayers) {
		std::cout << "Image layers exceed maximum allowed value for its type" << std::endl;
		exit(1);
	}
	if (props.sampleCounts && builder._image_info.samples != builder._image_info.samples) {
		std::cout << "Physical device cannot provide all sample counts requested" << std::endl;
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
	async_ctx = nullptr;
}
Image::~Image() {
	wait_for_async();
	vkDestroyImage(g_device->vk_device, vk_image, nullptr);
	vkFreeMemory(g_device->vk_device, vk_memory, nullptr);
	vkDestroyImageView(g_device->vk_device, vk_view, nullptr);
	for (auto& p : vk_subresource_views) {
		vkDestroyImageView(g_device->vk_device, p.second, nullptr);
	}
}
void Image::destroy() {
	std::shared_ptr<Image> ptr(std::shared_ptr<Image>(), this);
	g_user_images.erase(ptr);
}
void Image::populate_async(const void* data, size_t byte_size, 
	ResourceState target_state, ResourceState current_state, SyncType sync_type) {
	assert(builder._image_info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	if (byte_size == 0 || !data) {
		return;
	}

	otcv::Buffer* staging = nullptr;
	{
		otcv::BufferBuilder bb;
		staging = bb
			.size(byte_size)
			.usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
			.host_access(otcv::BufferBuilder::Access::Coherent)
			.build();
	}
	memcpy(staging->mapped, data, byte_size);
	assert(builder._image_info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	// unfinished async task, wait till the last task finishes
	wait_for_async();

	otcv::CommandBuffer* cmd_buf = g_command_pool->allocate();
	cmd_buf->begin(true);

	cmd_buf->cmd_image_memory_barrier(this, current_state, otcv::ResourceState::TransferDst);

	VkImageAspectFlags image_aspect = builder._view_info.subresourceRange.aspectMask;

	VkBufferImageCopy copy_region{};
	copy_region.bufferOffset = 0;
	copy_region.bufferRowLength = 0;
	copy_region.bufferImageHeight = 0;
	copy_region.imageSubresource.aspectMask = image_aspect;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageOffset = { 0, 0, 0 };
	copy_region.imageExtent = builder._image_info.extent;
	vkCmdCopyBufferToImage(cmd_buf->vk_command_buffer, staging->vk_buffer, vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

	// generate mipmaps when necessary
	if (builder._image_info.mipLevels > 1) {
		assert(builder._image_info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		assert(builder._view_info.subresourceRange.levelCount == builder._image_info.mipLevels);
		uint32_t base_width = builder._image_info.extent.width;
		uint32_t base_height = builder._image_info.extent.height;
		for (uint32_t level = 0; level < builder._image_info.mipLevels - 1; ++level) {
			// get source level ready
			cmd_buf->cmd_image_memory_barrier(this, ResourceState::TransferDst, ResourceState::TransferSrc, { image_aspect, level, 1, 0, 1 });
			// get dest level ready
			cmd_buf->cmd_image_memory_barrier(this, ResourceState::Created, ResourceState::TransferDst, { image_aspect, level + 1, 1, 0, 1 });
			// blit
			ImageBlit region;
			region
				.src_upper_bound(base_width >> level, base_height >> level)
				.src_mip(level)
				.src_aspect(image_aspect)
				.dst_upper_bound(base_width >> (level + 1), base_height >> (level + 1))
				.dst_mip(level + 1)
				.dst_aspect(image_aspect);
			cmd_buf->cmd_image_blit(this, this, region, VK_FILTER_LINEAR);
			// convert source level to target state
			cmd_buf->cmd_image_memory_barrier(this, ResourceState::TransferSrc, target_state, { image_aspect, level, 1, 0, 1 });
		}
	}
	// convert highest level to target state
	cmd_buf->cmd_image_memory_barrier(this, otcv::ResourceState::TransferDst, target_state, { image_aspect, builder._image_info.mipLevels - 1, 1, 0, 1 });

	cmd_buf->end();

	if (sync_type == SyncType::CPUWait) {
		otcv::Fence* fence = otcv::Fence::create(false);
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end().signal(fence);
		g_queue.submit(submit);

		async_ctx.reset(new AsyncPopulateCtx);
		async_ctx->command_buffer = cmd_buf;
		async_ctx->fence = fence;
		async_ctx->staging = staging;
	}
	else if (sync_type == SyncType::GPUBarrier) {
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end();
		g_queue.submit(submit);
	}
	else {
		assert(false);
	}
}

void Image::initialize_state_async(ResourceState target_state, ResourceState current_state, SyncType sync_type) {
	// unfinished async task, wait till the last task finishes
	wait_for_async();
	
	otcv::CommandBuffer* cmd_buf = g_command_pool->allocate();
	cmd_buf->begin(true);
	cmd_buf->cmd_image_memory_barrier(this, current_state, target_state);
	cmd_buf->end();

	if (sync_type == SyncType::CPUWait) {
		otcv::Fence* fence = otcv::Fence::create(false);
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end().signal(fence);
		g_queue.submit(submit);

		async_ctx.reset(new AsyncPopulateCtx);
		async_ctx->command_buffer = cmd_buf;
		async_ctx->fence = fence;
		async_ctx->staging = nullptr;
	}
	else if (sync_type == SyncType::GPUBarrier) {
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end();
		g_queue.submit(submit);
	}
	else {
		assert(false);
	}
}

void Image::wait_for_async() {
	if (!async_ctx) {
		// nothing to wait
		return;
	}
	if (async_ctx->fence) {
		async_ctx->fence->wait_reset();
		async_ctx->fence->destroy();
	}
	if (async_ctx->command_buffer) {
		g_command_pool->free(async_ctx->command_buffer);
	}
	if (async_ctx->staging) {
		async_ctx->staging->destroy();
	}

	async_ctx = nullptr;
}
void Image::populate(void* data, size_t byte_size,
	ResourceState target_state, ResourceState current_state) {
	populate_async(data, byte_size, target_state, current_state, SyncType::CPUWait);
	wait_for_async();
}

void Image::initialize_state(ResourceState target_state, ResourceState current_state) {
	initialize_state_async(target_state, current_state, SyncType::CPUWait);
	wait_for_async();
}

VkImageView Image::view_of_subresource(const VkImageSubresourceRange& range) {
	assert(builder._image_info.mipLevels > range.baseMipLevel);
	assert(builder._image_info.mipLevels >= range.baseMipLevel + range.levelCount);
	assert(builder._image_info.arrayLayers > range.baseArrayLayer);
	assert(builder._image_info.arrayLayers >= range.baseArrayLayer + range.layerCount);
	uint32_t view_id = pack(uint8_t(range.baseMipLevel), uint8_t(range.levelCount), uint8_t(range.baseArrayLayer), uint8_t(range.layerCount));
	auto iter = vk_subresource_views.find(view_id);
	if (iter != vk_subresource_views.end()) {
		return iter->second;
	}

	VkImageViewCreateInfo layer_view_info = builder._view_info;
	layer_view_info.subresourceRange = range;
	VkImageView vk_layers_view;
	VkResult result = vkCreateImageView(g_device->vk_device, &layer_view_info, nullptr, &vk_layers_view);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create layer view for [mip, count] = [" << range.baseMipLevel  << ", " << range.levelCount << " ]"
			<< ", [layer, count] = [ " << range.baseArrayLayer << ", " << range.layerCount << " ], error code = " << result << std::endl;
		exit(1);
	}
	vk_subresource_views[view_id] = vk_layers_view;
	return vk_layers_view;
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
	alloc_info.pNext = nullptr;
	alloc_info.allocationSize = mem_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, builder._mem_props);

	// for buffers that need device address
	VkMemoryAllocateFlagsInfo flags_info = {};
	flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flags_info.pNext = nullptr;
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	flags_info.deviceMask = 0;
	if (builder._info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		alloc_info.pNext = &flags_info;
	}

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
	async_ctx = nullptr;
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

void Buffer::populate_async(const void* data, SyncType sync_type, ResourceState target_state, ResourceState current_state) {
	// if fails, call the sync version
	assert(!(builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
	// should not set any resource state if CPU wait type is set
	if (sync_type == SyncType::CPUWait) {
		assert(target_state == ResourceState::Null);
		assert(current_state == ResourceState::Null);
	}
	else if (sync_type == SyncType::GPUBarrier) {
		assert(target_state != ResourceState::Null);
		assert(current_state != ResourceState::Null);
	}
	else {
		assert(false);
	}
	
	if (!data) {
		return;
	}
	VkDeviceSize size = builder._info.size;
	otcv::BufferBuilder b_builder;
	otcv::Buffer* staging = b_builder.size(size).usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).host_access(otcv::BufferBuilder::Access::Coherent).build();
	memcpy(staging->mapped, data, size);

	// unfinished async task, wait till the last task finishes
	wait_for_async();

	otcv::CommandBuffer* cmd_buf = g_command_pool->allocate();
	cmd_buf->begin(true);
	if (sync_type == SyncType::GPUBarrier) {
		cmd_buf->cmd_buffer_memory_barrier(this, current_state, ResourceState::TransferDst);
		cmd_buf->cmd_copy_buffer(staging, this);
		cmd_buf->cmd_buffer_memory_barrier(this, ResourceState::TransferDst, target_state);
	}
	else {
		cmd_buf->cmd_copy_buffer(staging, this);
	}
	cmd_buf->end();

	if (sync_type == SyncType::CPUWait) {
		otcv::Fence* fence = otcv::Fence::create(false);
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end().signal(fence);
		g_queue.submit(submit);

		async_ctx.reset(new AsyncPopulateCtx);
		async_ctx->fence = fence;
		async_ctx->command_buffer = cmd_buf;
		async_ctx->staging = staging;
	}
	else if (sync_type == SyncType::GPUBarrier) {
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buf)
			.end();
		g_queue.submit(submit);
	}
	else {
		assert(false);
	}
}

void Buffer::wait_for_async() {
	if (!async_ctx) {
		// nothing to wait
		return;
	}
	if (async_ctx->fence) {
		async_ctx->fence->wait_reset();
		async_ctx->fence->destroy();
	}
	if (async_ctx->command_buffer) {
		g_command_pool->free(async_ctx->command_buffer);
	}
	if (async_ctx->staging) {
		async_ctx->staging->destroy();
	}

	async_ctx = nullptr;
}

void Buffer::populate(const void* data) {
	if (!data) {
		return;
	}
	if (builder._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		copy_host_mapped(data, 0, builder._info.size)->flush();
	}
	else {
		// staging_queued_copy(data, this);
		populate_async(data);
		wait_for_async();
	}
}
Buffer* Buffer::copy_host_mapped(const void* data, uint32_t offset, uint32_t size) {
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

VkDeviceAddress Buffer::device_address() {
	assert(builder._info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
	VkBufferDeviceAddressInfo addr = {};
	addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addr.pNext = nullptr;
	addr.buffer = vk_buffer;
	return vkGetBufferDeviceAddress(g_device->vk_device, &addr);
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

PipelineLayout::PipelineLayout(const std::vector<DescriptorSetLayout*>& set_layouts, const std::map<std::string, VkPushConstantRange>& push_const_members) {
	std::vector<VkDescriptorSetLayout> vk_desc_set_layouts;
	for (auto& layout : set_layouts) {
		vk_desc_set_layouts.push_back(layout->vk_desc_set_layout);
	}

	// Vulkan does not accept same stage flags in different push constant ranges
	// Merge stages
	//std::map<VkShaderStageFlags, VkPushConstantRange> stage_range_map;
	//for (auto& ele : push_const_members) {
	//	const VkPushConstantRange& range = ele.second;
	//	stage_range_map[range.stageFlags] = range;
	//}

	std::vector<VkPushConstantRange> ranges;
	for (auto& member : push_const_members) {
		// ranges.push_back(member.second);
		const VkPushConstantRange& range = member.second;
		auto iter = std::find_if(ranges.begin(), ranges.end(),
			[&](VkPushConstantRange& r) {
			return r.stageFlags == range.stageFlags;
		});

		if (iter != ranges.end()) {
			// found range with the same stage flag, merge
			uint32_t end = std::max(iter->offset + iter->size, range.offset + range.size);
			iter->offset = std::min(range.offset, iter->offset);
			iter->size = end - iter->offset;
		}
		else {
			ranges.insert(iter, range);
		}
	}
	
	VkPipelineLayoutCreateInfo pipeline_layout_create{};
	pipeline_layout_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create.setLayoutCount = (uint32_t)vk_desc_set_layouts.size();
	pipeline_layout_create.pSetLayouts = vk_desc_set_layouts.data();
	pipeline_layout_create.pushConstantRangeCount = ranges.size();
	pipeline_layout_create.pPushConstantRanges = ranges.data();
	VkResult result = vkCreatePipelineLayout(g_device->vk_device, &pipeline_layout_create, nullptr, &this->vk_pipeline_layout);
	if (result != VK_SUCCESS) {
		std::cout << "cannot create pipeline layout, error code = " << result << std::endl;
		exit(1);
	}

	this->create_info = pipeline_layout_create;
	this->push_consts = push_const_members;
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
	int color_attachment_count;
	if (builder._pipeline_rendering) {
		color_attachment_count = builder._pipeline_rendering->_color_attachment_formats.size();
	}
	else {
		color_attachment_count = builder._render_pass->builder._subpasses[builder._subpass]._refs_color.size();
	}

	std::vector<VkPipelineColorBlendAttachmentState> blend_states(color_attachment_count, no_blend_state);
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
			VkDescriptorSetLayoutBinding vk_layout_binding = {};
			vk_layout_binding.binding = binding;
			vk_layout_binding.descriptorType = p.second._type;
			vk_layout_binding.descriptorCount = p.second._array_count;
			vk_layout_binding.stageFlags |= stage;
			vk_layout_binding.pImmutableSamplers = nullptr;
			// layout_binding_set[set][binding] = vk_layout_binding;
			layout_binding_set[set].push_back(vk_layout_binding);
		}
		// sort bindings
		for (auto& bindings : layout_binding_set) {
			std::sort(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b0, VkDescriptorSetLayoutBinding& b1) {
				return b0.binding < b1.binding;
			});
		}
	};

	std::map<std::string, VkPushConstantRange> push_constant_ranges;
	auto collect_push_constants = [&](ShaderModuleBuilder& shader_builder, VkShaderStageFlags stage) {
		for (auto& pc_member : shader_builder._push_constants) {
			uint16_t pc_offset;
			uint16_t pc_size;
			otcv::unpack(pc_member.second, pc_offset, pc_size);
			if (pc_size == 0) {
				return;
			}
			VkPushConstantRange range{};
			range.offset = pc_offset;
			range.size = pc_size;
			range.stageFlags = stage;
			push_constant_ranges[pc_member.first] = range;
		}
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
		desc_set_layouts.push_back(new DescriptorSetLayout(set_bindings));
	}
	pipeline_layout = new PipelineLayout(desc_set_layouts, push_constant_ranges);

	// dynamic rendering
	if (builder._pipeline_rendering) {
		builder._pipeline_rendering->_pipeline_rendering.colorAttachmentCount = color_attachment_count;
		builder._pipeline_rendering->_pipeline_rendering.pColorAttachmentFormats = builder._pipeline_rendering->_color_attachment_formats.data();
	}

	VkGraphicsPipelineCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = builder._pipeline_rendering ? &builder._pipeline_rendering->_pipeline_rendering : nullptr;
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
	create_info.renderPass = builder._pipeline_rendering ? VK_NULL_HANDLE : builder._render_pass->vk_render_pass;
	create_info.subpass = builder._pipeline_rendering ? 0 : builder._subpass;
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
	for (DescriptorSetLayout* set_layout : desc_set_layouts) {
		delete set_layout;
	}
}
void GraphicsPipeline::destroy() {
	std::shared_ptr<GraphicsPipeline> ptr(std::shared_ptr<GraphicsPipeline>(), this);
	g_user_graphics_pipelines.erase(ptr);
}

void GraphicsPipeline::cmd_bind(CommandBuffer* cmd_buffer) {
	vkCmdBindPipeline(cmd_buffer->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);
}

ComputePipeline::ComputePipeline(ShaderModule* compute_shader) {
	// std::map<uint16_t, std::vector<VkDescriptorSetLayoutBinding>> binding_set_map;

	std::vector<std::vector<VkDescriptorSetLayoutBinding>> layout_binding_set;
	auto collect_uniforms = [&]() {
		for (auto& p : compute_shader->builder._uniforms) {
			uint16_t set;
			uint16_t binding;
			otcv::unpack(p.first, set, binding);
			 if (set >= layout_binding_set.size()) {
				 layout_binding_set.resize(set + 1, {});
			 }
			VkDescriptorSetLayoutBinding vk_layout_binding = {};
			vk_layout_binding.binding = binding;
			vk_layout_binding.descriptorType = p.second._type;
			vk_layout_binding.descriptorCount = p.second._array_count;
			vk_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			layout_binding_set[set].push_back(vk_layout_binding);
		}
		// sort bindings
		for (auto& bindings : layout_binding_set) {
			std::sort(bindings.begin(), bindings.end(), [&](VkDescriptorSetLayoutBinding& b0, VkDescriptorSetLayoutBinding& b1){
				return b0.binding < b1.binding;
			});
		}
	};

	std::map<std::string, VkPushConstantRange> push_constant_ranges;
	auto collect_push_constants = [&]() {
		for (auto& pc_member : compute_shader->builder._push_constants) {
			uint16_t pc_offset;
			uint16_t pc_size;
			otcv::unpack(pc_member.second, pc_offset, pc_size);
			if (pc_size == 0) {
				return;
			}
			VkPushConstantRange range{};
			range.offset = pc_offset;
			range.size = pc_size;
			range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			push_constant_ranges[pc_member.first] = range;
		}
	};

	collect_uniforms();
	collect_push_constants();

	for (auto& set_bindings : layout_binding_set) {
		desc_set_layouts.emplace_back(new DescriptorSetLayout(set_bindings));
	}

	pipeline_layout = new PipelineLayout(desc_set_layouts, push_constant_ranges);

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
	for (DescriptorSetLayout* set_layout : desc_set_layouts) {
		delete set_layout;
	}
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
ImageBlit::ImageBlit() {

	_image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	_image_blit.srcSubresource.mipLevel = 0;
	_image_blit.srcSubresource.baseArrayLayer = 0;
	_image_blit.srcSubresource.layerCount = 1;

	_image_blit.srcOffsets[0].x = 0;
	_image_blit.srcOffsets[0].y = 0;
	_image_blit.srcOffsets[0].z = 0;

	_image_blit.srcOffsets[1].x = 0;
	_image_blit.srcOffsets[1].y = 0;
	_image_blit.srcOffsets[1].z = 0;

	_image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	_image_blit.dstSubresource.mipLevel = 0;
	_image_blit.dstSubresource.baseArrayLayer = 0;
	_image_blit.dstSubresource.layerCount = 1;

	_image_blit.dstOffsets[0].x = 0;
	_image_blit.dstOffsets[0].y = 0;
	_image_blit.dstOffsets[0].z = 0;

	_image_blit.dstOffsets[1].x = 0;
	_image_blit.dstOffsets[1].y = 0;
	_image_blit.dstOffsets[1].z = 0;
}
ImageBlit& ImageBlit::src_upper_bound(int32_t x, int32_t y, int32_t z) {
	_image_blit.srcOffsets[1].x = x;
	_image_blit.srcOffsets[1].y = y;
	_image_blit.srcOffsets[1].z = z;
	return *this;
}
ImageBlit& ImageBlit::src_lower_bound(int32_t x, int32_t y, int32_t z) {
	_image_blit.srcOffsets[0].x = x;
	_image_blit.srcOffsets[0].y = y;
	_image_blit.srcOffsets[0].z = z;
	return *this;
}
ImageBlit& ImageBlit::src_aspect(VkImageAspectFlags aspect) {
	_image_blit.srcSubresource.aspectMask = aspect;
	return *this;
}
ImageBlit& ImageBlit::src_mip(uint32_t mip) {
	_image_blit.srcSubresource.mipLevel = mip;
	return *this;
}
ImageBlit& ImageBlit::src_layer(uint32_t layer) {
	_image_blit.srcSubresource.baseArrayLayer = layer;
	return *this;
}
ImageBlit& ImageBlit::dst_upper_bound(int32_t x, int32_t y, int32_t z) {
	_image_blit.dstOffsets[1].x = x;
	_image_blit.dstOffsets[1].y = y;
	_image_blit.dstOffsets[1].z = z;
	return *this;
}
ImageBlit& ImageBlit::dst_lower_bound(int32_t x, int32_t y, int32_t z) {
	_image_blit.dstOffsets[0].x = x;
	_image_blit.dstOffsets[0].y = y;
	_image_blit.dstOffsets[0].z = z;
	return *this;
}
ImageBlit& ImageBlit::dst_aspect(VkImageAspectFlags aspect) {
	_image_blit.dstSubresource.aspectMask = aspect;
	return *this;
}
ImageBlit& ImageBlit::dst_mip(uint32_t mip) {
	_image_blit.dstSubresource.mipLevel = mip;
	return *this;
}
ImageBlit& ImageBlit::dst_layer(uint32_t layer) {
	_image_blit.dstSubresource.baseArrayLayer = layer;
	return *this;
}
ImageCopy::ImageCopy() {
	_image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	_image_copy.srcSubresource.mipLevel = 0;
	_image_copy.srcSubresource.baseArrayLayer = 0;
	_image_copy.srcSubresource.layerCount = 1;

	_image_copy.srcOffset = { 0, 0, 0 };

	_image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	_image_copy.dstSubresource.mipLevel = 0;
	_image_copy.dstSubresource.baseArrayLayer = 0;
	_image_copy.dstSubresource.layerCount = 1;

	_image_copy.dstOffset = { 0, 0, 0 };

	_image_copy.extent = { 0, 0, 0 };
}

ImageCopy& ImageCopy::src_layer(uint32_t base_layer, uint32_t layer_count) {
	_image_copy.srcSubresource.baseArrayLayer = base_layer;
	_image_copy.srcSubresource.layerCount = layer_count;
	return *this;
}
ImageCopy& ImageCopy::dst_layer(uint32_t base_layer, uint32_t layer_count) {
	_image_copy.dstSubresource.baseArrayLayer = base_layer;
	_image_copy.dstSubresource.layerCount = layer_count;
	return *this;
}

ImageCopy& ImageCopy::extent(uint32_t width, uint32_t height, uint32_t depth) {
	_image_copy.extent = { width, height, depth };
	return *this;
}

ImageCopy& ImageCopy::extent(VkExtent3D extent) {
	_image_copy.extent = extent;
	return *this;
}

ImageCopy& ImageCopy::src_offset(int32_t x, int32_t y, int32_t z) {
	_image_copy.srcOffset = { x, y, z };
	return *this;
}
ImageCopy& ImageCopy::dst_offset(int32_t x, int32_t y, int32_t z) {
	_image_copy.dstOffset = { x, y, z };
	return *this;
}

ImageCopy& ImageCopy::src_aspect(VkImageAspectFlags aspect) {
	_image_copy.srcSubresource.aspectMask = aspect;
	return *this;
}
ImageCopy& ImageCopy::src_mip(uint32_t mip) {
	_image_copy.srcSubresource.mipLevel = mip;
	return *this;
}

ImageCopy& ImageCopy::dst_aspect(VkImageAspectFlags aspect) {
	_image_copy.dstSubresource.aspectMask = aspect;
	return *this;
}
ImageCopy& ImageCopy::dst_mip(uint32_t mip) {
	_image_copy.dstSubresource.mipLevel = mip;
	return *this;
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