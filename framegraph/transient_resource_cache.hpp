#pragma once

#include <cassert>
#include <memory>
#include <list>
#include <unordered_map>

#include "otcv.h"
#include "builder_hasher.hpp"

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

typedef uint32_t CacheEntryHandle;

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

	void set_expire_interval(int interval) {
		expire_interval = interval;
	}

	CacheEntryHandle acquire(const Builder& b) {
		Catalog& cat = _storage_map[b];
		for (CacheEntryHandle e_id : cat) {
			Entry& e = _storage[e_id];

			if (!e.resource_ptr) {
				// dead resource
				continue;
			}

			if (e.currently_recording) {
				continue;
			}
			// in a 3-frame-in-flight system, frame N can always safely reuse resources from frame N - 3
			// because the CPU waits for that fence at the start of draw_frame()
			assert(e.active_fence);
			if (e.active_fence->is_signaled()) {
				e.currently_recording = true;
				assert(e.resource_ptr);
				e.latest_acquired = frame_count;
				return e_id;
			}
		}

		// upon reaching this point, all existing images are in-flight and not yet reachable by the CPU's current look-ahead
		// create one
		CacheEntryHandle new_e_id = _storage.size();
		cat.push_back(new_e_id);

		Entry& new_e = _storage.emplace_back(b);
		new_e.currently_recording = true;
		new_e.latest_acquired = frame_count;
		return new_e_id;
	}

	// return vacancy
	void end_frame_recording(otcv::Fence* frame_fence) {
		// clear up all resources of mark "currently recording"
		for (Entry& e : _storage) {
			if (e.currently_recording) {
				e.active_fence = frame_fence;
				e.currently_recording = false;
			}
		}

		for (Entry& e : _storage) {
			if (e.resource_ptr && e.latest_acquired < frame_count - expire_interval) {
				// kill expired resource
				e = Entry();
			}
		}

		++frame_count;
	}
	
	PhysicalResourcePtr<Builder, Resource> storage(CacheEntryHandle id) {
		if (id >= _storage.size()) {
			assert(false);
			return nullptr;
		}
		return _storage[id].resource_ptr;
	}

	uint32_t capacity() {
		return _storage.size();
	}

	uint32_t alive_count() {
		uint32_t count = 0;
		for (const Entry& e : _storage) {
			if (e.resource_ptr) {
				++count;
			}
		}
		return count;
	}

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
	std::unordered_map<Builder, Catalog, BuilderHash, BuilderEqual> _storage_map;

	typedef std::vector<Entry> Storage;
	Storage _storage;

	int frame_count = 0;

	int expire_interval = 30;
};

typedef PhysicalResourcePtr<ImageBuilder, Image> PhysicalImagePtr;
typedef PhysicalResourcePtr<BufferBuilder, Buffer> PhysicalBufferPtr;

typedef TransientResourceCache<ImageBuilder, Image, ImageBuilderHash, ImageBuilderEqual> TransientImageCache;
typedef TransientResourceCache<BufferBuilder, Buffer, BufferBuilderHash, BufferBuilderEqual> TransientBufferCache;

}
}