#pragma once
#include "otcv.h"

namespace otcv {

uint32_t pack(uint16_t a, uint16_t b);

uint32_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

uint64_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e);

void unpack(uint32_t ab, uint16_t& a, uint16_t& b);

std::vector<char> read_file_binary(const std::string& path);

struct ShaderLoadHint {
	enum class Hint {
		Default,
		DynamicUBO,
		DescriptorIndexing
	};
	Hint type = Hint::Default;
	void* custom = nullptr;
};
void get_spirv_resource_bindings(const uint32_t* spirv_bin, uint32_t word_count, ShaderModuleBuilder& builder, ShaderLoadHint::Hint hint, void* custom);

ShaderModule* load_shader(const std::string& spirv_path, ShaderLoadHint::Hint hint = ShaderLoadHint::Hint::Default, void* custom = nullptr);

ShaderModule* load_shader(const std::string& name, const uint32_t* spirv_bin, uint32_t byte_size, ShaderLoadHint::Hint hint = ShaderLoadHint::Hint::Default, void* custom = nullptr);

typedef std::map<std::string, ShaderModule*> ShaderBlob;
ShaderBlob load_shaders_from_dir(const std::string& dir, std::map<std::string, ShaderLoadHint> file_hints = {});

void unload_shader_blob(ShaderBlob& blob);

uint32_t calc_group_count(uint32_t total_invo, uint32_t group_size);

uint32_t find_memory_type(uint32_t usable_types_mask, VkMemoryPropertyFlags required_props);

CommandBuffer* begin_single_time_command_buffer();

// provide a fence in the case of async. Command buffer will not get freed
void end_single_time_command_buffer(otcv::CommandBuffer* cmd_buffer);

// buffer->buffer copy
// void queued_copy(Buffer* src, Buffer* dst);
void staging_queued_copy(void* data, Buffer* buffer);

// buffer->image 
void staging_queued_copy(void* data, size_t size, Image* image, ResourceState src_state, ResourceState dst_state);

void transition_image_state(CommandBuffer* command_buffer, const Image* image,
	ResourceState from_state, ResourceState to_state,
	VkImageSubresourceRange sub_range = {0, 0, 1, 0, 1});

void transition_buffer_state(CommandBuffer* command_buffer, const Buffer* buffer, ResourceState from_state, ResourceState to_state);

void wait_for_and_reset_fences(std::vector<Fence*> fences);

ShaderModuleBuilder::Uniform& uniform_at(GraphicsPipeline* pipeline, uint16_t set, uint16_t binding);

VertexBuffer* screen_quad_ndc();

VkDeviceSize ubo_alignment();

}