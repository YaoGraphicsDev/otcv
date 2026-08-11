#include "otcv_config.h"
#include "otcv_utils_internal.h"
#include "otcv_globals.h"

#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <spirv_cross/spirv_cross.hpp>
#include <shaderc/shaderc.hpp>


namespace otcv {

uint32_t pack(uint16_t a, uint16_t b) {
	return (uint32_t(a) << 16) | uint32_t(b);
}

uint32_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return (uint32_t(a) << 24) |
		(uint32_t(b) << 16) |
		(uint32_t(c) << 8) |
		uint32_t(d);
}

uint64_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
	return (uint64_t(a) << 32) |
		(uint64_t(b) << 24) |
		(uint64_t(c) << 16) |
		(uint64_t(d) << 8) |
		uint64_t(e);
}

void unpack(uint32_t ab, uint16_t& a, uint16_t& b) {
	a = ab >> 16;
	b = ab & 0xffff;
}

std::vector<char> read_file_binary(const std::string& path) {
	std::ifstream fs;
	fs.open(path, std::ios::ate | std::ios::binary);

	if (!fs.is_open()) {
		std::cout << "Cannot open file " << path << std::endl;
		exit(1);
	}

	size_t size = (size_t)fs.tellg();
	std::vector<char> buffer(size);

	fs.seekg(0);
	fs.read(buffer.data(), buffer.size());

	fs.close();
	return buffer;
}

void strip_all_extensions(const std::string& filepath, std::string& filename, std::vector<std::string>& extensions) {
	std::filesystem::path p(filepath);

	while (p.has_extension()) {
		filename = p.stem().string();
		extensions.push_back(p.extension().string());
		p = p.stem();
	}
}

