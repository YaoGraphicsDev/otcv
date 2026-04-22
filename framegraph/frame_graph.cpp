#include "frame_graph.hpp"
#include "otcv_utils.h"

#include <cassert>
#include <stack>
#include <algorithm>
#include <sstream>
#include <numeric>
#include <iostream>

namespace otcv {
namespace fg {

bool Pass::resource_check(ResourceAccessType access_type, ResourceHandle res_id) {
	const VirtualResource& res = _fg._v_resources[res_id];
	const std::map<PassType, std::map<ResourceType, std::vector<ResourceAccessType>>> legal_map = {
		{ PassType::Graphics, {
			{ ResourceType::Image, {
				{ ResourceAccessType::TextureIn },
				{ ResourceAccessType::ColorOut },
				{ ResourceAccessType::ColorInOut },
				{ ResourceAccessType::DepthStencilOut },
				{ ResourceAccessType::DepthStencilInOut },
			}},
			{ ResourceType::Buffer, {
				{ ResourceAccessType::VertexIn },
				{ ResourceAccessType::IndirectIn },
			}},
		}},
		{ PassType::Compute, {
			{ ResourceType::Image, {
				{ ResourceAccessType::TextureIn },
				{ ResourceAccessType::StorageImageIn },
				{ ResourceAccessType::StorageImageOut },
				{ ResourceAccessType::StorageImageInOut },
			}},
			{ ResourceType::Buffer, {
				{ ResourceAccessType::SSBOIn },
				{ ResourceAccessType::SSBOOut },
				{ ResourceAccessType::SSBOInOut },
			}},
		}},
		{ PassType::Transfer, {
			{ ResourceType::Image, {
				{ ResourceAccessType::TransferIn },
				{ ResourceAccessType::TransferOut },
				{ ResourceAccessType::TransferTarget },
			}},
			{ ResourceType::Buffer, {
				{ ResourceAccessType::TransferIn },
				{ ResourceAccessType::TransferOut },
				{ ResourceAccessType::TransferTarget },
			}},
		}},
	};

	const std::vector<ResourceAccessType>& allowed_vector = legal_map.at(_type).at(res.type);
	if (!std::any_of(allowed_vector.begin(), allowed_vector.end(), [&](const ResourceAccessType& a) {
		return a == access_type;
	})) {
		std::cout << "Pass::resource_check() error: Illegal pass and resource type combination: "
			<< "pass type = " << (int)_type
			<< ", resource_type = " << (int)res.type
			<< ", access type = " << (int)access_type
			<< std::endl;
		assert(false);
		return false;
	}

	return true;
}

bool Pass::access(
	ResourceAccessType acc_type,
	ResourceHandle id0,
	ResourceHandle id1) {
	if (id0 >= _fg._v_resources.size()) {
		std::cout << "Pass::resource_access() error: invalid id0 = " << id0 << std::endl;
		assert(false);
		return false;
	}
	if (id1 != FG_INVALID_HANDLE && id1 >= _fg._v_resources.size()) {
		std::cout << "Pass::resource_access() error: invalid id1 = " << id1 << std::endl;
		assert(false);
		return false;
	}
	if (id1 == id0) {
		std::cout << "Pass::resource_access() error: id0 and id1 should point to different virtual resources" << std::endl;
		assert(false);
		return false;
	}

	if ((acc_type == ResourceAccessType::ColorInOut ||
		acc_type == ResourceAccessType::DepthStencilInOut ||
		acc_type == ResourceAccessType::SSBOInOut ||
		acc_type == ResourceAccessType::StorageImageInOut ||
		acc_type == ResourceAccessType::TransferTarget) &&
		id1 >= _fg._v_resources.size()) {
		std::cout << "Pass::resource_access() error: inout access type with invalid id1 = " << id1 << std::endl;
		assert(false);
		return false;
	}

	if (!resource_check(acc_type, id0)) {
		std::cout << "Pass::resource_access() error: resource id = " << id0 << " resource check failed" << std::endl;
		assert(false);
		return false;
	}
	if (id1 != FG_INVALID_HANDLE) {
		if (_fg._v_resources[id1].type != _fg._v_resources[id0].type) {
			std::cout << "Pass::resource_access() error: resource id0 = " << id0 <<" and id1 = " << id1 << " type mismatch" << std::endl;
			assert(false);
			return false;
		}
		if (!resource_check(acc_type, id1)) {
			std::cout << "Pass::resource_access() error: resource id = " << id1 << " resource check failed" << std::endl;
			assert(false);
			return false;
		}
	}

	if (acc_type == ResourceAccessType::TextureIn) {
		_in_textures.push_back(id0);
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::ColorOut) {
		_out_colors.push_back(id0);
		_fg._v_resources[id0].write = _id;
	}
	else if (acc_type == ResourceAccessType::ColorInOut) {
		_inout_colors.push_back({ id0, id1 });
		_fg._v_resources[id0].reads.push_back(_id);
		_fg._v_resources[id1].write = _id;
	}
	else if (acc_type == ResourceAccessType::DepthStencilOut) {
		_out_depth_stencil = id0;
		_fg._v_resources[id0].write = _id;
	}
	else if (acc_type == ResourceAccessType::DepthStencilInOut) {
		_inout_depth_stencil = { id0, id1 };
		_fg._v_resources[id0].reads.push_back(_id);
		_fg._v_resources[id1].write = _id;
	}
	else if (acc_type == ResourceAccessType::StorageImageIn) {
		_in_storage_image.push_back(id0);
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::StorageImageOut) {
		_out_storage_image.push_back(id0);
		_fg._v_resources[id0].write = _id;
	}
	else if (acc_type == ResourceAccessType::StorageImageInOut) {
		_inout_storage_image.push_back({ id0, id1 });
		_fg._v_resources[id0].reads.push_back(_id);
		_fg._v_resources[id1].write = _id;
	}
	else if (acc_type == ResourceAccessType::SSBOIn) {
		_in_ssbo.push_back(id0);
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::SSBOOut) {
		_out_ssbo.push_back(id0);
		_fg._v_resources[id0].write = _id;
	}
	else if (acc_type == ResourceAccessType::SSBOInOut) {
		_inout_ssbo.push_back({ id0, id1 });
		_fg._v_resources[id0].reads.push_back(_id);
		_fg._v_resources[id1].write = _id;
	}
	else if (acc_type == ResourceAccessType::VertexIn) {
		_in_vertices.push_back(id0);
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::IndirectIn) {
		_in_indirect = id0;
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::TransferIn) {
		_in_transfer.push_back(id0);
		_fg._v_resources[id0].reads.push_back(_id);
	}
	else if (acc_type == ResourceAccessType::TransferOut) {
		_out_transfer.push_back(id0);
		_fg._v_resources[id0].write = _id;
	}
	else if (acc_type == ResourceAccessType::TransferTarget) {
		_target_transfer.push_back({ id0, id1 });
		_fg._v_resources[id0].reads.push_back(_id);
		_fg._v_resources[id1].write = _id;
	}
	else {
		assert(false);
	}
	_access_map[id0] = acc_type;
	if (id1 != FG_INVALID_HANDLE) {
		_access_map[id1] = acc_type;
	}

	return true;
}

void Pass::pre_pass_func(PrePassFunc precb) {
	_pre_pass_cb = precb;
}

void Pass::post_pass_func(PostPassFunc postcb) {
	_post_pass_cb = postcb;
}

void Pass::store_load_func(ResourceHandle res_id, LoadStoreFunc lscb) {
	_load_store_cbs[res_id] = lscb;
}

void Pass::render_area_func(RenderAreaFunc racb) {
	_render_area_cb = racb;
}

void Pass::execute_func(RenderFunc exec_cb) {
	_exec_cb = exec_cb;
}

FrameGraph::FrameGraph(
	std::shared_ptr<TransientImageCache> img_allocator,
	std::shared_ptr<TransientBufferCache> buf_allocator) {
	_dag.reset(new DAG);
	_img_allocator = img_allocator;
	_buf_allocator = buf_allocator;
	_backbuffer_id = FG_INVALID_HANDLE;
	_state = State::Building;
}

FrameGraph::~FrameGraph() {
}

void FrameGraph::reset() {
	// TODO: deal with descriptor and pool
	// reset this frame's descriptor pool?
	assert(false);
	// TODO: pass in FrameRecordInput and reset descriptor pool here

	_dag->clear();
	_passes.clear();
	_sorted_passes.clear();

	_v_resources.clear();
	_l_resources.clear();
	_img_resource_ids.clear();
	_buf_resource_ids.clear();

	_backbuffer_id = FG_INVALID_HANDLE;
	_state = State::Building;
}

Pass& FrameGraph::add_pass(const std::string& name, PassType type) {
	if (_state != State::Building) {
		std::cout << "FrameGraph::add_pass() error: reset graph before building" << std::endl;
		assert(false);
		return Pass(*this, FG_INVALID_HANDLE, "", PassType::Graphics);
	}
	if (maxPasses <= _passes.size()) {
		std::cout << "FrameGraph::add_pass() error: can't add any more passes. Pass limit = " << maxPasses << std::endl;
		assert(false);
		return Pass(*this, FG_INVALID_HANDLE, "", PassType::Graphics);
	}
	PassHandle id = _passes.size();
	return _passes.emplace_back(Pass(*this, id, name, type));
}

ResourceHandle FrameGraph::add_resource(const std::string& name, const ImageBuilder& builder) {
	if (_state != State::Building) {
		std::cout << "FrameGraph::add_resource() error: reset graph before building" << std::endl;
		assert(false);
		return FG_INVALID_HANDLE;
	}
	ResourceHandle id = _v_resources.size();
	VirtualResource& res = _v_resources.emplace_back();
	res.id = id;
	res.type = ResourceType::Image;
	res.name = name;
	res.img_builder = builder;
	return id;
}

ResourceHandle FrameGraph::add_resource(const std::string& name, const BufferBuilder& builder) {
	if (_state != State::Building) {
		std::cout << "FrameGraph::add_resource() error: reset graph before building" << std::endl;
		assert(false);
		return FG_INVALID_HANDLE;
	}
	ResourceHandle id = _v_resources.size();
	VirtualResource& res = _v_resources.emplace_back();
	res.id = id;
	res.type = ResourceType::Buffer;
	res.name = name;
	res.buf_builder = builder;
	return id;
}

bool FrameGraph::set_as_backbuffer(ResourceHandle v_id) {
	if (_state != State::Building) {
		std::cout << "FrameGraph::set_as_backbuffer() error: reset graph before building" << std::endl;
		assert(false);
		return false;
	}

	if (v_id >= _v_resources.size()) {
		std::cout << "Framegraph::set_as_backbuffer() failed: Virtual resource id = "  << v_id << " does not exist";
		assert(false);
		return false;
	}
	if (_v_resources[v_id].write == FG_INVALID_HANDLE) {
		std::cout << "Framegraph::set_as_backbuffer() failed: No pass wirtes to virtual resource id = " << v_id;
		assert(false);
		return false;
	}
	if (_v_resources[v_id].type != ResourceType::Image) {
		std::cout << "Framegraph::set_as_backbuffer() failed: Virtual resource id = " << v_id << " is not an image resource";
		assert(false);
		return false;
	}

	_backbuffer_id = v_id;
	return true;
}

PhysicalImagePtr FrameGraph::backbuffer() {
	if (_backbuffer_id == FG_INVALID_HANDLE) {
		return nullptr;
	}
	assert(_backbuffer_id < _v_resources.size());
	return _img_allocator->storage(_img_resource_ids[id_v2p(_backbuffer_id)]);
}

std::pair<bool, std::vector<FrameGraph::FrameRecordInput>> FrameGraph::compile(
	// std::vector<CommandPool*> frame_command_pools,
	// std::vector<DescriptorPool*> frame_desc_pools,
	uint32_t n_frames,
	ResourceState additional_backbuffer_usage) {
	if (_state == State::Compiled) {
		std::cout << "FrameGraph::compile() failed: framegraph already compiled." << std::endl;
		assert(false);
		return { false, {} };
	}

	// prepare DAG
	_dag.reset(new DAG);
	for (auto& p : _passes) {
		_dag->add_node();
	}
	for (auto& res : _v_resources) {
		for (PassHandle p_id : res.reads) {
			_dag->add_dep(p_id, res.write);
		}
	}
	if (_backbuffer_id == FG_INVALID_HANDLE) {
		std::cout << "Framegraph::compile() failed: backbuffer not set." << std::endl;
		assert(false);
		return { false, {} };
	}
	_dag->end_at(_v_resources[_backbuffer_id].write);

	// topological sort
	std::string sort_error;
	bool sort_result = _dag->sort(sort_error, _sorted_passes);
	if (!sort_result) {
		std::cout << sort_error << std::endl;
		assert(false);
		return { false, {} };
	}

	for (PassOrder o = 0; o < _sorted_passes.size(); ++o) {
		_passes[_sorted_passes[o]]._exec_order = o;
	}

	std::vector<FrameRecordInput> record_inputs;
	record_inputs.reserve(n_frames);
	for (uint32_t n = 0; n < n_frames; ++n) {
		record_inputs.emplace_back(_passes.size());
	}

	// setup descriptor set layout for each pass
	_pass_desc_set_layouts.resize(_passes.size());

	for (PassHandle p_id = 0; p_id < _passes.size(); ++p_id) {
		const Pass& pass = _passes[p_id];
		if (pass._type != PassType::Graphics && pass._type != PassType::Compute && pass._type != PassType::Transfer) {
			assert(false);
			continue;
		}
		VkShaderStageFlags stage_flag = 0;
		if (pass._type == PassType::Graphics) {
			stage_flag = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		else if (pass._type == PassType::Compute) {
			stage_flag = VK_SHADER_STAGE_COMPUTE_BIT;
		}

		// set up bindings. Bindings not necessarily continuous
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		// textures
		if (pass._in_textures.size() > maxTexturesPerPass) {
			std::cout << "compile() error : number of textures exceeds limit. pass id = " << p_id << std::endl;
			assert(false);
			return { false, {} };
		}
		for (int i = 0; i < pass._in_textures.size(); ++i) {
			auto& b = bindings.emplace_back();
			b = {};
			b.binding = TextureBaseBinding + i;
			b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			b.descriptorCount = 1;
			b.stageFlags = stage_flag;
		}
		// SSBOs
		if (pass._in_ssbo.size() + pass._out_ssbo.size() + pass._inout_ssbo.size() > maxSSBOsPerPass) {
			std::cout << "compile() error : number of ssbos exceeds limit. pass id = " << p_id << std::endl;
			assert(false);
			return { false, {} };
		}
		uint32_t ssbo_count = 0;
		auto add_ssbo_binding = [&](uint32_t offset) {
			assert(pass._type == PassType::Compute);
			auto& b = bindings.emplace_back();
			b = {};
			b.binding = SSBOBaseBinding + offset;
			b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			b.descriptorCount = 1;
			b.stageFlags = stage_flag;
		};
		for (auto id : pass._in_ssbo) {
			add_ssbo_binding(ssbo_count++);
		}
		for (auto id : pass._out_ssbo) {
			add_ssbo_binding(ssbo_count++);
		}
		for (auto id : pass._inout_ssbo) {
			add_ssbo_binding(ssbo_count++);
		}
		// storage images
		if (pass._in_storage_image.size() + pass._out_storage_image.size() + pass._inout_storage_image.size() > maxStorageImagesPerPass) {
			std::cout << "compile() error : number of storage images exceeds limit. pass id = " << p_id << std::endl;
			assert(false);
			return { false, {} };
		}
		uint32_t storage_image_count = 0;
		auto add_storage_image_binding = [&](uint32_t offset) {
			assert(pass._type == PassType::Compute);
			auto& b = bindings.emplace_back();
			b = {};
			b.binding = StorageImageBaseBinding + offset;
			b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			b.descriptorCount = 1;
			b.stageFlags = stage_flag;
		};
		for (auto id : pass._in_storage_image) {
			add_storage_image_binding(storage_image_count++);
		}
		for (auto id : pass._out_storage_image) {
			add_storage_image_binding(storage_image_count++);
		}
		for (auto id : pass._inout_storage_image) {
			add_storage_image_binding(storage_image_count++);
		}
		_pass_desc_set_layouts[p_id].reset(new DescriptorSetLayout(bindings));
		
		std::vector<CacheEntryHandle> textures(pass._in_textures.size(), FG_INVALID_HANDLE);
		std::vector<CacheEntryHandle> ssbos(ssbo_count, FG_INVALID_HANDLE);
		std::vector<CacheEntryHandle> storage_images(storage_image_count, FG_INVALID_HANDLE);
		for (FrameRecordInput& input : record_inputs) {
			// command buffer and descriptor set allocation
			PassOrder order = _passes[p_id]._exec_order;
			input.ordered_passes[order].cmd_buf = input.cmd_pool->allocate();
			input.ordered_passes[order].desc_set = input.desc_pool->allocate(_pass_desc_set_layouts[p_id].get());
			input.ordered_passes[order].textures = textures;
			input.ordered_passes[order].ssbos = ssbos;
			input.ordered_passes[order].storage_images = storage_images;
		}
	}

	// Merge virtual resources into logical resources
	std::map<ResourceHandle, ResourceHandle> v2l_map; // virtual -> logical
	std::vector<std::vector<PassHandle>> l2v_list; // logical -> virtual

	auto new_logical_resource = [&](ResourceHandle v) -> bool {
		if (v == FG_INVALID_HANDLE) {
			std::cout << "Framegraph::compile() error: invalid virtual resource id" << std::endl;
			assert(false);
			return false;
		}
		if (v2l_map.count(v) != 0) { // check if this virtual resource is first seen
			std::cout << "Framegraph::compile() error: virtual resource id = " << v << " was wrongfully accessed by other passes" << std::endl;
			assert(false);
			return false;
		}
		v2l_map[v] = l2v_list.size();
		l2v_list.push_back({ v });
		return true;
	};

	auto merge_virtual_resource = [&](ResourceHandle in_v, ResourceHandle out_v) -> bool {
		if (in_v == FG_INVALID_HANDLE) {
			std::cout << "Framegraph::compile() error: invalid input virtual resource id" << std::endl;
			assert(false);
			return false;
		}
		if (out_v == FG_INVALID_HANDLE) {
			std::cout << "Framegraph::compile() error: invalid output virtual resource id" << std::endl;
			assert(false);
			return false;
		}
		if (v2l_map.count(in_v) == 0) { // some pass has got to have output this image before this pass
			std::cout << "Framegraph::compile() error: virtual resource id = " << in_v << " has not yet been produced by a previous pass" << std::endl;
			assert(false);
			return false;
		}
		if (v2l_map.count(out_v) > 0) { // should be first time seeing this virtual id
			std::cout << "Framegraph::compile() error: virtual resource id = " << out_v << " was wrongfully accessed by other passes" << std::endl;
			assert(false);
			return false;
		}
		ResourceHandle l = v2l_map[in_v];
		v2l_map[out_v] = l;
		l2v_list[l].push_back(out_v);
		return true;
	};

	for (PassHandle p : _sorted_passes) {
		// output colors. Logical resource life cycle starts here
		for (ResourceHandle v : _passes[p]._out_colors) {
			if (!new_logical_resource(v)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot allocate new logical resource for out color" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// out depth stencil
		{
			ResourceHandle v = _passes[p]._out_depth_stencil;
			if (v != FG_INVALID_HANDLE) {
				if (!new_logical_resource(v)) {
					std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot allocate new logical resource for out depth stencil" << std::endl;
					assert(false);
					return { false, {} };
				}
			}
		}

		// inout colors. This is where merge is supposed to happen
		for (auto v_pair : _passes[p]._inout_colors) {
			if (!merge_virtual_resource(v_pair.first, v_pair.second)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot merge inout color to one logical resource" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// inout depth stencil
		auto v_pair = _passes[p]._inout_depth_stencil;
		if (v_pair.first != FG_INVALID_HANDLE && v_pair.second != FG_INVALID_HANDLE) {
			if (!merge_virtual_resource(v_pair.first, v_pair.second)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot merge inout depth stencil to one logical resource" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// input textures
		for (ResourceHandle v : _passes[p]._in_textures) {
			if (v2l_map.count(v) == 0) { // some pass has got to have output this image before this pass
				std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// input storage image
		for (ResourceHandle v : _passes[p]._in_storage_image) {
			if (v2l_map.count(v) == 0) { // some pass has got to have output this image before this pass
				std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// output storage image
		for (ResourceHandle v : _passes[p]._out_storage_image) {
			if (!new_logical_resource(v)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot allocate new logical resource for out storage image" << std::endl;
				assert(false);
				return { false, {} };
			}
		}
		
		//inout storage image
		for (auto v_pair : _passes[p]._inout_storage_image) {
			if (!merge_virtual_resource(v_pair.first, v_pair.second)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot merge inout storage image to one logical resource" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// input SSBO
		for (ResourceHandle v : _passes[p]._in_ssbo) {
			if (v2l_map.count(v) == 0) { // some pass has got to have output this buffer before this pass
				std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// output SSBO
		for (ResourceHandle v : _passes[p]._out_ssbo) {
			if (!new_logical_resource(v)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot allocate new logical resource for out ssbo" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// inout ssbo;
		for (auto v_pair : _passes[p]._inout_ssbo) {
			if (!merge_virtual_resource(v_pair.first, v_pair.second)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot merge inout ssbo to one logical resource" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// input vertex buffer
		for (ResourceHandle v : _passes[p]._in_vertices) {
			if (v2l_map.count(v) == 0) { // some pass has got to have output this buffer before this pass
				std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// input indirect
		{
			ResourceHandle v = _passes[p]._in_indirect;
			if (v != FG_INVALID_HANDLE) {
				if (v2l_map.count(v) == 0) { // some pass has got to have output this buffer before this pass
					std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
					assert(false);
					return { false, {} };
				}
			}
		}

		// in transfer
		for (ResourceHandle v : _passes[p]._in_transfer) {
			if (v2l_map.count(v) == 0) { // some pass has got to have output this resource before this pass
				std::cout << "Framegraph::compile(): error: virtual resource id = " << v << " has not yet been produced by a previous pass. current pass id = " << p << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// out transfer
		for (ResourceHandle v : _passes[p]._out_transfer) {
			if (!new_logical_resource(v)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot allocate new logical resource for out transfer" << std::endl;
				assert(false);
				return { false, {} };
			}
		}

		// transfer targets
		for (auto v_pair : _passes[p]._target_transfer) {
			if (!merge_virtual_resource(v_pair.first, v_pair.second)) {
				std::cout << "Framegraph::compile(): error: pass id = " << p << " cannot merge transfer target to one logical resource" << std::endl;
				assert(false);
				return { false, {} };
			}
		}
	}

	// Check if all virtual resources in a logical resource list is mergeable
	// Criteria:
	//	1. same type, either all images or buffers
	//	2. Matching builders
	auto virtrual_resources_mergeable = [&](const std::vector<ResourceHandle>& v_list) -> bool {
		assert(!v_list.empty()); // guaranteed by l2v_list
		if (_v_resources[v_list[0]].type == ResourceType::Image) {
			ImageBuilder& first_img_builder = _v_resources[v_list[0]].img_builder;
			return std::all_of(v_list.begin() + 1, v_list.end(), [&](ResourceHandle v) {
				if (_v_resources[v].type != ResourceType::Image) {
					return false;
				}
				ImageBuilderEqual eq;
				return eq(first_img_builder, _v_resources[v].img_builder);
			});
		}
		else if (_v_resources[v_list[0]].type == ResourceType::Buffer) {
			assert(!v_list.empty()); // guaranteed by l2v_list
			BufferBuilder& first_buf_builder = _v_resources[v_list[0]].buf_builder;
			return std::all_of(v_list.begin() + 1, v_list.end(), [&](ResourceHandle v) {
				if (_v_resources[v].type != ResourceType::Buffer) {
					return false;
				}
				BufferBuilderEqual eq;
				return eq(first_buf_builder, _v_resources[v].buf_builder);
			});
		}
		else {
			assert(false);
		}
	};
	
	// set up logical resources
	std::ostringstream oss;
	for (ResourceHandle i = 0; i < l2v_list.size(); ++i) {
		if (!virtrual_resources_mergeable(l2v_list[i])) {
			std::cout << "FrameGraph::compile() failed: cannot merge virtual resources. logical id = " << i << ", virtual ids = ";
			for (ResourceHandle v : l2v_list[i]) {
				std::cout << v << ", ";
			}
			std::cout << std::endl;
			assert(false);
			return { false, {} };
		}

		// set up logical resources
		LogicalResource& l_res = _l_resources.emplace_back();
		l_res.id = i;
		l_res.type = _v_resources[l2v_list[i][0]].type;
		l_res.img_builder = _v_resources[l2v_list[i][0]].img_builder;
		l_res.buf_builder = _v_resources[l2v_list[i][0]].buf_builder;
		l_res.virtual_ids = l2v_list[i];

		// associate virtual resources
		for (ResourceHandle v : l2v_list[i]) {
			_v_resources[v].logical_id = i;
		}

		// TODO: check this shit. Is the assumption correct?
		l_res.life_begin = _passes[_v_resources[l2v_list[i][0]].write]._exec_order;
		PassOrder latest_order = 0;
		for (ResourceHandle v : l2v_list[i]) {
			// Among all passes that read and write this resource, which one is the latest?
			for (PassHandle p : _v_resources[v].reads) {
				if (_passes[p]._exec_order > latest_order) {
					latest_order = _passes[p]._exec_order;
				}
			}
			PassHandle p = _v_resources[v].write;
			if (_passes[p]._exec_order > latest_order) {
				latest_order = _passes[p]._exec_order;
			}
		}
		l_res.life_end = latest_order;


		// determine resource usage
		VkImageUsageFlags virtual_image_usage = 0;
		VkBufferUsageFlags virtual_buffer_usage = 0;
		for (ResourceHandle v : l2v_list[i]) { // traverse every virtual resource associated with this logical resource
			using RAT = ResourceAccessType;
			// How does the pass that write to this virtual resource access this resource?
			// The answer to this question partially determines image usage.
			PassHandle write_pass = _v_resources[v].write;
			assert(write_pass != FG_INVALID_HANDLE);
			switch (_passes[write_pass]._access_map.at(v)) {
			case RAT::ColorOut:
			case RAT::ColorInOut:
				virtual_image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				break;
			case RAT::DepthStencilOut:
			case RAT::DepthStencilInOut:
				virtual_image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				break;
			case RAT::StorageImageOut:
			case RAT::StorageImageInOut:
				virtual_image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
				break;
			case RAT::SSBOOut:
			case RAT::SSBOInOut:
				virtual_buffer_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
				break;
			case RAT::TransferOut:
				virtual_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				virtual_image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				break;
			case RAT::TransferTarget:
				virtual_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				virtual_image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				break;
			default:
				assert(false);
				break;
			}

			// How do the passes that read this virtual resource access this resource?
			// The answer to this question partially determines image usage.
			const std::vector<PassHandle>& read_passes = _v_resources[v].reads;
			for (PassHandle read_pass : read_passes) {
				switch (_passes[read_pass]._access_map.at(v)) {
				case RAT::TextureIn:
					virtual_image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
					break;
				case RAT::ColorInOut:
					virtual_image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
					break;
				case RAT::DepthStencilInOut:
					virtual_image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
					break;
				case RAT::StorageImageIn:
				case RAT::StorageImageInOut:
					virtual_image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
					break;
				case RAT::SSBOIn:
				case RAT::SSBOInOut:
					virtual_buffer_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
					break;
				case RAT::VertexIn:
					virtual_buffer_usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
					break;
				case RAT::IndirectIn:
					virtual_buffer_usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
					break;
				case RAT::TransferIn:
					virtual_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
					virtual_image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
					break;
				default:
					assert(false);
					break;
				}
			}
		}
		// done with traversing all virtual resources associated with this logical resource

		VkImageUsageFlags logical_image_usage = virtual_image_usage;
		VkBufferUsageFlags logical_buffer_usage = virtual_buffer_usage;

		// If we are currently dealing with the logical resource that covers backbuffer,
		// then add a usage appointed by the user, for use outside of framegraph
		if (v2l_map[_backbuffer_id] == l_res.id) {
			switch (additional_backbuffer_usage) {
			case ResourceState::TransferSrc:
				logical_image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				break;
			case ResourceState::FragSample:
				logical_image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
				break;
			default:
				std::cout << "FrameGraph::compile() failed: unrecognized additional backbuffer usage = " << uint32_t(additional_backbuffer_usage) << std::endl;
				assert(false);
				return { false, {} };
				break;
			}
		}

		// finally, set usage to builder
		if (l_res.type == ResourceType::Image) {
			l_res.img_builder.usage(logical_image_usage);
		}
		else if (l_res.type == ResourceType::Buffer) {
			l_res.buf_builder.usage(logical_buffer_usage);
		}
		else {
			assert(false);
		}
	}
	
	// alias logical resources. Do this after resource usage is determined, the point where builder is complete
	{
		struct PhysicalSlot {
			PassOrder busy_until;
			uint32_t physical_index;

			// Sort by time so we can use lower_bound
			bool operator<(const PhysicalSlot& other) const {
				return busy_until < other.busy_until;
			}
		};

		// Map each unique ImageBuilder to a set of available physical slots
		std::unordered_map<ImageBuilder, std::set<PhysicalSlot>, ImageBuilderHash, ImageBuilderEqual> img_pool_map;
		std::unordered_map<BufferBuilder, std::set<PhysicalSlot>, BufferBuilderHash, BufferBuilderEqual> buf_pool_map;

		// Sort logical resources by their birth (life_begin)
		std::vector<uint32_t> sorted_indices(_l_resources.size());
		std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
		std::sort(sorted_indices.begin(), sorted_indices.end(), [&](uint32_t a, uint32_t b) {
			return _l_resources[a].life_begin < _l_resources[b].life_begin;
		});

		uint32_t global_physical_image_count = 0;
		uint32_t global_physical_buffer_count = 0;
		for (uint32_t id : sorted_indices) {
			LogicalResource& r = _l_resources[id];
			if (r.type == ResourceType::Image) {
				ImageBuilder& b = r.img_builder;
				auto& pool = img_pool_map[b];

				// largest busy_until such that busy_until < r.life_begin
				auto it = pool.lower_bound({ r.life_begin, 0 });
				if (it != pool.begin()) {
					// reuse opportunity. Resuse the one that finished most recently
					--it;
					uint32_t reused_id = it->physical_index;
					// update the pool
					pool.erase(it);
					pool.insert({ r.life_end, reused_id });
					r.physical_id = reused_id;
				}
				else {
					// no available resource finished before we started
					uint32_t new_id = global_physical_image_count++;
					pool.insert({ r.life_end, new_id });
					r.physical_id = new_id;
				}
			}
			else if (r.type == ResourceType::Buffer) {
				BufferBuilder& b = r.buf_builder;
				auto& pool = buf_pool_map[b];

				// largest busy_until such that busy_until < r.life_begin
				auto it = pool.lower_bound({ r.life_begin, 0 });
				if (it != pool.begin()) {
					// reuse opportunity. Resuse the one that finished most recently
					--it;
					uint32_t reused_id = it->physical_index;
					// update the pool
					pool.erase(it);
					pool.insert({ r.life_end, reused_id });
					r.physical_id = reused_id;
				}
				else {
					// no available resource finished before we started
					uint32_t new_id = global_physical_buffer_count++;
					pool.insert({ r.life_end, new_id });
					r.physical_id = new_id;
				}
			}
			else {
				assert(false);
				return { false, {} };
			}
		}
		
		_img_resource_ids.resize(global_physical_image_count, FG_INVALID_HANDLE);
		_buf_resource_ids.resize(global_physical_buffer_count, FG_INVALID_HANDLE);
	}

	_state = State::Compiled;
	
	return { true, record_inputs };
}

bool FrameGraph::record_graphics_pass(FrameRecordInput::PassInput& input, PassHandle pass_id) {
	const Pass& pass = _passes[pass_id];

	// descriptor sets have got to be correctly allocated before this point 
	assert(matching_layout(input.desc_set, _pass_desc_set_layouts[pass_id].get()));

	CommandBuffer* cmd = input.cmd_buf;

	cmd->begin();

	if (pass._pre_pass_cb) {
		pass._pre_pass_cb(input.cmd_buf);
	}

	// textures. Send into execution lambda through PassContext
	std::vector<CacheEntryHandle> tex_cache_ids;
	for (ResourceHandle v_id : pass._in_textures) {
		ResourceHandle p_id = id_v2p(v_id);
		assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input textures must have been allocated. Guaranteed by compile()
		tex_cache_ids.push_back(_img_resource_ids[p_id]);
		PhysicalImagePtr tex_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

		// layout transition
		if (tex_ptr->state != ResourceState::FragSample) {
			transition_image_state(cmd, tex_ptr->resource, tex_ptr->state, ResourceState::FragSample);//, pass._img_subrange_map.at(v_id));
			tex_ptr->state = ResourceState::FragSample;
		}
	}
	
	auto& pass_desc_layout = _pass_desc_set_layouts[pass_id];
	// check if texture descriptors need to bind to a difference set of images
	assert(tex_cache_ids.size() == input.textures.size()); // guaranteed by compile()
	if(tex_cache_ids != input.textures) {
		// different set of images for textures. Update descriptor set
		std::vector<Image*> new_textures;
		for (CacheEntryHandle& id : tex_cache_ids) {
			new_textures.push_back(_img_allocator->storage(id)->resource);
		}
		input.desc_set->bind_consecutive_sampled_images(TextureBaseBinding, new_textures.size(), new_textures.data());
		std::copy(tex_cache_ids.begin(), tex_cache_ids.end(), input.textures.begin());
	}

	// configure vertex buffers
	std::vector<CacheEntryHandle> vb_cache_ids;
	for (ResourceHandle v_id : pass._in_vertices) {
		ResourceHandle p_id = id_v2p(v_id);
		assert(_buf_resource_ids[p_id] != FG_INVALID_HANDLE); // input SSBOs must have been allocated. Guaranteed by compile()
		vb_cache_ids.push_back(_buf_resource_ids[p_id]);
		PhysicalBufferPtr buf_ptr = _buf_allocator->storage(_buf_resource_ids[p_id]);

		// layout transition
		if (buf_ptr->state != ResourceState::VertexRead) {
			transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::VertexRead);
			buf_ptr->state = ResourceState::VertexRead;
		}
	}

	// populate PassContext
	PassContext p_ctx;
	p_ctx.desc_set = input.desc_set;
	p_ctx.vertex_bufs.reserve(pass._in_vertices.size());
	std::transform(vb_cache_ids.begin(), vb_cache_ids.end(), std::back_inserter(p_ctx.vertex_bufs),
		[&](CacheEntryHandle id) {
		return _buf_allocator->storage(id)->resource;
	});

	// configure attachments
	RenderingBegin render_begin;
	// render area
	if (!pass._render_area_cb) {
		std::cout << "Framegraph::record_graphics_pass() error: render area not set. pass id = " << pass._id << std::endl;
		assert(false);
		return false;
	}
	pass._render_area_cb(render_begin);

	// out color attachments
	for (ResourceHandle v_id : pass._out_colors) {
		ResourceHandle p_id = id_v2p(v_id);
		CacheEntryHandle cache_id = _img_allocator->acquire(_l_resources[id_v2l(v_id)].img_builder);
		_img_resource_ids[p_id] = cache_id;
		PhysicalImagePtr attach_ptr = _img_allocator->storage(cache_id);

		// attachment steup
		// const VkImageSubresourceRange& sub_range = pass._img_subrange_map.at(v_id);
		RenderingBegin::Attachment& attach_setup = render_begin
			.color_attachment()
			// .image_view(null_range(sub_range) ? attach_ptr->resource->vk_view : attach_ptr->resource->view_of_subresource(sub_range))
			.image_view(attach_ptr->resource->vk_view)
			.image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		auto iter = pass._load_store_cbs.find(v_id);
		if (iter != pass._load_store_cbs.end() && iter->second) {
			iter->second(attach_setup);
		}
		attach_setup.end();

		// layout transition
		transition_image_state(cmd, attach_ptr->resource, attach_ptr->state, ResourceState::ColorAttachment);//, sub_range);
		attach_ptr->state = ResourceState::ColorAttachment;
	}
	// inout color attachments
	for (auto v_id_pair : pass._inout_colors) {
		assert(id_v2p(v_id_pair.first) == id_v2p(v_id_pair.second)); // inout color virtual id pair must point to the same physical id. Guaranteed by compile()
		ResourceHandle v_id = v_id_pair.first;
		ResourceHandle p_id = id_v2p(v_id);
		assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input/output color attachment must have been allocated. Guaranteed by compile()
		PhysicalImagePtr attach_ptr = _img_allocator->storage(_img_resource_ids[p_id]); 

		// attachment setup
		// const VkImageSubresourceRange& sub_range = pass._img_subrange_map.at(v_id);
		RenderingBegin::Attachment& attach_setup = render_begin
			.color_attachment()
			// .image_view(null_range(sub_range) ? attach_ptr->resource->vk_view : attach_ptr->resource->view_of_subresource(sub_range))
			.image_view(attach_ptr->resource->vk_view)
			.image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		auto iter = pass._load_store_cbs.find(v_id);
		if (iter != pass._load_store_cbs.end() && iter->second) {
			iter->second(attach_setup);
		}
		attach_setup.end();

		// layout transition
		transition_image_state(cmd, attach_ptr->resource, attach_ptr->state, ResourceState::ColorAttachment);//, sub_range);
		attach_ptr->state = ResourceState::ColorAttachment;
	}
	// out depth stencil
	{
		if (pass._out_depth_stencil != FG_INVALID_HANDLE) {
			ResourceHandle v_id = pass._out_depth_stencil;
			ResourceHandle p_id = id_v2p(v_id);
			CacheEntryHandle cache_id = _img_allocator->acquire(_l_resources[id_v2l(v_id)].img_builder);
			_img_resource_ids[p_id] = cache_id;
			PhysicalImagePtr attach_ptr = _img_allocator->storage(cache_id);

			// attachment setup
			// const VkImageSubresourceRange& sub_range = pass._img_subrange_map.at(v_id);
			RenderingBegin::Attachment& attach_setup = render_begin
				.depth_stencil_attachment()
				// .image_view(null_range(sub_range) ? attach_ptr->resource->vk_view : attach_ptr->resource->view_of_subresource(sub_range))
				.image_view(attach_ptr->resource->vk_view)
				.image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			auto iter = pass._load_store_cbs.find(v_id);
			if (iter != pass._load_store_cbs.end() && iter->second) {
				iter->second(attach_setup);
			}
			attach_setup.end();

			// layout transition
			transition_image_state(cmd, attach_ptr->resource, attach_ptr->state, ResourceState::DepthStencilAttachment);//, sub_range);
			attach_ptr->state = ResourceState::DepthStencilAttachment;
		}
	}
	// in out depth stencil
	if (pass._inout_depth_stencil.first != FG_INVALID_HANDLE &&
		pass._inout_depth_stencil.second != FG_INVALID_HANDLE) {
		assert(id_v2p(pass._inout_depth_stencil.first) == id_v2p(pass._inout_depth_stencil.second)); // inout depth stencil virtual id pair must point to the same physical id. Guaranteed by compile()
		ResourceHandle v_id = pass._inout_depth_stencil.first;
		ResourceHandle p_id = id_v2p(v_id);
		assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input/output color attachment must have been allocated. Guaranteed by compile()
		PhysicalImagePtr attach_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

		// attachment setup
		// const VkImageSubresourceRange& sub_range = pass._img_subrange_map.at(v_id);
		RenderingBegin::Attachment& attach_setup = render_begin
			.depth_stencil_attachment()
			//.image_view(null_range(sub_range) ? attach_ptr->resource->vk_view : attach_ptr->resource->view_of_subresource(sub_range))
			.image_view(attach_ptr->resource->vk_view)
			.image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		auto iter = pass._load_store_cbs.find(v_id);
		if (iter != pass._load_store_cbs.end() && iter->second) {
			iter->second(attach_setup);
		}
		attach_setup.end();

		// layout transition
		transition_image_state(cmd, attach_ptr->resource, attach_ptr->state, ResourceState::DepthStencilAttachment);//, sub_range);
		attach_ptr->state = ResourceState::DepthStencilAttachment;
		// p_ctx.inout_depth_stencil = attach_ptr->image;
	}

	// begin renderpass
	cmd->cmd_begin_rendering(render_begin);
	if (pass._exec_cb) {
		pass._exec_cb(cmd, p_ctx);
	}
	cmd->cmd_end_rendering();
	// one pass done

	if (pass._post_pass_cb) {
		pass._post_pass_cb(input.cmd_buf);
	}

	cmd->end();

	return true;
}

bool FrameGraph::record_compute_pass(FrameRecordInput::PassInput& input, PassHandle pass_id) {
	const Pass& pass = _passes[pass_id];

	// descriptor sets have got to be correctly allocated before this point 
	assert(matching_layout(input.desc_set, _pass_desc_set_layouts[pass_id].get()));
	auto& pass_desc_layout = _pass_desc_set_layouts[pass_id];

	CommandBuffer* cmd = input.cmd_buf;
	cmd->begin();

	if (pass._pre_pass_cb) {
		pass._pre_pass_cb(input.cmd_buf);
	}

	// textures. Send into execution lambda through PassContext
	{
		std::vector<CacheEntryHandle> tex_cache_ids;
		for (ResourceHandle v_id : pass._in_textures) {
			ResourceHandle p_id = id_v2p(v_id);
			assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input textures must have been allocated. Guaranteed by compile()
			tex_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr tex_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

			// layout transition
			if (tex_ptr->state != ResourceState::ComputeSample) {
				transition_image_state(cmd, tex_ptr->resource, tex_ptr->state, ResourceState::ComputeSample);//, pass._img_subrange_map.at(v_id));
				tex_ptr->state = ResourceState::ComputeSample;
			}
		}
		// check texture limits
		if (tex_cache_ids.size() > maxTexturesPerPass) {
			std::cout << "record_compute_pass() error : number of textures =" << tex_cache_ids.size() << " exceeds limit. pass id = " << pass_id << std::endl;
			assert(false);
			return false;
		}

		// check if texture descriptors need to bind to a difference set of images
		assert(tex_cache_ids.size() == input.textures.size());
		if (tex_cache_ids != input.textures) {
			// different set of images for textures. Update descriptor set
			std::vector<Image*> new_textures;
			for (CacheEntryHandle& id : tex_cache_ids) {
				new_textures.push_back(_img_allocator->storage(id)->resource);
			}
			input.desc_set->bind_consecutive_sampled_images(TextureBaseBinding, new_textures.size(), new_textures.data());
			std::copy(tex_cache_ids.begin(), tex_cache_ids.end(), input.textures.begin());
		}
	}

	// SSBOs
	{
		std::vector<CacheEntryHandle> ssbo_cache_ids;
		// in SSBO
		for (ResourceHandle v_id : pass._in_ssbo) {
			ResourceHandle p_id = id_v2p(v_id);
			assert(_buf_resource_ids[p_id] != FG_INVALID_HANDLE); // input SSBOs must have been allocated. Guaranteed by compile()
			ssbo_cache_ids.push_back(_buf_resource_ids[p_id]);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(_buf_resource_ids[p_id]);

			// layout transition
			if (buf_ptr->state != ResourceState::ComputeSSBORead) {
				transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::ComputeSSBORead);
				buf_ptr->state = ResourceState::ComputeSSBORead;
			}
		}
		// out SSBO
		for (ResourceHandle v_id : pass._out_ssbo) {
			ResourceHandle p_id = id_v2p(v_id);
			CacheEntryHandle cache_id = _buf_allocator->acquire(_l_resources[id_v2l(v_id)].buf_builder);
			_buf_resource_ids[p_id] = cache_id;
			ssbo_cache_ids.push_back(_buf_resource_ids[p_id]);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(cache_id);

			// layout transition
			transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::ComputeSSBOWrite);
			buf_ptr->state = ResourceState::ComputeSSBOWrite;
		}
		// inout SSBO
		for (auto v_id_pair : pass._inout_ssbo) {
			assert(id_v2p(v_id_pair.first) == id_v2p(v_id_pair.second)); // inout ssbo virtual id pair must point to the same physical id. Guaranteed by compile()
			ResourceHandle v_id = v_id_pair.first;
			ResourceHandle p_id = id_v2p(v_id);
			assert(_buf_resource_ids[p_id] != FG_INVALID_HANDLE); // input/output ssbos must have been allocated. Guaranteed by compile()
			ssbo_cache_ids.push_back(_buf_resource_ids[p_id]);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(_buf_resource_ids[p_id]);

			// layout transition
			transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::ComputeSSBO);
			buf_ptr->state = ResourceState::ComputeSSBO;
		}

		// check if ssbo descriptors need to bind to a difference set of images
		assert(ssbo_cache_ids.size() == input.ssbos.size());
		if (ssbo_cache_ids != input.ssbos) {
			// different set of images for textures. Update descriptor set
			std::vector<Buffer*> new_ssbos;
			for (CacheEntryHandle& id : ssbo_cache_ids) {
				new_ssbos.push_back(_buf_allocator->storage(id)->resource);
			}
			input.desc_set->bind_consecutive_buffers(SSBOBaseBinding, new_ssbos.size(), new_ssbos.data());
			std::copy(ssbo_cache_ids.begin(), ssbo_cache_ids.end(), input.ssbos.begin());
		}
	}
	// storage images
	{
		std::vector<CacheEntryHandle> image_cache_ids;
		// in storage image
		for (ResourceHandle v_id : pass._in_storage_image) {
			ResourceHandle p_id = id_v2p(v_id);
			assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input SSBOs must have been allocated. Guaranteed by compile()
			image_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr img_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

			// layout transition
			if (img_ptr->state != ResourceState::ComputeStorageRead) {
				transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::ComputeStorageRead);// , pass._img_subrange_map.at(v_id));
				img_ptr->state = ResourceState::ComputeStorageRead;
			}
		}
		// out storage image
		for (ResourceHandle v_id : pass._out_storage_image) {
			ResourceHandle p_id = id_v2p(v_id);
			CacheEntryHandle cache_id = _img_allocator->acquire(_l_resources[id_v2l(v_id)].img_builder);
			_img_resource_ids[p_id] = cache_id;
			image_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr img_ptr = _img_allocator->storage(cache_id);

			// layout transition
			transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::ComputeStorageWrite);// , pass._img_subrange_map.at(v_id));
			img_ptr->state = ResourceState::ComputeStorageWrite;
		}
		// inout storage image
		for (auto v_id_pair : pass._inout_storage_image) {
			assert(id_v2p(v_id_pair.first) == id_v2p(v_id_pair.second)); // inout ssbo virtual id pair must point to the same physical id. Guaranteed by compile()
			ResourceHandle v_id = v_id_pair.first;
			ResourceHandle p_id = id_v2p(v_id);
			assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // input/output ssbos must have been allocated. Guaranteed by compile()
			image_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr img_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

			// layout transition
			transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::ComputeStorage);// , pass._img_subrange_map.at(v_id));
			img_ptr->state = ResourceState::ComputeStorage;
		}

		// check if ssbo descriptors need to bind to a difference set of images
		assert(image_cache_ids.size() == input.storage_images.size());
		if (image_cache_ids != input.storage_images) {
			// different set of images for storage images. Update descriptor set
			std::vector<Image*> new_storage_images;
			for (CacheEntryHandle& id : image_cache_ids) {
				new_storage_images.push_back(_img_allocator->storage(id)->resource);
			}
			std::cout << "image_cache_ids[0] = " << image_cache_ids[0] << std::endl;
			input.desc_set->bind_consecutive_storage_images(StorageImageBaseBinding, new_storage_images.size(), new_storage_images.data());
			std::copy(image_cache_ids.begin(), image_cache_ids.end(), input.storage_images.begin());
		}
	}

	PassContext p_ctx;
	p_ctx.desc_set = input.desc_set;

	// issue compute pass
	if (pass._exec_cb) {
		pass._exec_cb(cmd, p_ctx);
	}

	if (pass._post_pass_cb) {
		pass._post_pass_cb(input.cmd_buf);
	}

	cmd->end();

	return true;
}

bool FrameGraph::record_transfer_pass(FrameRecordInput::PassInput& input, PassHandle pass_id) {
	const Pass& pass = _passes[pass_id];

	// descriptor sets have got to be correctly allocated before this point 
	assert(matching_layout(input.desc_set, _pass_desc_set_layouts[pass_id].get()));
	auto& pass_desc_layout = _pass_desc_set_layouts[pass_id];

	CommandBuffer* cmd = input.cmd_buf;
	cmd->begin();

	if (pass._pre_pass_cb) {
		pass._pre_pass_cb(input.cmd_buf);
	}

	std::vector<CacheEntryHandle> img_cache_ids;
	std::vector<CacheEntryHandle> buf_cache_ids;
	// in transfer
	for (ResourceHandle v_id : pass._in_transfer) {
		ResourceHandle p_id = id_v2p(v_id);
		if (_v_resources[v_id].type == ResourceType::Image) {
			assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // must have been allocated. Guaranteed by compile()
			img_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr img_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

			// layout transition
			if (img_ptr->state != ResourceState::TransferSrc) {
				transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::TransferSrc);// , pass._img_subrange_map.at(v_id));
				img_ptr->state = ResourceState::TransferSrc;
			}
		}
		else if (_v_resources[v_id].type == ResourceType::Buffer) {
			assert(_buf_resource_ids[p_id] != FG_INVALID_HANDLE); // must have been allocated. Guaranteed by compile()
			buf_cache_ids.push_back(_buf_resource_ids[p_id]);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(_buf_resource_ids[p_id]);

			// layout transition
			if (buf_ptr->state != ResourceState::TransferSrc) {
				transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::TransferSrc);// , pass._img_subrange_map.at(v_id));
				buf_ptr->state = ResourceState::TransferSrc;
			}
		}
		else {
			assert(false);
		}
	}
	// out transfer
	for (ResourceHandle v_id : pass._out_transfer) {
		ResourceHandle p_id = id_v2p(v_id);
		if (_v_resources[v_id].type == ResourceType::Image) {
			CacheEntryHandle cache_id = _img_allocator->acquire(_l_resources[id_v2l(v_id)].img_builder);
			_img_resource_ids[p_id] = cache_id;
			img_cache_ids.push_back(cache_id);
			PhysicalImagePtr img_ptr = _img_allocator->storage(cache_id);

			// layout transition
			transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::TransferDst);// , pass._img_subrange_map.at(v_id));
			img_ptr->state = ResourceState::TransferDst;
		}
		else if (_v_resources[v_id].type == ResourceType::Buffer) {
			CacheEntryHandle cache_id = _buf_allocator->acquire(_l_resources[id_v2l(v_id)].buf_builder);
			_buf_resource_ids[p_id] = cache_id;
			buf_cache_ids.push_back(cache_id);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(cache_id);

			// layout transition
			transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::TransferDst);// , pass._img_subrange_map.at(v_id));
			buf_ptr->state = ResourceState::TransferDst;
		}
		else {
			assert(false);
		}
	}
	// transfer target
	for (auto v_id_pair : pass._target_transfer) {
		assert(id_v2p(v_id_pair.first) == id_v2p(v_id_pair.second)); // inout ssbo virtual id pair must point to the same physical id. Guaranteed by compile()
		ResourceHandle v_id = v_id_pair.first;
		ResourceHandle p_id = id_v2p(v_id);
		if (_v_resources[v_id].type == ResourceType::Image) {
			assert(_img_resource_ids[p_id] != FG_INVALID_HANDLE); // must have been allocated. Guaranteed by compile()
			img_cache_ids.push_back(_img_resource_ids[p_id]);
			PhysicalImagePtr img_ptr = _img_allocator->storage(_img_resource_ids[p_id]);

			// layout transition
			if (img_ptr->state != ResourceState::TransferDst) {
				transition_image_state(cmd, img_ptr->resource, img_ptr->state, ResourceState::TransferDst);// , pass._img_subrange_map.at(v_id));
				img_ptr->state = ResourceState::TransferDst;
			}
		}
		else if (_v_resources[v_id].type == ResourceType::Buffer) {
			assert(_buf_resource_ids[p_id] != FG_INVALID_HANDLE); // must have been allocated. Guaranteed by compile()
			buf_cache_ids.push_back(_buf_resource_ids[p_id]);
			PhysicalBufferPtr buf_ptr = _buf_allocator->storage(_buf_resource_ids[p_id]);

			// layout transition
			if (buf_ptr->state != ResourceState::TransferDst) {
				transition_buffer_state(cmd, buf_ptr->resource, buf_ptr->state, ResourceState::TransferDst);// , pass._img_subrange_map.at(v_id));
				buf_ptr->state = ResourceState::TransferDst;
			}
		}
		else {
			assert(false);
		}
	}

	PassContext p_ctx;
	p_ctx.transfer_imgs.reserve(img_cache_ids.size());
	std::transform(img_cache_ids.begin(), img_cache_ids.end(), std::back_inserter(p_ctx.transfer_imgs),
		[&](CacheEntryHandle id) {
		return _img_allocator->storage(id)->resource;
	});
	p_ctx.transfer_bufs.reserve(buf_cache_ids.size());
	std::transform(buf_cache_ids.begin(), buf_cache_ids.end(), std::back_inserter(p_ctx.transfer_bufs),
		[&](CacheEntryHandle id) {
		return _buf_allocator->storage(id)->resource;
	});

	// issue transfer pass
	if (pass._exec_cb) {
		pass._exec_cb(cmd, p_ctx);
	}

	if (pass._post_pass_cb) {
		pass._post_pass_cb(input.cmd_buf);
	}

	cmd->end();

	return true;
}

bool FrameGraph::record(FrameRecordInput& input) {

	if (_state != State::Compiled) {
		std::cout << "Framegraph::record() error: not compiled" << std::endl;
		assert(false);
		return false;
	}

	input.cmd_pool->reset();

	for (uint32_t pass_order = 0; pass_order < _sorted_passes.size(); ++pass_order) {
		PassHandle pass_id = _sorted_passes[pass_order];
		// const Pass& p = _passes[pass_id];

		// Multiple command buffers, each for one pass.
		// This is a workaround to avoid a potential driver bug.
		// On XPS13, when using dynamic rendering, if multiple renderpasses are recorded in one command buffer,
		// and one of the passes has a null depth attachment, vkCmdBeginRendering will throw access violation.
		// Details: https://www.reddit.com/r/vulkan/comments/1l0ffey/access_violation_when_calling_vkcmdbeginrendering/
		
		// ommandBuffer* cmd = cmd_bufs[pass_order];
		// escriptorSet* desc_set = desc_sets[pass_order];
		if (_passes[pass_id]._type == PassType::Graphics) {
			if (!record_graphics_pass(input.ordered_passes[pass_order], pass_id)) {
				assert(false);
				return false;
			}
		}
		else if (_passes[pass_id]._type == PassType::Compute) {
			if (!record_compute_pass(input.ordered_passes[pass_order], pass_id)) {
				assert(false);
				return false;
			}
		}
		else if (_passes[pass_id]._type == PassType::Transfer) {
			if (!record_transfer_pass(input.ordered_passes[pass_order], pass_id)) {
				assert(false);
				return false;
			}
		}
		else {
			assert(false);
		}
	}
	// all renderpasses done recording

	// TODO: get backbuffer ready for transfer
	// really necessary?

	return true;
}

void DAG::clear() {
	nodes.clear();
	end_node = FG_INVALID_HANDLE;
}

DAG::NodeHandle DAG::add_node() {
	uint32_t id = static_cast<uint32_t>(nodes.size());
	nodes.push_back({ id });
	return id;
}

bool DAG::add_dep(NodeHandle node, NodeHandle dep_on) {
	if (node >= nodes.size() || dep_on >= nodes.size()) {
		return false;
	}
	nodes[node].deps.push_back(dep_on);
	return true;
}

void DAG::end_at(NodeHandle node) {
	end_node = node;
}

bool DAG::sort(std::string& error, std::vector<NodeHandle>& ordered) {
	error.clear();
	ordered.clear();

	if (end_node == FG_INVALID_HANDLE) {
		error = "DAG::sort() failed: no end node specified.";
		return false;
	}

	if (end_node >= nodes.size()) {
		error = "DAG::sort() failed: end node handle is invalid.";
		return false;
	}

	enum VisitState : uint8_t {
		Unvisited,
		Visiting,
		Visited
	};

	struct StackEntry {
		NodeHandle node;
		uint32_t dep_id;
	};

	std::vector<VisitState> state(nodes.size(), Unvisited);
	std::stack<StackEntry> stack;
	std::vector<NodeHandle> postorder;
	postorder.reserve(nodes.size());

	stack.push({ end_node, 0 });

	while (!stack.empty()) {
		StackEntry& top = stack.top();
		NodeHandle u = top.node;

		if (u >= nodes.size()) {
			error = "DAG::sort() failed: invalid node handle.";
			return false;
		}

		if (state[u] == Unvisited)
			state[u] = Visiting;

		const Node& node = nodes[u];

		if (top.dep_id < node.deps.size()) {
			NodeHandle v = node.deps[top.dep_id++];

			if (v >= nodes.size()) {
				error = "DAG::sort() failed: invalid dependency handle.";
				return false;
			}

			if (state[v] == Unvisited) {
				stack.push({ v, 0 });
			}
			else if (state[v] == Visiting) {
				error = "DAG::sort() failed: cycle detected.";
				return false;
			}
		}
		else {
			state[u] = Visited;
			postorder.push_back(u);
			stack.pop();
		}
	}

	ordered = std::move(postorder);
	return true;
}

}
}

