#pragma once

#include "otcv.h"
#include <cassert>

namespace otcv {

struct FieldRange {
	std::size_t offset;
	std::size_t size;
};

template <typename T, typename Accessor>
FieldRange field_range(Accessor accessor) {
	static_assert(std::is_standard_layout_v<T>);

	T object{};
	auto& field = accessor(object);

	const auto* base = reinterpret_cast<const std::byte*>(std::addressof(object));

	const auto* address = reinterpret_cast<const std::byte*>(std::addressof(field));

	return {
		static_cast<std::size_t>(address - base),
		sizeof(field)
	};
}

#define FIELD_RANGE(Type, Path)                              \
    field_range<Type>(                                      \
        [&](Type& object) -> auto& { return object.Path; }    \
    )

template <typename Vec2>
std::array<float, 2> vec2_to_array(const Vec2& v) { return { v.x, v.y }; }

template <typename Vec3>
std::array<float, 3> vec3_to_array(const Vec3& v) { return { v.x, v.y, v.z }; }

template <typename Vec4>
std::array<float, 4> vec4_to_array(const Vec4& v) { return { v.x, v.y, v.z, v.w }; }

template <typename T>
struct StaticUBO {
	StaticUBO() {
		otcv::BufferBuilder bb;
		bb.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
			.host_access(otcv::BufferBuilder::Access::Coherent)
			.size(sizeof(T));
		_buf = new otcv::Buffer(bb);
	}
	~StaticUBO() {
		delete _buf;
		_buf = nullptr;
	}
	
	void set(FieldRange range, const void* value) {
		assert(_buf->mapped);
		std::memcpy((char*)_buf->mapped + range.offset, value, range.size);
	}

	otcv::Buffer* _buf;
};

template<typename T>
struct StaticUBOArray {
	StaticUBOArray(uint32_t n_ubos) {
		_n_ubos = n_ubos;

		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(otcv::get_context().physical_device->vk_physical_device, &device_properties);
		VkPhysicalDeviceLimits limits = device_properties.limits;
		_stride = round_up_to(sizeof(T), limits.minUniformBufferOffsetAlignment);

		otcv::BufferBuilder bb;
		bb.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
			.host_access(otcv::BufferBuilder::Access::Coherent)
			.size(_stride * _n_ubos);
		_buf = new otcv::Buffer(bb);
	}

	~StaticUBOArray() {
		delete _buf;
		_buf = nullptr;
	}

	void set(uint32_t ubo_id, FieldRange range, const void* value) {
		assert(ubo_id < _n_ubos);
		assert(_buf->mapped);
		assert(_stride * ubo_id + range.offset < _buf->builder._info.size);
		std::memcpy((char*)_buf->mapped + _stride * ubo_id + range.offset, value, range.size);
	}

	template <typename Ele>
	void set(uint32_t ubo_id, const Ele& ele) {
		assert(ubo_id < _n_ubos);
		assert(_buf->mapped);
		assert(_stride * ubo_id < _buf->builder._info.size);
		assert(_stride * ubo_id + sizeof(ele) <= _buf->builder._info.size);
		std::memcpy((char*)_buf->mapped + _stride * ubo_id, (void*)&ele, sizeof(ele));
	}

	uint32_t round_up_to(uint32_t value, uint32_t alignment) {
		return (value + alignment - 1) & ~(alignment - 1);
	}

	uint32_t _n_ubos;
	uint32_t _stride;
	otcv::Buffer* _buf;
};

struct SSBOWriteContext {
	uint32_t id;
	struct AccessContext {
		FieldRange range;
		const void* value;
	};
	std::vector<AccessContext> access_ctxs;
};
// Only supports copy-in at initialization. Synchronous write.
template <typename T>
struct SSBO {
	SSBO(uint32_t n_ssbo, VkBufferUsageFlags additional_usage = 0) {
		_n_ssbos = n_ssbo;

		otcv::BufferBuilder bb;
		bb.usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additional_usage)
			.host_access(otcv::BufferBuilder::Access::Invisible)
			.size(T::ElementStride * _n_ssbos);
		_buf = new otcv::Buffer(bb);
		_staging_buf.resize(_buf->builder._info.size);
	}
	~SSBO() {
		delete _buf;
		_buf = nullptr;
	}

