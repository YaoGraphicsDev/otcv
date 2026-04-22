#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <string>
#include <functional>

namespace otcv {
/* 
* Boilerplate stuff
*/
// resources that requires explicit desctruction

// Note: fields of pointer types in create info are usually wild pointers. Set them to nullptr after initialization
struct Instance {
	Instance(VkInstanceCreateInfo&);
	~Instance();
	VkInstance vk_instance = VK_NULL_HANDLE;
	VkInstanceCreateInfo info = {};
};
struct Surface {
	Surface(void*);
	~Surface();
	VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
	void* window = nullptr;
};
struct Device {
	Device(VkDeviceCreateInfo&);
	~Device();
	VkDevice vk_device = VK_NULL_HANDLE;
	VkDeviceCreateInfo info = {};
};

// PhysicalDevice and Queue instances are not created by allocation and do not require explicit destruction
struct PhysicalDevice {
	VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
	uint32_t queue_family_index = 0;
};

// semaphore
struct Semaphore {
	Semaphore(VkSemaphoreCreateInfo&);
	~Semaphore();

	static Semaphore* create();
	void destroy();

	VkSemaphoreCreateInfo info;
	VkSemaphore vk_semaphore = VK_NULL_HANDLE;
};

// fence
struct Fence {
	Fence(VkFenceCreateInfo&);
	~Fence();

	static Fence* create(bool signaled = true);
	void destroy();

	void wait_reset();
	void wait();
	void reset();

	bool is_signaled();

	VkFenceCreateInfo info;
	VkFence vk_fence = VK_NULL_HANDLE;
};

enum class ResourceState {
	Null,
	Created,

	HostRead,  
	HostWrite, // This is a state that occurs after vkFlushMappedMemoryRanges 

	TransferSrc,
	TransferDst,

	ComputeSSBORead,
	ComputeSSBOWrite,
	ComputeSSBO, // Read & Write

	ComputeStorageRead,
	ComputeStorageWrite,
	ComputeStorage, // Read & Write

	IndirectRead,

	FragSample,
	ComputeSample,

	ColorAttachment,
	DepthStencilAttachment, // Test & Write, might need to differentiate these 2 operations in the future
	// check out SaschaWillems' shadowmapping example for details. subpass dependency

	PresentAvailableForTransferDst,
	PresentReady,

	VertexRead,
	IndexRead,
};
struct Image;
struct Swapchain {
	Swapchain(VkSwapchainCreateInfoKHR);
	~Swapchain();
	
	Image* mock_image(uint32_t id);

	VkSwapchainKHR vk_swapchain;
	std::vector<VkImage> images{};
	std::vector<VkImageView> views{};

	VkSwapchainCreateInfoKHR swapchain_info{};
	VkImageCreateInfo image_info{};
	
	std::vector<Image*> mock_images{};
};

// command pool and buffer
struct Image;
struct Buffer;
struct RenderPass;
struct RenderPassBegin;
struct RenderingBegin;
struct GraphicsPipeline;
struct ComputePipeline;
struct VertexBuffer;
struct DescriptorSet;
struct ImageBlit;
struct ImageCopy;
struct CommandBuffer {
	CommandBuffer(VkCommandBufferAllocateInfo&);
	~CommandBuffer();

	void begin(bool one_time = false);
	void end();
	void reset(bool release = false);
	void record(std::function<void(CommandBuffer*)> func, bool one_time = false);

	// graphics pipeline commands
	void cmd_begin_render_pass(RenderPass* pass, RenderPassBegin& begin);
	void cmd_end_render_pass(RenderPass* pass);

	// dynamic rendering
	void cmd_begin_rendering(RenderingBegin& begin);
	void cmd_end_rendering();

	void cmd_bind_graphics_pipeline(GraphicsPipeline* pipeline);

	void cmd_bind_vertex_buffer(VertexBuffer* vb, std::vector<VkDeviceSize> offsets = {});
	void cmd_bind_raw_buffer_as_vertex(Buffer* buf, uint32_t binding, std::vector<VkDeviceSize> offset = { 0 });
	void cmd_bind_index_buffer(Buffer* ib, VkIndexType type, VkDeviceSize offset = 0);

