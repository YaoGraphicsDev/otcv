#pragma once

#include "otcv.h"

#include <vector>

template <typename Con>
struct SequenceHash {
	size_t operator()(const Con& container) const {
		size_t hash = 0;
		std::hash<Con::value_type> hasher;
		for (const Con::value_type& item : container) {
			// A common hash combining strategy (boost-like)
			hash ^= hasher(item) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

namespace otcv {
namespace fg {

// image hasher
// excludes name
struct ImageBuilderSerializer {
	std::vector<uint8_t> serialized;
	ImageBuilderSerializer(const ImageBuilder& b);
	ImageBuilderSerializer() {}
};

struct ImageBuilderHash {
	std::size_t operator()(const otcv::ImageBuilder& b) const {
		ImageBuilderSerializer s(b);
		SequenceHash<std::vector<uint8_t>> hasher;
		return hasher(s.serialized);
	}
};

struct ImageBuilderEqual {
	bool operator()(const otcv::ImageBuilder& a, const otcv::ImageBuilder& b) const {
		ImageBuilderSerializer sa(a);
		ImageBuilderSerializer sb(b);
		return sa.serialized == sb.serialized;
	}
};

struct ImageBuilderLess {
	bool operator()(const otcv::ImageBuilder& a, const otcv::ImageBuilder& b) const {
		ImageBuilderSerializer sa(a);
		ImageBuilderSerializer sb(b);
		return sa.serialized < sb.serialized;
	}
};


// buffer hasher
// excludes names
struct BufferBuilderSerializer {
	std::vector<uint8_t> serialized;
	BufferBuilderSerializer(const BufferBuilder& b);
	BufferBuilderSerializer() {}
};

struct BufferBuilderHash {
	std::size_t operator()(const otcv::BufferBuilder& b) const {
		BufferBuilderSerializer s(b);
		SequenceHash<std::vector<uint8_t>> hasher;
		return hasher(s.serialized);
	}
};

struct BufferBuilderEqual {
	bool operator()(const otcv::BufferBuilder& a, const otcv::BufferBuilder& b) const {
		BufferBuilderSerializer sa(a);
		BufferBuilderSerializer sb(b);
		return sa.serialized == sb.serialized;
	}
};

struct BufferBuilderLess {
	bool operator()(const otcv::BufferBuilder& a, const otcv::BufferBuilder& b) const {
		BufferBuilderSerializer sa(a);
		BufferBuilderSerializer sb(b);
		return sa.serialized < sb.serialized;
	}
};

}
}