void get_spirv_resource_bindings(const uint32_t* spirv_bin, uint32_t word_count, ShaderModuleBuilder& builder, ShaderLoadHint::Hint hint, void* custom) {
	spirv_cross::Compiler compiler(spirv_bin, word_count);
	spirv_cross::ShaderResources shader_res = compiler.get_shader_resources();

	// reinterpret custom data based on hint
	std::set<uint16_t>* dynamic_sets = nullptr; // set numbers
	std::map<uint32_t, uint32_t>* indexing_limits = nullptr; // pack(set, binding) -- limit
	if (hint == ShaderLoadHint::Hint::DynamicUBO && custom) {
		dynamic_sets = static_cast<std::set<uint16_t>*>(custom);
	}
	else if (hint == ShaderLoadHint::Hint::DescriptorIndexing && custom) {
		indexing_limits = static_cast<std::map<uint32_t, uint32_t>*>(custom);
	}

	// ubos
	for (const auto& ubo : shader_res.uniform_buffers) {
		uint32_t set = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
		std::string name = compiler.get_name(ubo.id);
		const spirv_cross::SPIRType& type = compiler.get_type(ubo.base_type_id);
		size_t size = compiler.get_declared_struct_size(type); // layout of ubo is std140 by default

		ShaderModuleBuilder::Uniform& ubo_builder = builder.uniform(uint16_t(set), uint16_t(binding));
		// check hints
		if (hint == ShaderLoadHint::Hint::DynamicUBO
			&& dynamic_sets
			&& dynamic_sets->find(set) != dynamic_sets->end()) {
			ubo_builder.type(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		}
		else {
			ubo_builder.type(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		}
		// set all member names inside UBO and their offsets
		for (uint32_t i = 0; i < type.member_types.size(); ++i) {
			std::string member_name = compiler.get_member_name(ubo.base_type_id, i);
			uint32_t offset = compiler.get_member_decoration(ubo.base_type_id, i, spv::DecorationOffset);
			ubo_builder.field(member_name, offset);
		}

		// get array size
		uint32_t array_size = 1;
		if (hint == ShaderLoadHint::Hint::DescriptorIndexing
			&& indexing_limits
			&& indexing_limits->find(pack(set, binding)) != indexing_limits->end()) {
			// bindless
			array_size = (*indexing_limits)[pack(set, binding)];
		}
		else {
			// not bindless. Regular old sampler array
			if (!type.array.empty()) {
				array_size = type.array[0]; //type.array is holding the dimensions of an array type. array[0] -- first dimension
			}
		}

		ubo_builder
			.array_count(array_size)
			.size(size)
			.name(name)
			.end();
	}

	// texture samplers
	for (const auto& textures : shader_res.sampled_images) {
		uint32_t set = compiler.get_decoration(textures.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(textures.id, spv::DecorationBinding);
		std::string name = compiler.get_name(textures.id);

		spirv_cross::SPIRType type = compiler.get_type(textures.type_id);
		uint32_t array_size = 1;
		if (hint == ShaderLoadHint::Hint::DescriptorIndexing
			&& indexing_limits
			&& indexing_limits->find(pack(set, binding)) != indexing_limits->end()) {
			// bindless
			array_size = (*indexing_limits)[pack(set, binding)];
		} else {
			// not bindless. Regular old sampler array
			if (!type.array.empty()) {
				array_size = type.array[0]; //type.array is holding the dimensions of an array type. array[0] -- first dimension
			}
		}
		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.array_count(array_size)
			.name(name)
			.end();
	}

	// samplers
	for (const auto& samplers : shader_res.separate_samplers) {
		uint32_t set = compiler.get_decoration(samplers.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(samplers.id, spv::DecorationBinding);
		std::string name = compiler.get_name(samplers.id);

		spirv_cross::SPIRType type = compiler.get_type(samplers.type_id);
		uint32_t array_size = 1;
		if (hint == ShaderLoadHint::Hint::DescriptorIndexing
			&& indexing_limits
			&& indexing_limits->find(pack(set, binding)) != indexing_limits->end()) {
			// bindless
			array_size = (*indexing_limits)[pack(set, binding)];
		}
		else {
			// not bindless. Regular old sampler array
			if (!type.array.empty()) {
				array_size = type.array[0]; //type.array is holding the dimensions of an array type. array[0] -- first dimension
			}
		}
		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_SAMPLER)
			.array_count(array_size)
			.name(name)
			.end();
	}

	// sampled images
	for (const auto& images : shader_res.separate_images) {
		uint32_t set = compiler.get_decoration(images.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(images.id, spv::DecorationBinding);
		std::string name = compiler.get_name(images.id);

		spirv_cross::SPIRType type = compiler.get_type(images.type_id);
		uint32_t array_size = 1;
		if (hint == ShaderLoadHint::Hint::DescriptorIndexing
			&& indexing_limits
			&& indexing_limits->find(pack(set, binding)) != indexing_limits->end()) {
			// bindless
			array_size = (*indexing_limits)[pack(set, binding)];
		}
		else {
			// not bindless. Regular old sampler array
			if (!type.array.empty()) {
				array_size = type.array[0]; //type.array is holding the dimensions of an array type. array[0] -- first dimension
			}
		}
		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
			.array_count(array_size)
			.name(name)
			.end();
	}

	// storage images
	for (const auto& images : shader_res.storage_images) {
		uint32_t set = compiler.get_decoration(images.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(images.id, spv::DecorationBinding);
		std::string name = compiler.get_name(images.id);

		spirv_cross::SPIRType type = compiler.get_type(images.type_id);
		uint32_t array_size = 1;
		if (!type.array.empty()) {
			array_size = type.array[0];
		}
		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.array_count(array_size)
			.name(name)
			.end();
	}

	// ssbos
	for (const auto& ssbo : shader_res.storage_buffers) {
		uint32_t set = compiler.get_decoration(ssbo.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(ssbo.id, spv::DecorationBinding);
		std::string name = compiler.get_name(ssbo.id);

		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
			.name(name)
			.end();
	}

	// accecleration structure
	for (const auto& as : shader_res.acceleration_structures) {
		uint32_t set = compiler.get_decoration(as.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(as.id, spv::DecorationBinding);
		std::string name = compiler.get_name(as.id);

		spirv_cross::SPIRType type = compiler.get_type(as.type_id);
		uint32_t array_size = 1;
		if (!type.array.empty()) {
			array_size = type.array[0];
		}
		builder
			.uniform(uint16_t(set), uint16_t(binding))
			.type(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
			.array_count(array_size)
			.name(name)
			.end();
	}

	// push constants
	uint32_t pc_offset = 0;
	uint32_t pc_size = 0;
	if (!shader_res.push_constant_buffers.empty()) {
		for (size_t i = 0; i < shader_res.push_constant_buffers.size(); ++i) {
			const auto& pc = shader_res.push_constant_buffers[i];
			spirv_cross::SPIRType type = compiler.get_type(pc.base_type_id);
			for (uint32_t i = 0; i < type.member_types.size(); i++) {
				std::string member_name = compiler.get_member_name(pc.base_type_id, i);
				uint32_t offset = compiler.type_struct_member_offset(type, i);
				uint32_t size = compiler.get_declared_struct_member_size(type, i);
				builder.add_push_constant(member_name, (uint16_t)offset, (uint16_t)size);
			}
		}
	}
}

ShaderModule* load_shader(const std::string& name, const uint32_t* spirv_bin, uint32_t byte_size, ShaderLoadHint::Hint hint, void* custom) {
	ShaderModuleBuilder builder;
	builder.name(name);
	builder.spirv_binary(spirv_bin, byte_size);

	get_spirv_resource_bindings(spirv_bin, byte_size / sizeof(uint32_t), builder, hint, custom);

	ShaderModule* shader = builder.build();
	return shader;
}

ShaderModule* load_shader(const std::string& spirv_path, ShaderLoadHint::Hint hint, void* custom) {
	std::vector<char> code_bytes = std::move(read_file_binary(spirv_path));
	return load_shader(spirv_path, (uint32_t*)code_bytes.data(), code_bytes.size(), hint, custom);
}

std::map<std::string, ShaderModule*> load_shaders_from_dir(const std::string& dir, std::map<std::string, ShaderLoadHint> file_hints) {
	if (!std::filesystem::exists(dir)) {
		std::cout << "Directory " << dir << " does not exist" << std::endl;
		exit(1);
	}
	if (!std::filesystem::is_directory(dir)) {
		std::cout << "Path " << dir << " is not a directory" << std::endl;
		exit(1);
	}

	std::map<std::string, ShaderModule*> shader_map;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.is_regular_file()) {
			std::string filename = entry.path().stem().string();
			std::string extension_spv = entry.path().extension().string();
			std::string extension_type = entry.path().stem().extension().string();
			if (extension_spv != ".spv") {
				std::cout << "unrecognized file extension of file : " << entry.path().string() << std::endl;
				continue;
			}
			
			auto iter = file_hints.find(filename);
			if (iter != file_hints.end()) {
				ShaderLoadHint hint = iter->second;
				shader_map[filename] = load_shader(entry.path().string(), hint.type, hint.custom);
			} else {
				shader_map[filename] = load_shader(entry.path().string());
			}
		}
	}

	return shader_map;
}

void unload_shader_blob(ShaderBlob& blob) {
	for (auto& p : blob) {
		p.second->destroy();
	}
	blob.clear();
}

uint32_t calc_group_count(uint32_t total_invo, uint32_t group_size) {
	if (total_invo % group_size == 0) {
		return total_invo / group_size;
	}
	else {
		return total_invo / group_size + 1;
	}
}

uint32_t find_memory_type(uint32_t usable_types_mask, VkMemoryPropertyFlags required_props) {
	VkPhysicalDeviceMemoryProperties supported_props{};
	vkGetPhysicalDeviceMemoryProperties(g_physical_device.vk_physical_device, &supported_props);

	for (uint32_t i = 0; i < supported_props.memoryTypeCount; ++i) {
		if (usable_types_mask & (1 << i) && supported_props.memoryTypes[i].propertyFlags & required_props) {
			return i;
		}
	}

	std::cout << "cannot find suitable memory type" << std::endl;
	exit(1);
}

CommandBuffer* begin_single_time_command_buffer() {
	otcv::CommandBuffer* cmd_buf = g_command_pool->allocate();
	cmd_buf->begin(true);
	return cmd_buf;
}

void end_single_time_command_buffer(otcv::CommandBuffer* cmd_buffer) {
	cmd_buffer->end();
	{
		otcv::QueueSubmit submit;
		submit
			.batch()
			.add_command_buffer(cmd_buffer)
			.end();
		g_queue.submit(submit);
	}
	g_queue.idle_wait();
	g_command_pool->free(cmd_buffer);
}

// image has to be in transfer dst layout
void queued_copy(Buffer* src, Image* dst) {
	if (!src || !dst) {
		return;
	}

	assert(src->builder._info.size == 
		dst->builder._image_info.extent.width * 
		dst->builder._image_info.extent.height *
		dst->builder._image_info.extent.depth);
	VkBufferImageCopy copy_region{};
	copy_region.bufferOffset = 0;
	copy_region.bufferRowLength = 0;
	copy_region.bufferImageHeight = 0;
	copy_region.imageSubresource.aspectMask = dst->builder._view_info.subresourceRange.aspectMask;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageOffset = { 0, 0, 0 };
	copy_region.imageExtent = dst->builder._image_info.extent;

	CommandBuffer* cmd_buffer = begin_single_time_command_buffer();
	// TODO: determin image state
	vkCmdCopyBufferToImage(cmd_buffer->vk_command_buffer, src->vk_buffer, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
	end_single_time_command_buffer(cmd_buffer);
}
void staging_queued_copy(void* data, Buffer* buffer) {
	if (!data || !buffer) {
		return;
	}
	VkDeviceSize size = buffer->builder._info.size;
	otcv::BufferBuilder b_builder;
	otcv::Buffer* staging = b_builder.size(size).usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).host_access(otcv::BufferBuilder::Access::Coherent).build();
	memcpy(staging->mapped, data, size);
	VkBufferCopy region{};
	region.srcOffset = 0;
	region.dstOffset = 0;
	region.size = size;

	CommandBuffer* cmd_buffer = begin_single_time_command_buffer();
	vkCmdCopyBuffer(cmd_buffer->vk_command_buffer, staging->vk_buffer, buffer->vk_buffer, 1, &region);
	end_single_time_command_buffer(cmd_buffer);
	staging->destroy();
}
void staging_queued_copy(void* data, size_t size, Image* image, ResourceState src_state, ResourceState dst_state) {
	if (size == 0 || !data) {
		return;
	}

	otcv::BufferBuilder builder;
	otcv::Buffer* staging = builder
		.size(size)
		.usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
		.host_access(otcv::BufferBuilder::Access::Coherent)
		.build();
	otcv::CommandBuffer* command_buffer = otcv::begin_single_time_command_buffer();
	memcpy(staging->mapped, data, size);
	assert(image->builder._image_info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	command_buffer->cmd_image_memory_barrier(image, src_state, otcv::ResourceState::TransferDst);

	VkBufferImageCopy copy_region{};
	copy_region.bufferOffset = 0;
	copy_region.bufferRowLength = 0;
	copy_region.bufferImageHeight = 0;
	copy_region.imageSubresource.aspectMask = image->builder._view_info.subresourceRange.aspectMask;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageOffset = { 0, 0, 0 };
	copy_region.imageExtent = image->builder._image_info.extent;
	vkCmdCopyBufferToImage(command_buffer->vk_command_buffer, staging->vk_buffer, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

	// copy_buffer_to_image(command_buffer->vk_command_buffer, staging->vk_buffer, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, width, height, aspect);
	command_buffer->cmd_image_memory_barrier(image, otcv::ResourceState::TransferDst, dst_state);

	otcv::end_single_time_command_buffer(command_buffer);
	staging->destroy();
}

void transition_image_state(CommandBuffer* command_buffer, const Image* image, ResourceState from_state, ResourceState to_state, VkImageSubresourceRange sub_range) {
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image->vk_image;
	if (sub_range.aspectMask == VK_IMAGE_ASPECT_NONE) {
		// default image view subresource range
		barrier.subresourceRange = image->builder._view_info.subresourceRange;
		// one caveat to depth stencil image: VUID-VkImageMemoryBarrier-image-03320. Set both depth and stencil aspect mask regardless of either or both aspects get set in image view
		if ((barrier.subresourceRange.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) || (barrier.subresourceRange.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}    
	else {
		barrier.subresourceRange = sub_range;
	}

	//if (from_state == ResourceState::DepthStencilAttachment || to_state == ResourceState::DepthStencilAttachment) {
	//	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	//}
	//else if (from_state == ResourceState::ColorAttachment || to_state == ResourceState::ColorAttachment ||
	//	from_state == ResourceState::FragSample || to_state == ResourceState::FragSample ||
	//	from_state == ResourceState::PresentAvailableForTransferDst || to_state == ResourceState::PresentAvailableForTransferDst ||
	//	from_state == ResourceState::PresentReady || to_state == ResourceState::PresentReady) {
	//	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//}
	//else if (sub_range.aspectMask == VK_IMAGE_ASPECT_NONE) {
	//	std::cout << "Cannot figure out image aspect. from_state = " << (uint32_t)from_state << ", to_state = " << (uint32_t)to_state << std::endl;
	//	assert(false); // can't handle the correct aspect right now.
	//	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//}

	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE;
	VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_NONE;

	if (from_state == ResourceState::Created) {
		barrier.srcAccessMask = VK_ACCESS_NONE;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	else if (from_state == ResourceState::TransferDst) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (from_state == ResourceState::TransferSrc) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (from_state == ResourceState::ComputeStorageRead) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeStorageWrite) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeStorage) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeSample) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::FragSample) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (from_state == ResourceState::ColorAttachment) {
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (from_state == ResourceState::DepthStencilAttachment) {
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	else if (from_state == ResourceState::DepthStencilAttachmentRead) {
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	else if (from_state == ResourceState::PresentAvailableForTransferDst) {
		/* 
		* The final command buffer that contains transfer submit should wait behind swapchain image ready semaphore,
		* which mean the transfer operation itself is allowed to start only after swapchain image is 1.acquired and 2.fully flushed.
		* Actually, the full flushing part is irrelavant because:
		* "Presentation is a read-only operation that will not affect the content of the presentable images"
		* source: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkQueuePresentKHR.html
		* 
		* what only matters is the guarantee that transfer can only happen after acquisition.
		* Chain a barrier after the semaphore
		* https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/#:~:text=Execution%20dependency%20chain%20with,after%20semaphore%20is%20signaled.
		* Also, the note under VkSubmitInfo
		* https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo.html#:~:text=A%20common%20scenario,performed%20in%20between.
		*/
		barrier.srcAccessMask = VK_ACCESS_NONE;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else {
		std::cout << "cannot find suitable start state for pipeline barrier, from_state = " << (uint32_t)from_state << std::endl;
		assert(false);
		exit(1);
	}

	if (to_state == ResourceState::TransferDst) {
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (to_state == ResourceState::TransferSrc) {
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (to_state == ResourceState::ComputeStorageRead) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeStorageWrite) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeStorage) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeSample) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::FragSample) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (to_state == ResourceState::DepthStencilAttachment) {
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	else if (to_state == ResourceState::DepthStencilAttachmentRead) {
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	else if (to_state == ResourceState::ColorAttachment) {
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (to_state == ResourceState::PresentReady) {
		/*
		* source: note under https://registry.khronos.org/vulkan/specs/1.2-extensions/html/vkspec.html#VkPresentInfoKHR
		*/
		barrier.dstAccessMask = VK_ACCESS_NONE;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
	else {
		std::cout << "cannot find suitable end state for pipeline barrier, to_state = " << (uint32_t)to_state << std::endl;
		assert(false);
		exit(1);
	}
	vkCmdPipelineBarrier(command_buffer->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}
void transition_buffer_state(CommandBuffer* command_buffer, const Buffer* buffer, ResourceState from_state, ResourceState to_state) {
	VkBufferMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer->vk_buffer;
	barrier.offset = 0;
	barrier.size = buffer->builder._info.size;

	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE;
	VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_NONE;
	if (from_state == ResourceState::Created) {
		barrier.srcAccessMask = VK_ACCESS_NONE;
		src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	else if (from_state == ResourceState::ComputeSSBORead) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::FragSSBORead) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeSSBOWrite || from_state == ResourceState::ComputeSSBO) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::VertexRead) {
		barrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	}
	else if (from_state == ResourceState::HostWrite) {
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT;
	}
	else if (from_state == ResourceState::IndirectRead) {
		barrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	}
	else if (from_state == ResourceState::TransferDst) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else {
		std::cout << "cannot find suitable start state for pipeline barrier, from_state = " << (uint32_t)from_state << std::endl;
		assert(false);
		exit(1);
	}

	if (to_state == ResourceState::VertexRead) {
		barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	}
	else if (to_state == ResourceState::ComputeSSBOWrite) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeSSBORead) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::FragSSBORead) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeSSBO) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::TransferDst) {
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (to_state == ResourceState::IndirectRead) {
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	}
	else if (to_state == ResourceState::IndexRead) {
		barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	}
	else if (to_state == ResourceState::TransferSrc) {
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else {
		std::cout << "cannot find suitable end state for pipeline barrier, to_state = " << (uint32_t)to_state << std::endl;
		assert(false);
		exit(1);
	}

	vkCmdPipelineBarrier(command_buffer->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
		0, nullptr,
		1, &barrier,
		0, nullptr);
}

void wait_for_and_reset_fences(std::vector<Fence*> fences) {
	std::vector<VkFence> vk_fences(fences.size());
	for (size_t i = 0; i < fences.size(); ++i) {
		vk_fences[i] = fences[i]->vk_fence;
	}
	vkWaitForFences(g_device->vk_device, vk_fences.size(), vk_fences.data(), VK_TRUE, UINT64_MAX);
	vkResetFences(g_device->vk_device, vk_fences.size(), vk_fences.data());
}

ShaderModuleBuilder::Uniform& uniform_at(GraphicsPipeline* pipeline, uint16_t set, uint16_t binding) {
	uint32_t key = otcv::pack(set, binding);
	// This specific set & binding might live in vertex or fragment shader, or both
	// if in both, both declarations are supposed to be exactly the same. If not, go with the one in fragment
	auto v_iter = pipeline->builder._vertex_shader->builder._uniforms.find(key);
	auto f_iter = pipeline->builder._fragment_shader->builder._uniforms.find(key);

	auto& iter = f_iter;
	if (f_iter != pipeline->builder._fragment_shader->builder._uniforms.end()) {
		iter = f_iter;
	}
	else if (v_iter != pipeline->builder._vertex_shader->builder._uniforms.end()) {
		iter = v_iter;
	}
	else {
		assert(false);
		std::cout << "Cannot find uniform at set = " << set << ", binding = " << binding << std::endl;
		return ShaderModuleBuilder::Uniform();
	}

	return iter->second;
}

VertexBuffer* screen_quad_ndc() {
	std::vector<float> v_data = { // depth lies in the middle of NDC space
		// x | y | z | u | v
		-1.0f, -1.0f, 0.5f, 0.0f, 0.0f,
		-1.0f,  3.0f, 0.5f, 0.0f, 2.0f,
		 3.0f, -1.0f, 0.5f, 2.0f, 0.0f,
	};

	BufferBuilder bb;
	bb.size(v_data.size() * sizeof(float))
		.host_access(BufferBuilder::Access::Invisible)
		.usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	VertexBufferBuilder vbb;
	vbb.add_binding(bb, v_data.data())
		.add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3)
		.add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2);
	otcv::VertexBuffer* vb = vbb.build();
	return vb;
}

VkDeviceSize ubo_alignment() {
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(otcv::get_context().physical_device->vk_physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	VkDeviceSize ubo_alignment = limits.minUniformBufferOffsetAlignment;
	return ubo_alignment;
}

}