	void cmd_set_viewport(float width, float height,
		float x = 0.0f, float y = 0.0f,
		float min_depth = 0.0f, float max_depth = 1.0f);
	void cmd_set_scissor(float width, float height,
		float x = 0.0f, float y = 0.0f);

	void cmd_push_constant(GraphicsPipeline* pipeline, const std::string& name, const void* data);
	void cmd_bind_descriptor_set(GraphicsPipeline* pipeline, DescriptorSet* set, uint32_t target_set = 0, std::vector<uint32_t> dynamic_offsets = {});
	void cmd_bind_descriptor_set(ComputePipeline* pipeline, DescriptorSet* set, uint32_t target_set = 0, std::vector<uint32_t> dynamic_offsets = {});
	void cmd_draw(uint32_t vertex_count,
		uint32_t instance_count = 1,
		uint32_t first_vertex = 0,
		uint32_t first_instance = 0);
	void cmd_draw_indexed(uint32_t index_count,
		uint32_t first_index = 0,
		int32_t vertex_offset = 0,
		uint32_t instance_count = 1,
		uint32_t first_instance = 0);
	void cmd_draw_indexed_indirect_count(Buffer* commands,
		VkDeviceSize commands_offset,
		Buffer* counts,
		VkDeviceSize counts_offset,
		uint32_t max_draw,
		uint32_t commands_stride);

	// compute pipeline commands
	void cmd_bind_compute_pipeline(ComputePipeline* pipeline);
	void cmd_push_constant(ComputePipeline* pipeline, const std::string& name, const void* data);
	void cmd_dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z = 1);

	// blit & copy commands
	void cmd_copy_buffer(Buffer* src, Buffer* dst);

	// memory barrier commands
	// does not allow setting barriers to individual array layers. May lose synchronization granularity
	void cmd_image_memory_barrier(Image* image, ResourceState from_state, ResourceState to_state, VkImageSubresourceRange sub_range = { 0, 0, 1, 0, 1 });
	void cmd_buffer_memory_barrier(Buffer* buffer, ResourceState from_state, ResourceState to_state);

	// blit
	void cmd_image_blit(Image* src, Image* dst, ImageBlit& region, VkFilter filter = VK_FILTER_NEAREST);
	
	void cmd_image_copy(Image* src, Image* dst, ImageCopy& region);

	void cmd_fill_buffer(Buffer* dst_buffer, uint32_t data, VkDeviceSize dst_offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

	VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo alloc_info = {};
	VkCommandBufferBeginInfo begin_info = {};
};
struct CommandPool {
	CommandPool(bool transient, bool allow_individual_reset);
	~CommandPool();
	static CommandPool* create(bool transient, bool allow_individual_reset, bool user = true);
	void destroy();

	CommandBuffer* allocate();
	void reset(bool release = false);
	void free(CommandBuffer*);

	VkCommandPool vk_command_pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo info = {};
};

// command queue
struct QueueSubmit {
	struct Batch {
		Batch(QueueSubmit* parent);
		Batch& add_command_buffer(CommandBuffer* cmd_buffer);
		Batch& add_wait(Semaphore* semaphore, VkPipelineStageFlags wait_stage);
		Batch& add_signal(Semaphore* semaphore);
		QueueSubmit& end() { return *_parent; }

		QueueSubmit* _parent = nullptr;
		std::vector<VkSemaphore> _wait_semaphores;
		std::vector<VkPipelineStageFlags> _wait_stages;
		std::vector<VkSemaphore> _signal_semaphores;
		std::vector<VkCommandBuffer> _cmd_buffers;
	};
	Batch& batch();
	QueueSubmit& signal(Fence* fence);

	std::vector<Batch> _batches;
	Fence* _fence = nullptr;
};
struct QueuePresent {
	QueuePresent& image_index(uint32_t index);
	QueuePresent& add_wait(Semaphore* semaphore);

