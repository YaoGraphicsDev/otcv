#include "otcv_utils.h"
#include "otcv_globals.h"

#include <cassert>
#include <iostream>
#include <fstream>

namespace otcv {

uint32_t pack(uint16_t a, uint16_t b) {
	return (((uint32_t)a) << 16) + (uint32_t)b;
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
void transition_image_state(CommandBuffer* command_buffer, const Image* image, ResourceState from_state, ResourceState to_state, uint32_t mip, uint32_t layer) {
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image->vk_image;
	if (from_state == ResourceState::DepthStencilAttachment || to_state == ResourceState::DepthStencilAttachment) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	barrier.subresourceRange.baseMipLevel = mip;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = layer;
	barrier.subresourceRange.layerCount = 1;

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
	else if (from_state == ResourceState::ComputeSSBORead) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeSSBOWrite) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::ComputeSSBO) {
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
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (from_state == ResourceState::Present) {
		/* "Presentation is a read-only operation that will not affect the content of the presentable images"
		* source: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkQueuePresentKHR.html
		*/
		/* this access and stage masks do not really matter since the final command buffer execution is guaranteed to wait on image ready semaphore,
		 * that is signalled by the swap chain image acquire operation.
		 * Which means by the time of this transition, there is already a full cache flush induced by the semaphore
		 * The only thing that matters is the layout transition
		 */
		barrier.srcAccessMask = VK_ACCESS_NONE;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	else {
		std::cout << "cannot find suitable start state for pipeline barrier, from_state = " << (uint32_t)from_state << std::endl;
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
	else if (to_state == ResourceState::ComputeSSBORead) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeSSBOWrite) {
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (to_state == ResourceState::ComputeSSBO) {
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
	else if (to_state == ResourceState::ColorAttachment) {
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (to_state == ResourceState::Present) {
		/*
		* source: note under https://registry.khronos.org/vulkan/specs/1.2-extensions/html/vkspec.html#VkPresentInfoKHR
		*/
		barrier.dstAccessMask = VK_ACCESS_NONE;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
	else {
		std::cout << "cannot find suitable end state for pipeline barrier, to_state = " << (uint32_t)to_state << std::endl;
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
	else if (from_state == ResourceState::ComputeSSBOWrite) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
	else if (from_state == ResourceState::VertexRead) {
		barrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	}
	else {
		std::cout << "cannot find suitable start state for pipeline barrier, from_state = " << (uint32_t)from_state << std::endl;
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
	else {
		std::cout << "cannot find suitable end state for pipeline barrier, to_state = " << (uint32_t)to_state << std::endl;
		exit(1);
	}

	vkCmdPipelineBarrier(command_buffer->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
		0, nullptr,
		1, &barrier,
		0, nullptr);
}
}