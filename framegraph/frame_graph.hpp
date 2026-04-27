#pragma once
#include <memory>
#include <cassert>

#include "otcv.h"
#include "transient_resource_cache.hpp"

namespace otcv {
namespace fg {

typedef uint32_t ResourceHandle;
typedef uint32_t PassHandle;
typedef uint32_t PassOrder;

const uint32_t FG_INVALID_HANDLE = std::numeric_limits<uint32_t>::max();
const uint32_t FG_ORDER_LAST = std::numeric_limits<uint32_t>::max();
const VkImageSubresourceRange FG_IMAGE_NULL_RANGE = {
	VK_IMAGE_ASPECT_NONE,
	0, 1,
	0, 1};

class FrameGraph;
struct PassContext;

enum class ResourceAccessType { // Compatible pass type:
	TextureIn = 0,			// Graphics/Compute	Image	R
	ColorOut,				// Graphics			Image	W
	ColorInOut,				// Graphics			Image	R/W
	// TODO: depth stencil in. Compare-only pipeline may use depth stencil buffer as such
	DepthStencilOut,		// Graphics			Image	W
	DepthStencilInOut,		// Graphics			Image	R/W
	
	// TODO: storage images
	StorageImageIn,			// Compute			Image	R
	StorageImageOut,		// Compute			Image	W
	StorageImageInOut,		// Compute			Image	R/W

	SSBOIn,					// Compute			Buffer	R
	SSBOOut,				// Compute			Buffer	W
	SSBOInOut,				// Compute			Buffer	R/W

	VertexIn,				// Graphics			Buffer	R
	IndexIn,				// Graphics			Buffer	R
	// Indirect draw
	IndirectIn,				// Graphics			Buffer	R

	TransferIn,				// Transfer			Buffer/Image	R
	TransferOut,			// Transfer			Buffer/Image	W, new transient resourece
	TransferTarget			// Transfer			Buffer/Image	W, existing transient resource
};

enum class PassType {
	Graphics = 0,
	Compute,
	Transfer,
};

struct Pass {
	friend class FrameGraph;
public:
	Pass(FrameGraph& fg, PassHandle id, const std::string name, PassType pass_type) 
		: _fg(fg), _id(id), _name(name), _type(pass_type) {}

	bool access(
		ResourceAccessType acc_type,
		ResourceHandle id0,
		ResourceHandle id1 = FG_INVALID_HANDLE);


	// called immediately after cmd->begin()
	typedef std::function<void(CommandBuffer*)> PrePassFunc;
	void pre_pass_func(PrePassFunc precb);

	// Immediately followed by cmd->end()
	typedef std::function<void(CommandBuffer*)> PostPassFunc;
	void post_pass_func(PostPassFunc postcb);

	// only calls to RenderingBegin::Attachment::load_store() & RenderingBegin::Attachment::clear_value() will be respected
	// Other calls will cause undefined behaviour
	typedef std::function<void(RenderingBegin&)> RenderAreaFunc;
	void render_area_func(RenderAreaFunc racb);

	// only calls to RenderingBegin::Attachment::load_store() & RenderingBegin::Attachment::clear_value() will be respected.
	// Other calls will cause undefined behaviour
	typedef std::function<void(RenderingBegin::Attachment&)> LoadStoreFunc;
	void store_load_func(ResourceHandle res_id, LoadStoreFunc lscb);

	typedef std::function<void(CommandBuffer*, PassContext&)> RenderFunc;
	void execute_func(RenderFunc exec_cb);

	PassHandle id() const { return _id; }

	// PassOrder& exec_order() { return _exec_order; };

private:
	FrameGraph& _fg;
	PassHandle _id = FG_INVALID_HANDLE;
	std::string _name;
	PassType _type = PassType::Graphics;

	std::vector<ResourceHandle>								_in_textures;
	std::vector<ResourceHandle>								_out_colors;
	std::vector<std::pair<ResourceHandle, ResourceHandle>>	_inout_colors;
	ResourceHandle											_out_depth_stencil = FG_INVALID_HANDLE;
	std::pair<ResourceHandle, ResourceHandle>				_inout_depth_stencil = { FG_INVALID_HANDLE, FG_INVALID_HANDLE };

	std::vector<ResourceHandle>								_in_ssbo;
	std::vector<ResourceHandle>								_out_ssbo;
	std::vector<std::pair<ResourceHandle, ResourceHandle>>	_inout_ssbo;

	std::vector<ResourceHandle>								_in_storage_image;
	std::vector<ResourceHandle>								_out_storage_image;
	std::vector<std::pair<ResourceHandle, ResourceHandle>>	_inout_storage_image;