	uint32_t _image_index;
	std::vector<VkSemaphore> _wait_semaphores;
};
struct Queue {
	VkQueue vk_queue = VK_NULL_HANDLE;
	void submit(QueueSubmit& info);
	void present(QueuePresent& info);
	void idle_wait();
};

// shader module
struct ShaderModule;
struct ShaderModuleBuilder {
	struct Uniform {
		Uniform();
		Uniform(ShaderModuleBuilder* parent);
		Uniform& type(VkDescriptorType type);
		Uniform& name(const std::string& name);
		Uniform& size(size_t size);
		Uniform& field(const std::string& member, uint32_t offset);
		Uniform& array_count(uint32_t n);
		ShaderModuleBuilder& end();

		ShaderModuleBuilder* _parent = nullptr;
		std::string _name = "";
		VkDescriptorType _type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uint32_t _array_count = 1;
		size_t _size; // only relevant when building an UBO
		std::map<std::string, VkDeviceSize> _field_offset_map; // only relevant when building an UBO
	};
	ShaderModuleBuilder& name(const std::string& name);
	ShaderModuleBuilder& spirv_binary(const uint32_t* data, size_t byte_size);
	// ShaderModuleBuilder& spirv_reflect_path(const std::string& path);
	Uniform& uniform(uint16_t set, uint16_t binding);
	ShaderModuleBuilder& add_push_constant(const std::string& member_name, uint16_t offset, uint16_t size);
	
	ShaderModule* build();

	std::string _name;
	const uint32_t* _spirv_data = nullptr;
	size_t _spirv_byte_size = 0;
	std::map<uint32_t, Uniform> _uniforms;
	std::map<std::string, uint32_t> _push_constants;
	// uint32_t _push_constant_offset_size = 0;
};
struct ShaderModule {
	ShaderModule(ShaderModuleBuilder& builder, const char* spirv_code, size_t byte_size);
	~ShaderModule();
	void destroy();

	ShaderModuleBuilder builder;
	VkShaderModule vk_shader;
};

// Descriptor pool
struct Image;
struct Sampler;
struct Buffer;
struct DescriptorSet {
	DescriptorSet(VkDescriptorSet vk_desc_set, VkDescriptorSetAllocateInfo alloc_info,
		const std::vector<VkDescriptorSetLayoutBinding>& bindings,
		bool free_required);
	~DescriptorSet();
	
	// TODO: bindings in a descriptor set no longer continuous
	void bind_image_sampler(uint32_t binding, Image** p_images, Sampler** p_samplers, uint32_t array_start = 0, uint32_t array_count = 1);
	void bind_storage_image(uint32_t binding, Image** p_images, uint32_t array_start = 0, uint32_t array_count = 1);
	void bind_sampler(uint32_t binding, Sampler** p_samplers, uint32_t array_start = 0, uint32_t array_count = 1);
	void bind_sampled_image(uint32_t binding, Image** p_images, uint32_t array_start = 0, uint32_t array_count = 1);
	void bind_buffer(uint32_t binding, Buffer* buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE);
	void bind_buffer_array(uint32_t binding, Buffer* buffer, VkDeviceSize offset, VkDeviceSize stride, uint32_t count);

	void bind_consecutive_buffers(uint32_t binding_start, uint32_t binding_count, Buffer** p_buffers);
	void bind_consecutive_sampled_images(uint32_t binding_start, uint32_t binding_count, Image** p_images);
	void bind_consecutive_storage_images(uint32_t binding_start, uint32_t binding_count, Image** p_images);

	VkDescriptorSet vk_desc_set = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo alloc_info = {};
	std::vector<VkDescriptorSetLayoutBinding> bindings = {};
	bool free_required;
};
struct DescriptorPool;
struct DescriptorPoolBuilder {
	DescriptorPoolBuilder();

	DescriptorPoolBuilder& descriptor_type_capacity(VkDescriptorType type, uint32_t count);
	DescriptorPoolBuilder& descriptor_set_capacity(uint32_t count);
	DescriptorPoolBuilder& descriptor_set_freeable(bool freeable = true);
	DescriptorPoolBuilder& bindless();
	DescriptorPool* build();

