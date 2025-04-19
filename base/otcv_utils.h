#pragma once
#include "otcv.h"

namespace otcv {
uint32_t pack(uint16_t a, uint16_t b);

void unpack(uint32_t ab, uint16_t& a, uint16_t& b);

std::vector<char> read_file_binary(const std::string& path);

void get_spirv_resource_bindings(const uint32_t* spirv_bin, uint32_t word_count, ShaderModuleBuilder& builder);

ShaderModule* load_shader(const std::string& spirv_path);

ShaderModule* load_shader(const uint32_t* spirv_bin, uint32_t byte_size);

std::map<std::string, ShaderModule*> load_shaders_from_dir(const std::string& dir);

uint32_t calc_group_count(uint32_t total_invo, uint32_t group_size);

uint32_t find_memory_type(uint32_t usable_types_mask, VkMemoryPropertyFlags required_props);

CommandBuffer* begin_single_time_command_buffer();

void end_single_time_command_buffer(otcv::CommandBuffer* cmd_buffer);

// buffer->buffer copy
// void queued_copy(Buffer* src, Buffer* dst);
void staging_queued_copy(void* data, Buffer* buffer);

// buffer->image 
void staging_queued_copy(void* data, size_t size, Image* image, ResourceState src_state, ResourceState dst_state);

void transition_image_state(CommandBuffer* command_buffer, const Image* image,
	ResourceState from_state, ResourceState to_state,
	uint32_t mip, uint32_t layer);

void transition_buffer_state(CommandBuffer* command_buffer, const Buffer* buffer, ResourceState from_state, ResourceState to_state);

void wait_for_and_reset_fences(std::vector<Fence*> fences);
}