	std::vector<ResourceHandle>								_in_vertices;
	std::vector<ResourceHandle>								_in_indices;
	ResourceHandle											_in_indirect = FG_INVALID_HANDLE; // TODO: could have multiple indirect buffers. It is not a one-on-one relationship between indirect buffer and renderpass

	std::vector<ResourceHandle>								_in_transfer;
	std::vector<ResourceHandle>								_out_transfer;
	std::vector<std::pair<ResourceHandle, ResourceHandle>>	_target_transfer;

	std::map<ResourceHandle, ResourceAccessType>			_access_map;

	PrePassFunc								_pre_pass_cb;
	PostPassFunc							_post_pass_cb;

	std::map<ResourceHandle, LoadStoreFunc>	_load_store_cbs;
	RenderAreaFunc							_render_area_cb = nullptr;
	RenderFunc								_exec_cb = nullptr;

	PassOrder _exec_order = FG_ORDER_LAST;

	bool resource_check(ResourceAccessType access_type, ResourceHandle res_id);
};

enum class ResourceType {
	Image,
	Buffer
};

struct VirtualResource {
	ResourceHandle id;
	ResourceType type;
	std::string name;

	// for non-imported resources
	ImageBuilder	img_builder;
	BufferBuilder	buf_builder;

	PassHandle				write = FG_INVALID_HANDLE;
	std::vector<PassHandle>	reads;

	ResourceHandle logical_id = FG_INVALID_HANDLE;
};

struct LogicalResource {
	ResourceHandle id;
	ResourceType type;

	ImageBuilder	img_builder;
	BufferBuilder	buf_builder;

	std::vector<ResourceHandle> virtual_ids;
	ResourceHandle physical_id;

	PassOrder life_begin = FG_ORDER_LAST;
	PassOrder life_end = FG_ORDER_LAST;
};

struct DAG;

struct PassContext {
	DescriptorSet*			desc_set;
	std::vector<Buffer*>	vertex_bufs;
	std::vector<Buffer*>	index_bufs;
	std::vector<Buffer*>	transfer_bufs; // in this order: in, out, target
	std::vector<Image*>		transfer_imgs; // in this order: in, out, target
};

class FrameGraph {
	friend class Pass;
public:
	FrameGraph(
		std::shared_ptr<TransientImageCache> img_allocator,
		std::shared_ptr<TransientBufferCache> buf_allocator); // set this value to the number of swapchain images if framegraph is involved in frames-in-flight. Set 1 for one time use

	~FrameGraph();

	// limits
	static const uint32_t maxPasses = 64;
	static const uint32_t maxTexturesPerPass = 16; // maxDescriptorSetSampledImages, maxPerStageDescriptorSampledImages
	static const uint32_t maxSSBOsPerPass = 8; // maxDescriptorSetStorageBuffers, maxPerStageDescriptorStorageBuffers
	static const uint32_t maxStorageImagesPerPass = 8; // maxDescriptorSetStorageImages, maxPerStageDescriptorStorageImages

	// descriptor set binding rules
	static const uint32_t TextureBaseBinding = 0; // 0 - 15
	static const uint32_t SSBOBaseBinding = TextureBaseBinding + maxTexturesPerPass; // 16 - 23
	static const uint32_t StorageImageBaseBinding = SSBOBaseBinding + maxSSBOsPerPass; // 24 - 31

	enum class State {
		Building,
		Compiled
	};
	// void reset();

	DescriptorPoolBuilder descriptor_pool_capacity() {
		DescriptorPoolBuilder builder; // build once for each frame
		builder
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxTexturesPerPass * maxPasses * 2) // graphics and compute
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSSBOsPerPass * maxPasses) // compute exclusive 
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxStorageImagesPerPass * maxPasses); // compute exclusive
		builder.descriptor_set_capacity(maxPasses * 2);
		builder.descriptor_set_freeable(false);
		return builder;
	}

	Pass& add_pass(const std::string& name, PassType type);

	ResourceHandle add_resource(const std::string& name, const ImageBuilder& builder);

	ResourceHandle add_resource(const std::string& name, const BufferBuilder& builder);

	// set a virtual resource as backbuffer
	// has to be an image
	bool set_as_backbuffer(ResourceHandle v_id);

	PhysicalImagePtr backbuffer();

	struct FrameRecordInput {
		FrameRecordInput(uint32_t n_passes) {
			DescriptorPoolBuilder builder;
			builder
				.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxTexturesPerPass * maxPasses * 2) // graphics and compute
				.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSSBOsPerPass * maxPasses) // compute exclusive 
				.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxStorageImagesPerPass * maxPasses); // compute exclusive
			builder.descriptor_set_capacity(maxPasses * 2);
			builder.descriptor_set_freeable(false);
			desc_pool.reset(new DescriptorPool(builder));
			cmd_pool.reset(new CommandPool(true, false));
			ordered_passes.resize(n_passes);
		}
		std::shared_ptr<CommandPool>	cmd_pool;
		std::shared_ptr<DescriptorPool> desc_pool;
		struct PassInput {
			CommandBuffer* cmd_buf;
			DescriptorSet* desc_set;
			std::vector<CacheEntryHandle> textures;
			std::vector<CacheEntryHandle> ssbos;
			std::vector<CacheEntryHandle> storage_images;
		};
		std::vector<PassInput> ordered_passes;
	};
	// generate for all frames in flight at once
	// If framegraph is recompiled on the fly, make sure no older versions are in use
	// set additional_backbuffer_usage for backbuffer's other use outside of framegraph
	std::pair<bool, std::vector<FrameRecordInput>> compile(
		uint32_t n_frames,
		ResourceState additional_backbuffer_usage);

	bool record(FrameRecordInput& record_input);

	uint32_t n_passes() { return _passes.size(); }

	ImageBuilder& image_builder(ResourceHandle id) {
		return _v_resources[id].img_builder;
	}

	BufferBuilder& buffer_builder(ResourceHandle id) {
		return _v_resources[id].buf_builder;
	}