	std::vector<VkDescriptorPoolSize> _pool_sizes;
	VkDescriptorPoolCreateInfo _info;
};
struct DescriptorSetLayout;
struct DescriptorPool {
	DescriptorPool(DescriptorPoolBuilder& builder);
	~DescriptorPool();

	void destroy();

	void reset();
	DescriptorSet* allocate(DescriptorSetLayout* set_layout, std::function<void()> oom_callback = nullptr);
	void free(DescriptorSet* set);

	VkDescriptorPool vk_desc_pool = VK_NULL_HANDLE;
	std::set<DescriptorSet*> desc_sets = {};
	DescriptorPoolBuilder builder = {};
};

// image
// optimal tiling & device local memory only
struct Image;
struct ImageBuilder {
	ImageBuilder();
	ImageBuilder& name(const std::string& name);
	ImageBuilder& image_type(VkImageType type);
	ImageBuilder& format(VkFormat format);
	ImageBuilder& size(uint32_t width, uint32_t height, uint32_t depth);
	// 0 -- maximum levels of mip
	ImageBuilder& enable_mips(bool enable = true);
	ImageBuilder& layers(uint32_t n);
	ImageBuilder& samples(uint32_t n);
	ImageBuilder& usage(VkImageUsageFlags usage);
	// Vulkan strictly limits the initial layout of an image to undefined. This function is not that useful
	ImageBuilder& initial_layout(VkImageLayout layout);
	ImageBuilder& view_type(VkImageViewType type);
	ImageBuilder& aspect(VkImageAspectFlags aspect);
	ImageBuilder& swizzle(VkComponentSwizzle r, VkComponentSwizzle g, VkComponentSwizzle b, VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_A);
	Image* build();

	bool _has_mips = false;
	std::string _name = ""; // to mark different kinds of textures
	VkImageCreateInfo _image_info = {};
	VkImageViewCreateInfo _view_info = {};
};
struct Image {
	Image(ImageBuilder& builder);
	~Image();
	void destroy();

	enum class SyncType {
		CPUWait,
		GPUBarrier
	};
	// images that does not require explicit cpu wait are expected to go into render/compute pipelines later,
	// synchronized on the GPU side with image memory barrier
	void populate_async(void* data, size_t byte_size, 
		ResourceState target_state, ResourceState current_state = ResourceState::Created, SyncType sync_type = SyncType::CPUWait);

	void initialize_state_async(ResourceState target_state, ResourceState current_state = ResourceState::Created, SyncType sync_type = SyncType::CPUWait);

	void wait_for_async();

	// sync version
	void populate(void* data, size_t byte_size,
		ResourceState target_state, ResourceState current_state = ResourceState::Created);

	void initialize_state(ResourceState target_state, ResourceState current_state = ResourceState::Created);

	VkImageView view_of_subresource(const VkImageSubresourceRange& range);

	ImageBuilder builder;
	VkImage vk_image = VK_NULL_HANDLE;
	VkDeviceMemory vk_memory = VK_NULL_HANDLE;
	// main view
	VkImageView vk_view = VK_NULL_HANDLE;
	// view of layers
	// packed index start -- count
	std::map<uint32_t, VkImageView> vk_subresource_views;