	// should only be used for initializtion. As it idle waits for transfer to finish
	// provide asyn version
	void write(std::vector<SSBOWriteContext>& writes) {
		for (SSBOWriteContext& write : writes) {
			assert(write.id < _n_ssbos);
			for (SSBOWriteContext::AccessContext& acc_ctx : write.access_ctxs) {
				assert(T::ElementStride * write.id + acc_ctx.range.offset + acc_ctx.range.size <= _staging_buf.size());
				std::memcpy(_staging_buf.data() + T::ElementStride * write.id + acc_ctx.range.offset, acc_ctx.value, acc_ctx.range.size);
			}
		}

		_buf->populate(_staging_buf.data());
	}

	uint32_t _n_ssbos;
	otcv::Buffer* _buf;
	std::vector<uint8_t> _staging_buf;
};

/*
* Supports writing through a staging buffer and copy commands.Requires a command buffer as input parameter.
* Best of both (StaticUBOArray & SSBO) world:, 
*	1. Can be updated through mapped memory on host
*	2. Offer device local fast memory access
*	3. No UBO limitation, does not assume dynamically uniform access
* Only downside being the copy command
*/
template <typename T>
struct StagedWriteSSBO {
	StagedWriteSSBO(uint32_t n_ssbo, VkBufferUsageFlags additional_usage = 0) {
		_n_ssbos = n_ssbo;

		{
			otcv::BufferBuilder bb;
			bb.usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additional_usage)
				.host_access(otcv::BufferBuilder::Access::Invisible)
				.size(T::ElementStride * _n_ssbos);
			_buf = new otcv::Buffer(bb);
		}
		{
			otcv::BufferBuilder bb;
			bb.usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
				.host_access(otcv::BufferBuilder::Access::Coherent)
				.size(T::ElementStride * _n_ssbos);
			_staging_buf = new otcv::Buffer(bb);
		}
	}

	~StagedWriteSSBO() {
		delete _buf;
		delete _staging_buf;
		_buf = nullptr;
		_staging_buf = nullptr;
	}

	void set(uint32_t ssbo_id, FieldRange range, const void* value) {
		assert(ssbo_id < _n_ssbos);
		assert(_staging_buf->mapped);
		assert(T::ElementStride * ssbo_id + range.offset < _staging_buf->builder._info.size);
		std::memcpy((char*)_staging_buf->mapped + T::ElementStride * ssbo_id + range.offset, value, range.size);
	}

	void set(uint32_t ssbo_id, const typename T::Element& ele) {
		assert(ssbo_id < _n_ssbos);
		assert(_staging_buf->mapped);
		assert(T::ElementStride * ssbo_id < _staging_buf->builder._info.size);
		assert(T::ElementStride * ssbo_id + sizeof(ele) <= _staging_buf->builder._info.size);
		std::memcpy((char*)_staging_buf->mapped + T::ElementStride * ssbo_id, (void*)&ele, sizeof(ele));
	}

	void push_staging_commands(
		CommandBuffer& cmd_buf,
		ResourceState source_state,
		ResourceState target_state) {
		cmd_buf.cmd_buffer_memory_barrier(_buf, source_state, ResourceState::TransferDst);
		cmd_buf.cmd_copy_buffer(_staging_buf, _buf);
		cmd_buf.cmd_buffer_memory_barrier(_buf, ResourceState::TransferDst, target_state);
	}

	uint32_t _n_ssbos;
	Buffer*	_buf;
	Buffer*	_staging_buf;
};


}