private:
	std::vector<std::shared_ptr<DescriptorSetLayout>> _pass_desc_set_layouts;

	std::shared_ptr<DAG> _dag = nullptr;
	std::shared_ptr<TransientImageCache> _img_allocator = nullptr;
	std::shared_ptr<TransientBufferCache> _buf_allocator = nullptr;
	std::vector<Pass> _passes;
	std::vector<PassHandle> _sorted_passes;
	
	std::vector<VirtualResource> _v_resources;
	std::vector<LogicalResource> _l_resources;
	std::vector<CacheEntryHandle> _img_resource_ids;
	std::vector<CacheEntryHandle> _buf_resource_ids;

	// virtual resource id of backbuffer
	ResourceHandle _backbuffer_id = FG_INVALID_HANDLE;

	State _state = State::Building;

	bool record_graphics_pass(FrameRecordInput::PassInput& input, PassHandle pass_id);

	bool record_compute_pass(FrameRecordInput::PassInput& input, PassHandle pass_id);

	bool record_transfer_pass(FrameRecordInput::PassInput& input, PassHandle pass_id);

	// Helper function: project virtual resource id to physical resource id
	ResourceHandle id_v2p(ResourceHandle v_id) {
		ResourceHandle l_id = _v_resources[v_id].logical_id;
		return _l_resources[l_id].physical_id;
	}

	// project virtual resource id to logical id
	ResourceHandle id_v2l(ResourceHandle v_id) {
		return _v_resources[v_id].logical_id;
	}

	bool matching_layout(DescriptorSet* set, DescriptorSetLayout* layout) {
		if (set->bindings.size() != layout->bindings.size()) {
			return false;
		}
		auto matching_binding = [&](VkDescriptorSetLayoutBinding& b0, VkDescriptorSetLayoutBinding& b1) {
			return b0.binding == b1.binding &&
				b0.descriptorType == b1.descriptorType &&
				b0.descriptorCount == b1.descriptorCount &&
				b0.stageFlags == b1.stageFlags;
		};
		for (uint32_t i = 0; i < set->bindings.size(); ++i) {
			if (!matching_binding(set->bindings[i], layout->bindings[i])) {
				return false;
			}
		}
		return true;
	}

	static bool null_range(const VkImageSubresourceRange& range) {
		return (range.aspectMask == FG_IMAGE_NULL_RANGE.aspectMask &&
			range.baseMipLevel == FG_IMAGE_NULL_RANGE.baseMipLevel && range.levelCount == FG_IMAGE_NULL_RANGE.levelCount &&
			range.baseArrayLayer == FG_IMAGE_NULL_RANGE.baseArrayLayer && range.layerCount == FG_IMAGE_NULL_RANGE.layerCount);
	};

	static bool null_range(VkDeviceSize offset, VkDeviceSize size) {
		return offset == 0 && size == VK_WHOLE_SIZE;
	}
};

// Directed Acyclic Graph
struct DAG {
	typedef uint32_t NodeHandle;

	void clear();

	NodeHandle add_node();

	bool add_dep(NodeHandle node, NodeHandle dep_on);

	void end_at(NodeHandle node);

	bool sort(std::string& error, std::vector<NodeHandle>& ordered);

private:
	struct Node {
		NodeHandle id = FG_INVALID_HANDLE;
		std::vector<NodeHandle> deps;
	};

	std::vector<Node> nodes;
	NodeHandle end_node = FG_INVALID_HANDLE;
};

}
}