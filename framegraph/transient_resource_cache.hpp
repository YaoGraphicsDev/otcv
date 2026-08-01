#pragma once

#include <cassert>
#include <memory>
#include <vector>
#include <map>
#include <algorithm>

#include "otcv.h"
#include "otcv_builder_hasher.h"

namespace otcv {
namespace fg {

template<class Builder, class Resource>
struct PhysicalResource {
	explicit PhysicalResource(Builder b)
		: resource(new Resource(b)), state(ResourceState::Created) {
	}
	~PhysicalResource() {
		delete resource;
		state = ResourceState::Null;
	}
	Resource* resource = nullptr;
	ResourceState state = ResourceState::Null;
};

template<class Builder, class Resource>
using PhysicalResourcePtr = std::shared_ptr<PhysicalResource<Builder, Resource>>;

using CacheEntryHandle = void*;
const CacheEntryHandle FG_INVALID_CACHE_ENTRY_HANDLE = nullptr;

template<
	class Builder,
	class Resource,
	class BuilderHash,
	class BuilderEqual
>
class TransientResourceCache {
public:
	using PhysicalT = PhysicalResource<Builder, Resource>;
	using PhysicalPtr = std::shared_ptr<PhysicalT>;

private:
	struct Entry {
		explicit Entry(const Builder& b)
			: resource_ptr(std::make_shared<PhysicalT>(b)) {
		}
		Entry() {}

		PhysicalPtr resource_ptr = nullptr;
		Fence* active_fence = nullptr;
		bool currently_recording = false;
		int latest_acquired = 0;
	};

	// using Pool = std::list<Entry>;
	// std::unordered_map<Builder, Pool, BuilderHash, BuilderEqual> _storage;

	typedef std::vector<CacheEntryHandle> Catalog;
	std::unordered_map<Builder, Catalog, BuilderHash, BuilderEqual> _catalog_map;

	typedef std::map<CacheEntryHandle, Entry> Storage;
	Storage _storage;

	int frame_count = 0;

	int expire_interval = 30;

public:
	void set_expire_interval(int interval) {
		expire_interval = interval;
	}

	//CacheEntryHandle acquire(const Builder& b) {
	//	Catalog& cat = _catalog_map[b];
	//	for (CacheEntryHandle e_hdl : cat) {
	//		Entry& e = _storage.at(e_hdl);

	//		if (!e.resource_ptr) {
	//			assert(false);
	//			continue;
	//		}

	//		if (e.currently_recording) {
	//			continue;
	//		}
	//		// in a 3-frame-in-flight system, frame N can always safely reuse resources from frame N - 3
	//		// because the CPU waits for that fence at the start of draw_frame()
	//		assert(e.active_fence);
	//		if (e.active_fence->is_signaled()) {
	//			e.currently_recording = true;
	//			assert(e.resource_ptr);
	//			e.latest_acquired = frame_count;
	//			return e_hdl;
	//		}
	//	}

	//	// upon reaching this point, all existing images are in-flight and not yet reachable by the CPU's current look-ahead
	//	// create one
	//	Entry new_e(b);
	//	new_e.currently_recording = true;
	//	new_e.latest_acquired = frame_count;

	//	CacheEntryHandle new_e_hdl = new_e.resource_ptr.get();
	//	assert(_storage.count(new_e_hdl) == 0);
	//	_storage[new_e_hdl] = new_e;
	//	cat.push_back(new_e_hdl);

	//	return new_e_hdl;
	//}

	CacheEntryHandle acquire(const Builder& b) {
		Catalog& cat = _catalog_map[b];

		for (CacheEntryHandle e_hdl : cat) {
			auto storage_it = _storage.find(e_hdl);
			if (storage_it == _storage.end()) {
				assert(false);
				continue;
			}

			Entry& e = storage_it->second;

			if (!e.resource_ptr) {
				assert(false);
				continue;
			}

			if (e.currently_recording) {
				continue;
			}

			// in a 3-frame-in-flight system, frame N can always safely reuse resources from frame N - 3
			// because the CPU waits for that fence at the start of draw_frame()
			if (e.active_fence && e.active_fence->is_signaled()) {
				e.currently_recording = true;
				e.latest_acquired = frame_count;
				return e_hdl;
			}
		}

		// upon reaching this point, all existing images are in-flight and not yet reachable by the CPU's current look-ahead
		// create one
		Entry new_e(b);
		new_e.currently_recording = true;
		new_e.latest_acquired = frame_count;

		CacheEntryHandle new_e_hdl = new_e.resource_ptr.get();

		assert(_storage.count(new_e_hdl) == 0);

		_storage.emplace(new_e_hdl, std::move(new_e));
		cat.push_back(new_e_hdl);

		return new_e_hdl;
	}

	void remove_from_catalog(CacheEntryHandle handle) {
		for (auto& [builder, catalog] : _catalog_map) {
			auto it = std::remove(
				catalog.begin(),
				catalog.end(),
				handle);

			catalog.erase(it, catalog.end());
		}
	}

	// return vacancy
	void end_frame_recording(otcv::Fence* frame_fence) {
		assert(frame_fence);

		for (auto& [hdl, entry] : _storage) {
			if (entry.currently_recording) {
				entry.active_fence = frame_fence;
				entry.currently_recording = false;
			}
		}

		for (auto storage_it = _storage.begin(); storage_it != _storage.end(); ) {
			CacheEntryHandle hdl = storage_it->first;
			Entry& entry = storage_it->second;

			bool old_enough = entry.latest_acquired < frame_count - expire_interval;
			// TODO: Drag window around. Somehow old_enough entries dont get GPU done flag set. Why are active fences not signaled?
			// bool gpu_done = entry.active_fence && entry.active_fence->is_signaled();
			bool can_delete =
				!entry.currently_recording &&
				old_enough /*&& gpu_done*/;

			if (can_delete) {
				remove_from_catalog(hdl);
				storage_it = _storage.erase(storage_it);
			}
			else {
				++storage_it;
			}
		}

		++frame_count;
	}
	
	//void end_frame_recording(otcv::Fence* frame_fence) {
	//	// clear up all resources of mark "currently recording"
	//	for (auto& ele : _storage) {
	//		Entry& e = ele.second;
	//		if (e.currently_recording) {
	//			e.active_fence = frame_fence;
	//			e.currently_recording = false;
	//		}
	//	}

	//	for (auto it = _storage.begin(); it != _storage.end();) {
	//		Entry& e = it->second;
	//		if (e.resource_ptr && e.latest_acquired < frame_count - expire_interval) {
	//			it = _storage.erase(it); // returns next iterator
	//		}
	//		else {
	//			++it;
	//		}
	//	}

	//	++frame_count;
	//}
	
	PhysicalResourcePtr<Builder, Resource> storage(CacheEntryHandle hdl) {
		return _storage.at(hdl).resource_ptr;
	}

	uint32_t capacity() {
		return _storage.size();
	}

	//uint32_t alive_count() {
	//	uint32_t count = 0;
	//	for (const Entry& e : _storage) {
	//		if (e.resource_ptr) {
	//			++count;
	//		}
	//	}
	//	return count;
	//}
};

typedef PhysicalResourcePtr<ImageBuilder, Image> PhysicalImagePtr;
typedef PhysicalResourcePtr<BufferBuilder, Buffer> PhysicalBufferPtr;

typedef TransientResourceCache<ImageBuilder, Image, ImageBuilderHash, ImageBuilderEqual> TransientImageCache;
typedef TransientResourceCache<BufferBuilder, Buffer, BufferBuilderHash, BufferBuilderEqual> TransientBufferCache;

}
}