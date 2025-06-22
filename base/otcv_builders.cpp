#include "otcv.h"
#include "otcv_globals.h"
#include "otcv_utils.h"

#include <iostream>
#include <fstream>
#include <cassert>

namespace otcv {

// sampler builder
SamplerBuilder::SamplerBuilder() {
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(g_physical_device.vk_physical_device, &properties);

	_info = VkSamplerCreateInfo{};
	_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	_info.magFilter = VK_FILTER_LINEAR;
	_info.minFilter = VK_FILTER_LINEAR;
	_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	_info.addressModeV= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	_info.mipLodBias = 0.0f;
	_info.anisotropyEnable = VK_TRUE;
	_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	_info.compareEnable = VK_FALSE;
	_info.minLod = 0.0f;
	// https://registry.khronos.org/vulkan/specs/latest/man/html/VK_LOD_CLAMP_NONE.html
	// essntially telling the sampler to use all levels of mips there are
	_info.maxLod = VK_LOD_CLAMP_NONE; 
	_info.unnormalizedCoordinates = VK_FALSE;
}
SamplerBuilder& SamplerBuilder::filter(VkFilter min, VkFilter mag) {
	_info.magFilter = mag;
	_info.minFilter = min;

	if (min == VK_FILTER_NEAREST && mag == VK_FILTER_NEAREST) {
		_info.anisotropyEnable = VK_FALSE;
		_info.maxAnisotropy = 0.0f;
	} else {
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(g_physical_device.vk_physical_device, &properties);
		_info.anisotropyEnable = VK_TRUE;
		_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	}
	return *this;
}
SamplerBuilder& SamplerBuilder::mipmap(VkSamplerMipmapMode mode) {
	_info.mipmapMode = mode;
	return *this;
}
SamplerBuilder& SamplerBuilder::address_mode(VkSamplerAddressMode mode, VkBorderColor color) {
	_info.addressModeU = mode;
	_info.addressModeV = mode;
	_info.addressModeW = mode;
	_info.borderColor = color;
	return *this;
}
SamplerBuilder& SamplerBuilder::address_mode_u(VkSamplerAddressMode mode, VkBorderColor color) {
	_info.addressModeU = mode;
	_info.borderColor = color;
	return *this;
}
SamplerBuilder& SamplerBuilder::address_mode_v(VkSamplerAddressMode mode, VkBorderColor color) {
	_info.addressModeV = mode;
	_info.borderColor = color;
	return *this;
}
SamplerBuilder& SamplerBuilder::address_mode_w(VkSamplerAddressMode mode, VkBorderColor color) {
	_info.addressModeW = mode;
	_info.borderColor = color;
	return *this;
}
SamplerBuilder& SamplerBuilder::lod(float min, float max, float mip_bias) {
	_info.minLod = min;
	_info.maxLod = max;
	_info.mipLodBias = mip_bias;
	return *this;
}
SamplerBuilder& SamplerBuilder::compare(VkCompareOp op) {
	_info.compareEnable = VK_TRUE;
	_info.compareOp = op;
	return *this;
}
Sampler* SamplerBuilder::build() {
	Sampler* sampler = new Sampler(*this);
	g_user_samplers.insert(std::shared_ptr<Sampler>(sampler));
	return sampler;
}

// image builder
ImageBuilder::ImageBuilder() {
	_has_mips = false;
	_name = "";

	_image_info = VkImageCreateInfo{};
	_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	_image_info.imageType = VK_IMAGE_TYPE_2D;
	_image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	_image_info.extent = { 0, 0, 0 };
	_image_info.mipLevels = 1;
	_image_info.arrayLayers = 1;
	_image_info.samples = VK_SAMPLE_COUNT_1_BIT; // see VkSampleCountFlagBits
	_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	_image_info.usage = 0;
	_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	_image_info.queueFamilyIndexCount = 0;
	_image_info.pQueueFamilyIndices = nullptr;
	_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	_view_info = VkImageViewCreateInfo{};
	_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	_view_info.image = VK_NULL_HANDLE;
	_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	_view_info.subresourceRange.baseMipLevel = 0;
	_view_info.subresourceRange.levelCount = 1;
	_view_info.subresourceRange.baseArrayLayer = 0;
	_view_info.subresourceRange.layerCount = 1;
}
ImageBuilder& ImageBuilder::name(const std::string& name) {
	_name = name;
	return *this;
}
ImageBuilder& ImageBuilder::image_type(VkImageType type) {
	_image_info.imageType = type;
	return *this;
}
ImageBuilder& ImageBuilder::format(VkFormat format) {
	_image_info.format = format;
	_view_info.format = format;
	return *this;
}
ImageBuilder& ImageBuilder::size(uint32_t width, uint32_t height, uint32_t depth) {
	_image_info.extent = VkExtent3D{ width, height, depth };
	return *this;
}
ImageBuilder& ImageBuilder::enable_mips(bool enable) {
	_has_mips = enable;
	return *this;
}
ImageBuilder& ImageBuilder::layers(uint32_t n) {
	_image_info.arrayLayers = n;
	_view_info.subresourceRange.layerCount = n;
	return *this;
}
ImageBuilder& ImageBuilder::samples(uint32_t n) {
	_image_info.samples = (VkSampleCountFlagBits)n;
	return *this;
}
ImageBuilder& ImageBuilder::usage(VkImageUsageFlags usage) {
	_image_info.usage = usage;
	return *this;
}
ImageBuilder& ImageBuilder::initial_layout(VkImageLayout layout) {
	_image_info.initialLayout = layout;
	return *this;
}
ImageBuilder& ImageBuilder::view_type(VkImageViewType type) {
	_view_info.viewType = type;
	return *this;
}
ImageBuilder& ImageBuilder::aspect(VkImageAspectFlags aspect) {
	_view_info.subresourceRange.aspectMask = aspect;
	return *this;
}

ImageBuilder& ImageBuilder::swizzle(VkComponentSwizzle r, VkComponentSwizzle g, VkComponentSwizzle b, VkComponentSwizzle a) {
	_view_info.components.r = r;
	_view_info.components.g = g;
	_view_info.components.b = b;
	_view_info.components.a = a;
	return *this;
}
Image* ImageBuilder::build() {
	Image* image = new Image(*this);
	g_user_images.insert(std::shared_ptr<Image>(image));
	return image;
}

// buffer builder
BufferBuilder::BufferBuilder() {
	_info = VkBufferCreateInfo{};
	_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	_info.size = 0;
	_info.usage = 0;
	_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	_info.queueFamilyIndexCount = 0;
	_info.pQueueFamilyIndices = nullptr;

	_mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}
BufferBuilder& BufferBuilder::size(VkDeviceSize size) {
	_info.size = size;
	return *this;
}
BufferBuilder& BufferBuilder::usage(VkBufferUsageFlags usage) {
	_info.usage = usage;
	return *this;
}
BufferBuilder& BufferBuilder::host_access(Access access) {
	VkMemoryPropertyFlags props = 0;
	if (access == Access::Invisible) {
		props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	else if (access == Access::Coherent) {
		props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else if (access == Access::Incoherent) {
		props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	}
	_mem_props = props;
	return *this;
}
Buffer* BufferBuilder::build() {
	Buffer* buffer = new Buffer(*this);
	g_user_buffers.insert(std::shared_ptr<Buffer>(buffer));
	return buffer;
}

// vertex buffer builder
VertexBufferBuilder& VertexBufferBuilder::add_binding(BufferBuilder b_builder, void* data) {
	_binding_descs.emplace_back();
	_binding_descs.back().binding = _binding_descs.size() - 1;
	_binding_descs.back().stride = 0;
	_binding_descs.back().inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	b_builder._info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	_buffer_builders.push_back(b_builder);
	_data_handles.push_back(data);
	return *this;
}
VertexBufferBuilder& VertexBufferBuilder::add_binding() {
	_binding_descs.emplace_back();
	_binding_descs.back().binding = _binding_descs.size() - 1;
	_binding_descs.back().stride = 0;
	_binding_descs.back().inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	_buildable = false;
	return *this;
}
VertexBufferBuilder& VertexBufferBuilder::add_attribute(uint32_t binding, VkFormat format, uint32_t byte_size) {
	_attr_descs.emplace_back();
	_attr_descs.back().location = _attr_descs.size() - 1;
	_attr_descs.back().binding = binding;
	_attr_descs.back().format = format;
	_attr_descs.back().offset = _binding_descs[binding].stride;

	_binding_descs[binding].stride += byte_size;
	return *this;
}
VertexBufferBuilder& VertexBufferBuilder::add_attribute_padding(uint32_t binding, uint32_t byte_size) {
	_binding_descs[binding].stride += byte_size;
	return *this;
}
VertexBuffer* VertexBufferBuilder::build() {
	assert(_buildable);
	VertexBuffer* vb = new VertexBuffer(*this);
	g_user_vertex_buffers.insert(std::shared_ptr<VertexBuffer>(vb));
	return vb;
}

// shader module
ShaderModuleBuilder::Uniform::Uniform() {
	this->_parent = nullptr;
	this->_name = "";
	this->_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	this->_size = 0;
	this->_array_count = 1;
}
ShaderModuleBuilder::Uniform::Uniform(ShaderModuleBuilder* parent) {
	this->_parent = parent;
	this->_name = "";
	this->_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	this->_size = 0;
	this->_array_count = 1;
}
ShaderModuleBuilder::Uniform&
ShaderModuleBuilder::Uniform::type(VkDescriptorType type) {
	this->_type = type;
	return *this;
}
ShaderModuleBuilder::Uniform&
ShaderModuleBuilder::Uniform::size(size_t size) {
	this->_size = size;
	return *this;
}
ShaderModuleBuilder::Uniform&
ShaderModuleBuilder::Uniform::field(const std::string& member, uint32_t offset) {
	this->_field_offset_map[member] = offset;
	return *this;
}
ShaderModuleBuilder::Uniform&
ShaderModuleBuilder::Uniform::name(const std::string& name) {
	this->_name = name;
	return *this;
}
ShaderModuleBuilder::Uniform&
ShaderModuleBuilder::Uniform::array_count(uint32_t n) {
	this->_array_count = n;
	return *this;
}
ShaderModuleBuilder& ShaderModuleBuilder::Uniform::end() {
	return *_parent;
}
ShaderModuleBuilder& ShaderModuleBuilder::name(const std::string& name) {
	this->_name = name;
	return *this;
}
ShaderModuleBuilder& ShaderModuleBuilder::spirv_binary(const uint32_t* data, size_t byte_size) {
	this->_spirv_data = data;
	this->_spirv_byte_size = byte_size;
	return *this;
}
//ShaderModuleBuilder& ShaderModuleBuilder::spirv_reflect_path(const std::string& path) {
//	this->_spirv_reflect_path = path;
//	return *this;
//}
ShaderModuleBuilder::Uniform& ShaderModuleBuilder::uniform(uint16_t set, uint16_t binding) {
	uint32_t key = otcv::pack(set, binding);
	this->_uniforms[key] = Uniform(this);
	return this->_uniforms[key];
}
ShaderModuleBuilder&
ShaderModuleBuilder::add_push_constant(const std::string& member_name, uint16_t offset, uint16_t size) {
	this->_push_constants[member_name] = pack(offset, size);
	// this->_push_constant_offset_size = pack(offset, size);
	return *this;
}
ShaderModule* ShaderModuleBuilder::build() {
	ShaderModule* shader = nullptr;
	if (_spirv_data && _spirv_byte_size > 0) {
		shader = new ShaderModule(*this, (const char*)_spirv_data, _spirv_byte_size);
	}
	
	g_user_shader_modules.insert(std::shared_ptr<ShaderModule>(shader));
	return shader;
}

// queue submit
QueueSubmit::Batch& QueueSubmit::batch() {
	_batches.emplace_back(this);
	return _batches.back();
}
QueueSubmit& QueueSubmit::signal(Fence* fence) {
	this->_fence = fence;
	return *this;
}

QueueSubmit::Batch::Batch(QueueSubmit* parent) {
	this->_parent = parent;
	this->_cmd_buffers = {};
	this->_signal_semaphores = {};
	this->_wait_stages = {};
}
QueueSubmit::Batch&
QueueSubmit::Batch::add_command_buffer(CommandBuffer* cmd_buffer) {
	this->_cmd_buffers.push_back(cmd_buffer->vk_command_buffer);
	return *this;
}
QueueSubmit::Batch&
QueueSubmit::Batch::add_wait(Semaphore* semaphore, VkPipelineStageFlags wait_stage) {
	this->_wait_semaphores.push_back(semaphore->vk_semaphore);
	this->_wait_stages.push_back(wait_stage);
	return *this;
}
QueueSubmit::Batch&
QueueSubmit::Batch::add_signal(Semaphore* semaphore) {
	this->_signal_semaphores.push_back(semaphore->vk_semaphore);
	return *this;
}

QueuePresent& QueuePresent::image_index(uint32_t index) {
	_image_index = index;
	return *this;
}
QueuePresent& QueuePresent::add_wait(Semaphore* semaphore) {
	_wait_semaphores.push_back(semaphore->vk_semaphore);
	return *this;
}

// render pass builder
RenderPassBuilder::Attachment&
RenderPassBuilder::attachment() {
	_attachments.emplace_back(this);
	return _attachments.back();
}
RenderPassBuilder::Subpass&
RenderPassBuilder::subpass() {
	_subpasses.emplace_back(this);
	return _subpasses.back();
}
RenderPassBuilder::Dependency& 
RenderPassBuilder::dependencies() {
	_dependencies.emplace_back(this);
	return _dependencies.back();
}
RenderPassBuilder::Attachment::Attachment(RenderPassBuilder* parent) {
	this->_parent = parent;
	this->_desc = VkAttachmentDescription{};
}
RenderPassBuilder::Attachment&
RenderPassBuilder::Attachment::format_samples(VkFormat format, uint32_t samples) {
	_desc.format = format;
	_desc.samples = (VkSampleCountFlagBits)samples;
	return *this;
}
RenderPassBuilder::Attachment&
RenderPassBuilder::Attachment::layouts(VkImageLayout initial, VkImageLayout final) {
	_desc.initialLayout = initial;
	_desc.finalLayout = final;
	return *this;
}
RenderPassBuilder::Attachment& 
RenderPassBuilder::Attachment::load_store(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op,
	VkAttachmentLoadOp stencil_load_op, VkAttachmentStoreOp stencil_store_op) {
	_desc.loadOp = load_op;
	_desc.storeOp = store_op;
	_desc.stencilLoadOp = stencil_load_op;
	_desc.stencilStoreOp = stencil_store_op;
	return *this;
}
RenderPassBuilder::Subpass::Subpass(RenderPassBuilder* parent) {
	this->_parent = parent;
}
RenderPassBuilder::Subpass&
RenderPassBuilder::Subpass::ref_color(uint32_t attachment_id, VkImageLayout layout) {
	_refs_color.push_back({ attachment_id, layout });
	return *this;
}
RenderPassBuilder::Subpass&
RenderPassBuilder::Subpass::ref_depth_stencil(uint32_t attachment_id, VkImageLayout layout) {
	_ref_depth_stencil.resize(1);
	_ref_depth_stencil.back() = { attachment_id, layout };
	return *this;
}
RenderPassBuilder::Subpass&
RenderPassBuilder::Subpass::ref_input(uint32_t attachment_id, VkImageLayout layout) {
	_refs_input.push_back({ attachment_id, layout });
	return *this;
}

RenderPassBuilder::Dependency::Dependency(RenderPassBuilder* parent) {
	this->_parent = parent;
}
RenderPassBuilder::Dependency&
RenderPassBuilder::Dependency::src(uint32_t subpass, VkPipelineStageFlags stage, VkAccessFlags access) {
	_dep.srcSubpass = subpass;
	_dep.srcStageMask = stage;
	_dep.srcAccessMask = access;
	return *this;
}
RenderPassBuilder::Dependency&
RenderPassBuilder::Dependency::dst(uint32_t subpass, VkPipelineStageFlags stage, VkAccessFlags access) {
	_dep.dstSubpass = subpass;
	_dep.dstStageMask = stage;
	_dep.dstAccessMask = access;
	return *this;
}
RenderPassBuilder::Dependency&
RenderPassBuilder::Dependency::flags(VkDependencyFlags f) {
	_dep.dependencyFlags = f;
	return *this;
}
RenderPass* RenderPassBuilder::build() {
	RenderPass* render_pass = new RenderPass(*this);
	g_user_render_passes.insert(std::shared_ptr<RenderPass>(render_pass));
	return render_pass;
}
// render pass begin
RenderPassBegin::RenderPassBegin() {
	_info = VkRenderPassBeginInfo{};
	_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	_info.renderPass = VK_NULL_HANDLE;
	_info.framebuffer = VK_NULL_HANDLE;
	_info.renderArea = { {0, 0}, {0, 0} };
	_info.clearValueCount = 0;
}
RenderPassBegin& RenderPassBegin::framebuffer(Framebuffer* fb) {
	_info.framebuffer = fb->vk_framebuffer;
	return *this;
}
RenderPassBegin& RenderPassBegin::clear_depth_stencil(float depth, uint32_t stencil) {
	VkClearValue value{};
	value.depthStencil = { depth, stencil };
	_clear_values.push_back(value);
	return *this;
}
RenderPassBegin& RenderPassBegin::clear_color(float r, float g, float b, float a) {
	VkClearValue value{};
	value.color.float32[0] = r;
	value.color.float32[1] = g;
	value.color.float32[2] = b;
	value.color.float32[3] = a;
	_clear_values.push_back(value);
	return *this;
}
RenderPassBegin& RenderPassBegin::clear_color(int32_t r, int32_t g, int32_t b, int32_t a) {
	VkClearValue value{};
	value.color.int32[0] = r;
	value.color.int32[1] = g;
	value.color.int32[2] = b;
	value.color.int32[3] = a;
	_clear_values.push_back(value);
	return *this;
}
RenderPassBegin& RenderPassBegin::clear_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
	VkClearValue value{};
	value.color.uint32[0] = r;
	value.color.uint32[1] = g;
	value.color.uint32[2] = b;
	value.color.uint32[3] = a;
	_clear_values.push_back(value);
	return *this;
}
RenderPassBegin& RenderPassBegin::area(uint32_t width, uint32_t height, int32_t x, int32_t y) {
	_info.renderArea.extent = { width, height };
	_info.renderArea.offset = { x, y };
	return *this;
}

// rendering begin
RenderingBegin::Attachment::Attachment(RenderingBegin* parent) {
	_parent = parent;
	_info = VkRenderingAttachmentInfo{};
	_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	_info.imageView = VK_NULL_HANDLE;
	_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	_info.resolveMode = VK_RESOLVE_MODE_NONE;
	_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::image_view(VkImageView view) {
	_info.imageView = view;
	return *this;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::image_layout(VkImageLayout layout) {
	_info.imageLayout = layout;
	return *this;
}

RenderingBegin::Attachment&
RenderingBegin::Attachment::load_store(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op) {
	_info.loadOp = load_op;
	_info.storeOp = store_op;
	return *this;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::clear_value(float depth, uint32_t stencil) {
	_info.clearValue.depthStencil = { depth, stencil };
	return *this;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::clear_value(float r, float g, float b, float a) {
	_info.clearValue.color.float32[0] = r;
	_info.clearValue.color.float32[1] = g;
	_info.clearValue.color.float32[2] = b;
	_info.clearValue.color.float32[3] = a;
	return *this;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::clear_value(int32_t r, int32_t g, int32_t b, int32_t a) {
	_info.clearValue.color.int32[0] = r;
	_info.clearValue.color.int32[1] = g;
	_info.clearValue.color.int32[2] = b;
	_info.clearValue.color.int32[3] = a;
	return *this;
}
RenderingBegin::Attachment&
RenderingBegin::Attachment::clear_value(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
	_info.clearValue.color.uint32[0] = r;
	_info.clearValue.color.uint32[1] = g;
	_info.clearValue.color.uint32[2] = b;
	_info.clearValue.color.uint32[3] = a;
	return *this;
}
RenderingBegin& RenderingBegin::Attachment::end() {
	return *_parent;
}
RenderingBegin::RenderingBegin() {
	_info = VkRenderingInfo{};
	_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	_info.renderArea = { {0, 0}, {0, 0} };
	_info.layerCount = 1;
	_info.viewMask = 0;
	_info.colorAttachmentCount = 0;
	_info.pColorAttachments = VK_NULL_HANDLE;
	_info.pDepthAttachment = VK_NULL_HANDLE;
	_info.pStencilAttachment = VK_NULL_HANDLE;

	_depth_stencil_attachment = nullptr;
}
RenderingBegin& RenderingBegin::area(uint32_t width, uint32_t height, int32_t x, int32_t y) {
	_info.renderArea.offset = { x, y };
	_info.renderArea.extent = { width, height };
	return *this;
}
RenderingBegin::Attachment& RenderingBegin::color_attachment() {
	_color_attachments.push_back(Attachment(this));
	return _color_attachments.back();
}
RenderingBegin::Attachment& RenderingBegin::depth_stencil_attachment() {
	_depth_stencil_attachment.reset(new Attachment(this));
	return *_depth_stencil_attachment;
}

// graphics pipeline builder
GraphicsPipelineBuilder::AttachmentBlend::AttachmentBlend(GraphicsPipelineBuilder* parent) {
	this->_parent = parent;
	_attachment_blend = VkPipelineColorBlendAttachmentState{};
	_attachment_blend.blendEnable = VK_TRUE;
	_attachment_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	_attachment_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	_attachment_blend.colorBlendOp = VK_BLEND_OP_ADD;
	_attachment_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	_attachment_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
	_attachment_blend.alphaBlendOp = VK_BLEND_OP_ADD;
	_attachment_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}

GraphicsPipelineBuilder::AttachmentBlend&
GraphicsPipelineBuilder::AttachmentBlend::color_blend(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op) {
	_attachment_blend.srcColorBlendFactor = src;
	_attachment_blend.dstColorBlendFactor = dst;
	_attachment_blend.colorBlendOp = op;
	return *this;
}
GraphicsPipelineBuilder::AttachmentBlend&
GraphicsPipelineBuilder::AttachmentBlend::alpha_blend(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op) {
	_attachment_blend.srcAlphaBlendFactor = src;
	_attachment_blend.dstAlphaBlendFactor = dst;
	_attachment_blend.alphaBlendOp = op;
	return *this;
}
GraphicsPipelineBuilder::AttachmentBlend&
GraphicsPipelineBuilder::AttachmentBlend::color_mask(VkColorComponentFlags color_component) {
	_attachment_blend.colorWriteMask = color_component;
	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::AttachmentBlend::end() {
	return *_parent;
}

GraphicsPipelineBuilder::PipelineRendering::PipelineRendering(GraphicsPipelineBuilder* parent) {
	this->_parent = parent;
	_pipeline_rendering = VkPipelineRenderingCreateInfo{};
	_pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	_pipeline_rendering.viewMask = 0;
	_pipeline_rendering.colorAttachmentCount = 0;
	_pipeline_rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
	_pipeline_rendering.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
}
GraphicsPipelineBuilder::PipelineRendering&
GraphicsPipelineBuilder::PipelineRendering::add_color_attachment_format(VkFormat format) {
	_color_attachment_formats.push_back(format);
	return *this;
}
GraphicsPipelineBuilder::PipelineRendering&
GraphicsPipelineBuilder::PipelineRendering::depth_stencil_attachment_format(VkFormat format) {
	_pipeline_rendering.depthAttachmentFormat = format;
	_pipeline_rendering.stencilAttachmentFormat = format;
	return *this;
}
GraphicsPipelineBuilder::PipelineRendering&
GraphicsPipelineBuilder::PipelineRendering::add_color_attachment_format(otcv::Image* image) {
	_color_attachment_formats.push_back(image->builder._image_info.format);
	return *this;
}
GraphicsPipelineBuilder::PipelineRendering&
GraphicsPipelineBuilder::PipelineRendering::depth_stencil_attachment_format(otcv::Image* image) {
	_pipeline_rendering.depthAttachmentFormat = image->builder._image_info.format;
	_pipeline_rendering.stencilAttachmentFormat = image->builder._image_info.format;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::PipelineRendering::end() {
	return *_parent;
}

GraphicsPipelineBuilder::GraphicsPipelineBuilder() {
	_vertex_shader = nullptr;
	_fragment_shader = nullptr;

	_vertex_state = VkPipelineVertexInputStateCreateInfo{};
	_vertex_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	_vertex_state.vertexBindingDescriptionCount = 0;
	_vertex_state.vertexAttributeDescriptionCount = 0;

	_assembly_state = VkPipelineInputAssemblyStateCreateInfo{};
	_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	_assembly_state.primitiveRestartEnable = VK_FALSE;

	_viewport = VkViewport{};
	_viewport.x = 0.0f;
	_viewport.y = 0.0f;
	_viewport.width = 0.0f;
	_viewport.height = 0.0f;
	_viewport.minDepth = 0.0f;
	_viewport.maxDepth = 1.0f;

	_scissor = VkRect2D{};
	_scissor.offset = VkOffset2D{ 0, 0 };
	_scissor.extent = VkExtent2D{ 0, 0 };

	_viewport_state = VkPipelineViewportStateCreateInfo{};
	_viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	_viewport_state.viewportCount = 1;
	_viewport_state.pViewports = &_viewport;
	_viewport_state.scissorCount = 1;
	_viewport_state.pScissors = &_scissor;

	_rast_state = VkPipelineRasterizationStateCreateInfo{};
	_rast_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	_rast_state.depthClampEnable = VK_FALSE;
	_rast_state.rasterizerDiscardEnable = VK_FALSE;
	_rast_state.polygonMode = VK_POLYGON_MODE_FILL;
	_rast_state.cullMode = VK_CULL_MODE_NONE;
	_rast_state.depthBiasEnable = VK_FALSE;
	_rast_state.lineWidth = 1.0f;

	_ms_state = VkPipelineMultisampleStateCreateInfo{};
	_ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	_ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	_ms_state.sampleShadingEnable = VK_FALSE;

	_depth_stencil_state = VkPipelineDepthStencilStateCreateInfo{};
	_depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	_depth_stencil_state.depthTestEnable = VK_FALSE;
	_depth_stencil_state.depthWriteEnable = VK_FALSE;
	_depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
	_depth_stencil_state.stencilTestEnable = VK_FALSE;

	_blend_state = VkPipelineColorBlendStateCreateInfo{};
	_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	_blend_state.logicOpEnable = VK_FALSE;
	_blend_state.attachmentCount = 0;

	_dynamic_state = VkPipelineDynamicStateCreateInfo{};
	_dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	_dynamic_state.dynamicStateCount = 0;

	_render_pass = nullptr;
	_subpass = -1;

	_pipeline_rendering = nullptr;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::render_pass(RenderPass* render_pass, uint32_t subpass) {
	_render_pass = render_pass;
	_subpass = subpass;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::shader_vertex(ShaderModule* vs) {
	this->_vertex_shader = vs;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::shader_fragment(ShaderModule* fs) {
	this->_fragment_shader = fs;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::vertex_state(const VertexBufferBuilder& vbb) {
	_vertex_bindings = vbb._binding_descs;
	_vertex_attributes = vbb._attr_descs;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::topology(VkPrimitiveTopology topo) {
	_assembly_state.topology = topo;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::viewport(float width, float height, float x, float y, float min_depth, float max_depth) {
	_viewport.x = x;
	_viewport.y = y;
	_viewport.width = width;
	_viewport.height = height;
	_viewport.minDepth = min_depth;
	_viewport.maxDepth = max_depth;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::scissor(VkExtent2D extent, VkOffset2D offset) {
	_scissor.offset = offset;
	_scissor.extent = extent;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::polygon_mode(VkPolygonMode mode) {
	_rast_state.polygonMode = mode;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::cull_back_face(VkFrontFace front_face) {
	_rast_state.cullMode = VK_CULL_MODE_BACK_BIT;
	_rast_state.frontFace = front_face;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::depth_bias(float slope, float constant, float clamp) {
	_rast_state.depthBiasEnable = VK_TRUE;
	_rast_state.depthBiasSlopeFactor = slope;
	_rast_state.depthBiasConstantFactor = constant;
	_rast_state.depthBiasClamp = clamp;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::line_width(float width) {
	_rast_state.lineWidth = width;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::depth_test(bool enable_test, bool enable_write, VkCompareOp comp_op) {
	_depth_stencil_state.depthTestEnable = enable_test;
	_depth_stencil_state.depthWriteEnable = enable_write;
	_depth_stencil_state.depthCompareOp = comp_op;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::blend_logic_op(VkLogicOp op) {
	_blend_state.logicOpEnable = VK_TRUE;
	_blend_state.logicOp = op;
	return *this;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::blend_constants(float r, float g, float b, float a) {
	_blend_state.blendConstants[0] = r;
	_blend_state.blendConstants[1] = g;
	_blend_state.blendConstants[2] = b;
	_blend_state.blendConstants[3] = a;
	return *this;
}

GraphicsPipelineBuilder::AttachmentBlend&
GraphicsPipelineBuilder::blend_attachment(uint32_t n) {
	auto insert_result =  _attachment_blend_states_map.insert({ n, AttachmentBlend(this) });
	if (insert_result.second) {
		insert_result.first->second = AttachmentBlend(this);
	}
	return insert_result.first->second;
}
GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_dynamic_state(VkDynamicState dyn_state) {
	_dynamic_states.push_back(dyn_state);
	return *this;
}
GraphicsPipelineBuilder::PipelineRendering& GraphicsPipelineBuilder::pipline_rendering() {
	_pipeline_rendering.reset(new PipelineRendering(this));
	return *_pipeline_rendering;
}
GraphicsPipeline* GraphicsPipelineBuilder::build() {
	GraphicsPipeline* pipeline = new GraphicsPipeline(*this);
	g_user_graphics_pipelines.insert(std::shared_ptr<GraphicsPipeline>(pipeline));
	return pipeline;
}

// descriptor pool
DescriptorPoolBuilder::DescriptorPoolBuilder() {
	_info = VkDescriptorPoolCreateInfo{};
	_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	_info.maxSets = 0;
	_info.poolSizeCount = 0;
}

DescriptorPoolBuilder& DescriptorPoolBuilder::descriptor_type_capacity(VkDescriptorType type, uint32_t count) {
	_pool_sizes.push_back({ type, count });
	return *this;
}
DescriptorPoolBuilder& DescriptorPoolBuilder::descriptor_set_capacity(uint32_t count) {
	_info.maxSets = count;
	return *this;
}
DescriptorPoolBuilder& DescriptorPoolBuilder::descriptor_set_freeable(bool freeable) {
	if (freeable) {
		_info.flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	}
	else {
		_info.flags &= ~VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	}
	return *this;
}
DescriptorPoolBuilder& DescriptorPoolBuilder::bindless() {
	// _info.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT; // for my use case this is not necessary
	return *this;
}
DescriptorPool* DescriptorPoolBuilder::build() {
	DescriptorPool* pool = new DescriptorPool(*this);
	g_user_descriptor_pools.insert(std::shared_ptr<DescriptorPool>(pool));
	return pool;
}

// framebuffer builder
FramebufferBuilder::FramebufferBuilder() {
	this->_info = VkFramebufferCreateInfo{};
	this->_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	this->_info.renderPass = VK_NULL_HANDLE;
	this->_info.attachmentCount = 0;
	this->_info.pAttachments = nullptr;
	this->_info.width = 0;
	this->_info.height = 0;
	this->_info.layers = 0;
}
FramebufferBuilder& FramebufferBuilder::render_pass(RenderPass* render_pass) {
	this->_info.renderPass = render_pass->vk_render_pass;
	return *this;
}
FramebufferBuilder& FramebufferBuilder::size(uint32_t width, uint32_t height, uint32_t layers) {
	this->_info.width = width;
	this->_info.height = height;
	this->_info.layers = layers;
	return *this;
}
FramebufferBuilder& FramebufferBuilder::add_attachment(Image* image) {
	this->_attachments.push_back(image->vk_view);
	return *this;
}
Framebuffer* FramebufferBuilder::build() {
	Framebuffer* buffer = new Framebuffer(*this);
	g_user_framebuffers.insert(std::shared_ptr<Framebuffer>(buffer));
	return buffer;
}
}