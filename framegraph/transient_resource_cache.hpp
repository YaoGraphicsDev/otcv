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
		: resource(b.build()), state(ResourceState::Created) {
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

	CacheEntryHandle acquire(const Builder& b) {
		Catalog& cat = _storage_map[b];
		for (CacheEntryHandle e_id : cat) {
			Entry& e = _storage[e_id];

			if (e.currently_recording) {
				continue;
			}
			// in a 3-frame-in-flight system, frame N can always safely reuse resources from frame N - 3
			// because the CPU waits for that fence at the start of draw_frame()
			if (e.active_fence == nullptr || e.active_fence->is_signaled()) {
				e.currently_recording = true;
				assert(e.resource_ptr);
				return e_id;
			}
		}

		// upon reaching this point, all existing images are in-flight and not yet reachable by the CPU's current look-ahead
		// create one
		CacheEntryHandle new_e_id = _storage.size();
		cat.push_back(new_e_id);

		Entry& new_e = _storage.emplace_back(b);
		new_e.currently_recording = true;
		return new_e_id;
	}

	void end_frame_recording(otcv::Fence* frame_fence) {
		// clear up all resources of mark "currently recording"
		for (Entry& e : _storage) {
			if (e.currently_recording) {
				e.active_fence = frame_fence;
				e.currently_recording = false;
			}
		}
	}
	
	PhysicalResourcePtr<Builder, Resource> storage(CacheEntryHandle id) {
		if (id >= _storage.size()) {
			assert(false);
			return nullptr;
		}
		return _storage[id].resource_ptr;
	}

private:
	struct Entry {
		explicit Entry(const Builder& b)
			: resource_ptr(std::make_shared<PhysicalT>(b)) {
		}

		PhysicalPtr resource_ptr = nullptr;
		Fence* active_fence = nullptr;
		bool currently_recording = false;
	};

	// using Pool = std::list<Entry>;
	// std::unordered_map<Builder, Pool, BuilderHash, BuilderEqual> _storage;

	typedef std::vector<CacheEntryHandle> Catalog;
	std::unordered_map<Builder, Catalog, BuilderHash, BuilderEqual> _storage_map;

	typedef std::vector<Entry> Storage;
	Storage _storage;
};

typedef PhysicalResourcePtr<ImageBuilder, Image> PhysicalImagePtr;
typedef PhysicalResourcePtr<BufferBuilder, Buffer> PhysicalBufferPtr;

typedef TransientResourceCache<ImageBuilder, Image, ImageBuilderHash, ImageBuilderEqual> TransientImageCache;
typedef TransientResourceCache<BufferBuilder, Buffer, BufferBuilderHash, BufferBuilderEqual> TransientBufferCache;

}
}