	struct AsyncPopulateCtx {
		otcv::Fence* fence;
		otcv::CommandBuffer* command_buffer;
		otcv::Buffer* staging;
	};
	std::shared_ptr<AsyncPopulateCtx> async_ctx = nullptr;
};

struct Sampler;
struct SamplerBuilder {
	SamplerBuilder();
	SamplerBuilder& filter(VkFilter min, VkFilter mag);
	SamplerBuilder& mipmap(VkSamplerMipmapMode mode);
	SamplerBuilder& address_mode(VkSamplerAddressMode mode, VkBorderColor color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
	SamplerBuilder& address_mode_u(VkSamplerAddressMode mode, VkBorderColor color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
	SamplerBuilder& address_mode_v(VkSamplerAddressMode mode, VkBorderColor color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
	SamplerBuilder& address_mode_w(VkSamplerAddressMode mode, VkBorderColor color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
	SamplerBuilder& lod(float min, float max, float mip_bias = 0.0f);
	SamplerBuilder& compare(VkCompareOp op);
	Sampler* build();

	VkSamplerCreateInfo _info = {};
};
struct Sampler {
	Sampler(SamplerBuilder& builder);
	~Sampler();
	void destroy();

	VkSampler vk_sampler = VK_NULL_HANDLE;
	SamplerBuilder builder = {};
};

// buffer
struct Buffer;
struct BufferBuilder {
	enum class Access {
		Invisible,
		Coherent,
		Incoherent,
	};
	BufferBuilder();
	BufferBuilder& size(VkDeviceSize size);
	BufferBuilder& usage(VkBufferUsageFlags usage);
	BufferBuilder& host_access(Access access);
	Buffer* build();

	VkBufferCreateInfo _info{};
	VkMemoryPropertyFlags _mem_props = 0;
};
struct Buffer {
	Buffer(BufferBuilder& builder);
	~Buffer();
	void destroy();

	enum class SyncType {
		CPUWait,
		GPUBarrier
	};
	void populate_async(const void* data, SyncType sync_type = SyncType::CPUWait, ResourceState target_state = ResourceState::Null, ResourceState current_state = ResourceState::Null);

	void wait_for_async();

	// sync version
	void populate(const void* data);

	Buffer* copy_host_mapped(const void* data, uint32_t offset, uint32_t size);

	void flush();

	BufferBuilder builder;
	VkBuffer vk_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vk_memory = VK_NULL_HANDLE;
	void* mapped = nullptr;

	struct AsyncPopulateCtx {
		otcv::Fence* fence;
		otcv::CommandBuffer* command_buffer;
		otcv::Buffer* staging;
	};
	std::shared_ptr<AsyncPopulateCtx> async_ctx = nullptr;
};

// Vertex Buffer
struct VertexBuffer;
struct VertexBufferBuilder {
	VertexBufferBuilder& add_binding(BufferBuilder b_builder, const void* data = nullptr);
	VertexBufferBuilder& add_binding(); // for constructing binding description only. Do not build after call
	VertexBufferBuilder& add_attribute(uint32_t binding, VkFormat format, uint32_t byte_size);
	VertexBufferBuilder& add_attribute_padding(uint32_t binding, uint32_t byte_size);
	VertexBuffer* build();

	std::vector<VkVertexInputBindingDescription> _binding_descs;
	std::vector<VkVertexInputAttributeDescription> _attr_descs;
	std::vector<BufferBuilder> _buffer_builders;
	std::vector<const void*> _data_handles;
	bool _buildable = true;
};
struct VertexBuffer {
	enum class BufferDataUpload {
		Dont,
		Sync,
		AsyncCPUWait,
		AsyncGPUBarrier
	};

	VertexBuffer(VertexBufferBuilder& builder, BufferDataUpload upload_option = BufferDataUpload::Sync);
	~VertexBuffer();
	void destroy();

	void resize(uint32_t binding, size_t size);

	void wait_for_async_upload();

	VertexBufferBuilder builder;
	std::vector<Buffer*> buffers;
};

// render pass
struct RenderPass;
struct RenderPassBuilder {
	struct Attachment {
		Attachment(RenderPassBuilder* parent);
		Attachment& format_samples(VkFormat format, uint32_t samples = 1);
		Attachment& layouts(VkImageLayout initial, VkImageLayout final);
		Attachment& load_store(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op,
			VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE);
		RenderPassBuilder& end() { return *_parent; }

		RenderPassBuilder* _parent = nullptr;
		VkAttachmentDescription _desc = {};
	};

	struct Subpass {
		Subpass(RenderPassBuilder* parent);
		Subpass& ref_color(uint32_t attachment_id, VkImageLayout layout);
		Subpass& ref_depth_stencil(uint32_t attachment_id, VkImageLayout layout);
		Subpass& ref_input(uint32_t attachment_id, VkImageLayout layout);
		RenderPassBuilder& end() { return *_parent; }

		RenderPassBuilder* _parent = nullptr;
		std::vector<VkAttachmentReference> _refs_color = {};
		std::vector<VkAttachmentReference> _refs_input = {};
		std::vector<VkAttachmentReference> _ref_depth_stencil = {};
	};

	struct Dependency {
		Dependency(RenderPassBuilder* parent);
		Dependency& src(uint32_t subpass, VkPipelineStageFlags stage, VkAccessFlags access);
		Dependency& dst(uint32_t subpass, VkPipelineStageFlags stage, VkAccessFlags access);
		Dependency& flags(VkDependencyFlags f);
		RenderPassBuilder& end() { return *_parent; }

		RenderPassBuilder* _parent = nullptr;
		VkSubpassDependency _dep = {};
	};

	Attachment& attachment();
	Subpass& subpass();
	Dependency& dependencies();
	RenderPass* build();

	std::vector<Attachment> _attachments;
	std::vector<Subpass> _subpasses;
	std::vector<Dependency> _dependencies;
};
struct Framebuffer;
struct RenderPassBegin {
	RenderPassBegin();
	RenderPassBegin& framebuffer(Framebuffer* fb);
	RenderPassBegin& clear_depth_stencil(float depth, uint32_t stencil = 0);
	RenderPassBegin& clear_color(float r, float g, float b, float a);
	RenderPassBegin& clear_color(int32_t r, int32_t g, int32_t b, int32_t a);
	RenderPassBegin& clear_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
	RenderPassBegin& area(uint32_t width, uint32_t height, int32_t x = 0, int32_t y = 0);

	VkRenderPassBeginInfo _info;
	std::vector<VkClearValue> _clear_values;
};
// dynamic rendering
struct RenderingBegin {
	struct Attachment {
		Attachment(RenderingBegin* parent);
		Attachment& image_view(VkImageView view);
		Attachment& image_layout(VkImageLayout layout);
		Attachment& load_store(VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op);
		Attachment& clear_value(float depth, uint32_t stencil = 0);
		Attachment& clear_value(float r, float g, float b, float a);
		Attachment& clear_value(int32_t r, int32_t g, int32_t b, int32_t a);
		Attachment& clear_value(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
		RenderingBegin& end();

		RenderingBegin* _parent = nullptr;
		VkRenderingAttachmentInfo _info;
	};

	RenderingBegin();
	RenderingBegin& area(uint32_t width, uint32_t height, int32_t x = 0, int32_t y = 0);
	Attachment& color_attachment();
	Attachment& depth_stencil_attachment();

	std::vector<Attachment> _color_attachments;
	std::shared_ptr<Attachment> _depth_stencil_attachment;
	VkRenderingInfo _info;
};
struct RenderPass {
	RenderPass(RenderPassBuilder& builder);
	~RenderPass();
	void destroy();

	VkRenderPass vk_render_pass;
	RenderPassBuilder builder;
	RenderPassBegin begin;
};

// layouts
struct DescriptorSetLayout {
	DescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);
	~DescriptorSetLayout();

	VkDescriptorSetLayout vk_desc_set_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayoutCreateInfo create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	std::vector<VkDescriptorSetLayoutBinding> bindings = {};
};
struct PipelineLayout {
	PipelineLayout(
		const std::vector<DescriptorSetLayout*>& set_layouts,
		const std::map<std::string, VkPushConstantRange>& push_const_members);
	~PipelineLayout();

	VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	std::map<std::string, VkPushConstantRange> push_consts = {};
	// std::vector<VkPushConstantRange> push_const_ranges = {};
};

// Graphics Pipeline
struct GraphicsPipeline;
struct GraphicsPipelineBuilder {
	struct AttachmentBlend {
		AttachmentBlend(GraphicsPipelineBuilder* parent);
		AttachmentBlend& color_blend(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op);
		AttachmentBlend& alpha_blend(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op);
		AttachmentBlend& color_mask(VkColorComponentFlags color_component);
		GraphicsPipelineBuilder& end();

		GraphicsPipelineBuilder* _parent = nullptr;
		VkPipelineColorBlendAttachmentState _attachment_blend;
	};
	struct PipelineRendering {
		PipelineRendering(GraphicsPipelineBuilder* parent);
		PipelineRendering& add_color_attachment_format(VkFormat format);
		PipelineRendering& depth_stencil_attachment_format(VkFormat);
		PipelineRendering& add_color_attachment_format(otcv::Image* image);
		PipelineRendering& depth_stencil_attachment_format(otcv::Image* image);
		GraphicsPipelineBuilder& end();

		GraphicsPipelineBuilder* _parent = nullptr;
		std::vector<VkFormat> _color_attachment_formats;
		VkPipelineRenderingCreateInfo _pipeline_rendering;
	};

	GraphicsPipelineBuilder();
	GraphicsPipelineBuilder& render_pass(RenderPass* render_pass, uint32_t subpass = 0);
	// VkPipelineShaderStageCreateInfo
	GraphicsPipelineBuilder& shader_vertex(ShaderModule* vs);
	GraphicsPipelineBuilder& shader_fragment(ShaderModule* fs);
	// VkPipelineVertexInputStateCreateInfo
	GraphicsPipelineBuilder& vertex_state(const VertexBufferBuilder& vbb);
	// VkPipelineInputAssemblyStateCreateInfo
	GraphicsPipelineBuilder& topology(VkPrimitiveTopology topo);
	// VkPipelineViewportStateCreateInfo, does not support multiViewport
	GraphicsPipelineBuilder& viewport(float width, float height,
		float x = 0.0f, float y = 0.0f, float min_depth = 0.0f, float max_depth = 1.0f);
	GraphicsPipelineBuilder& scissor(VkExtent2D extent, VkOffset2D offset = { 0, 0 });
	// VkPipelineRasterizationStateCreateInfo
	GraphicsPipelineBuilder& polygon_mode(VkPolygonMode mode);
	GraphicsPipelineBuilder& cull_back_face(VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE);
	GraphicsPipelineBuilder& depth_bias(float slope, float constant, float clamp = 0.0f);
	GraphicsPipelineBuilder& line_width(float width);
	// VkPipelineDepthStencilStateCreateInfo
	GraphicsPipelineBuilder& depth_test(bool enable_test = true, bool enable_write = true, VkCompareOp comp_op = VK_COMPARE_OP_LESS);
	// VkPipelineColorBlendAttachmentState
	GraphicsPipelineBuilder& blend_logic_op(VkLogicOp op);
	GraphicsPipelineBuilder& blend_constants(float r, float g, float b, float a);
	AttachmentBlend& blend_attachment(uint32_t n);
	// dynamic state, has nothing to do with dynamic rendering
	GraphicsPipelineBuilder& add_dynamic_state(VkDynamicState dyn_state);
	// dynamic rendering, optional
	PipelineRendering& pipline_rendering();

	GraphicsPipeline* build();

	ShaderModule* _vertex_shader;
	ShaderModule* _fragment_shader;
	VkPipelineVertexInputStateCreateInfo _vertex_state;
	std::vector<VkVertexInputBindingDescription> _vertex_bindings;
	std::vector<VkVertexInputAttributeDescription> _vertex_attributes;
	VkPipelineInputAssemblyStateCreateInfo _assembly_state;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineViewportStateCreateInfo _viewport_state;
	VkPipelineRasterizationStateCreateInfo _rast_state;
	VkPipelineMultisampleStateCreateInfo _ms_state;
	VkPipelineDepthStencilStateCreateInfo _depth_stencil_state;
	VkPipelineColorBlendStateCreateInfo _blend_state;
	std::map<uint32_t, AttachmentBlend> _attachment_blend_states_map;
	VkPipelineDynamicStateCreateInfo _dynamic_state;
	std::vector<VkDynamicState> _dynamic_states;
	RenderPass* _render_pass;
	uint32_t _subpass;
	std::shared_ptr<PipelineRendering> _pipeline_rendering; // dynamic rendering
};
struct GraphicsPipeline {
	GraphicsPipeline(GraphicsPipelineBuilder& builder);
	~GraphicsPipeline();
	void destroy();

	void cmd_bind(CommandBuffer* cmd_buffer);
	// void cmd_bind_descriptor_set(CommandBuffer* cmd_buffer, DescriptorSet* set);
	// void cmd_push_constant(CommandBuffer* cmd_buffer, const void* data, VkShaderStageFlags stage);
	VkPipeline vk_pipeline;
	std::vector<DescriptorSetLayout*> desc_set_layouts;
	PipelineLayout* pipeline_layout;
	GraphicsPipelineBuilder builder;
};

// compute pipeline
struct ComputePipeline {
	ComputePipeline(ShaderModule* compute_shader);
	~ComputePipeline();

	static ComputePipeline* create(ShaderModule* compute_shader);
	void destroy();

	VkPipeline vk_pipeline;
	ShaderModule* compute_shader;
	std::vector<DescriptorSetLayout*> desc_set_layouts;
	PipelineLayout* pipeline_layout;
	VkComputePipelineCreateInfo info;
};

// blit
struct ImageBlit {
	ImageBlit();
	ImageBlit& src_upper_bound(int32_t x, int32_t y, int32_t z = 1);
	ImageBlit& src_lower_bound(int32_t x, int32_t y, int32_t z = 0);
	ImageBlit& src_aspect(VkImageAspectFlags aspect);
	ImageBlit& src_mip(uint32_t mip);

	ImageBlit& dst_upper_bound(int32_t x, int32_t y, int32_t z = 1);
	ImageBlit& dst_lower_bound(int32_t x, int32_t y, int32_t z = 0);
	ImageBlit& dst_aspect(VkImageAspectFlags aspect);
	ImageBlit& dst_mip(uint32_t mip);

	VkImageBlit _image_blit = {};
};

// copy
struct ImageCopy {
	ImageCopy();
	ImageCopy& extent(uint32_t width, uint32_t height, uint32_t depth = 1);

	ImageCopy& src_offset(int32_t x, int32_t y, int32_t z = 0);
	ImageCopy& dst_offset(int32_t x, int32_t y, int32_t z = 0);

	ImageCopy& src_aspect(VkImageAspectFlags aspect);
	ImageCopy& src_mip(uint32_t mip);

	ImageCopy& dst_aspect(VkImageAspectFlags aspect);
	ImageCopy& dst_mip(uint32_t mip);

	VkImageCopy _image_copy = {};
};

// framebuffer
struct Framebuffer;
struct FramebufferBuilder {
	FramebufferBuilder();

	FramebufferBuilder& render_pass(RenderPass* render_pass);
	FramebufferBuilder& size(uint32_t width, uint32_t height, uint32_t layers = 1);
	FramebufferBuilder& add_attachment(Image* image);
	Framebuffer* build();

	VkFramebufferCreateInfo _info;
	std::vector<VkImageView> _attachments;
};
struct Framebuffer {
	Framebuffer(FramebufferBuilder& builder);
	~Framebuffer();
	void destroy();

	VkFramebuffer vk_framebuffer = VK_NULL_HANDLE;
	FramebufferBuilder builder;
};

struct Context {
	Instance* instance = nullptr;
	Surface* surface = nullptr;
	PhysicalDevice* physical_device = nullptr;
	Device* device = nullptr;
	Queue* queue = nullptr;
	CommandPool* command_pool = nullptr;
	Swapchain* swapchain = nullptr;
};

template<typename T>
struct RawPtrLess {
	bool operator()(const std::shared_ptr<T> p1, const std::shared_ptr<T> p2) const {
		return reinterpret_cast<size_t>(p1.get()) < reinterpret_cast<size_t>(p2.get());
	}
};

Context create_context(void* window_data);

Context get_context();

void destroy_context();
}