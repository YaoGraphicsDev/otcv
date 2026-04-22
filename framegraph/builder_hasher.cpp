#include "builder_hasher.hpp"

#include <cassert>

template<typename T>
inline void serialize_trivial(std::vector<uint8_t>& serialized, T value) {
	uint8_t* start = (uint8_t*)(&value);
	size_t length = sizeof(value);
	serialized.insert(serialized.end(), start, start + length);
}

inline void serialize_string(std::vector<uint8_t>& serialized, const std::string& str) {
	uint8_t* start = (uint8_t*)(str.data());
	size_t length = str.length();
	serialized.insert(serialized.end(), start, start + length);
}

inline void serialize_arbitrary(std::vector<uint8_t>& serialized, const void* ptr, uint32_t length) {
	uint8_t* start = (uint8_t*)(ptr);
	serialized.insert(serialized.end(), start, start + length);
}

namespace otcv {
namespace fg{

ImageBuilderSerializer::ImageBuilderSerializer(const ImageBuilder & b) {
	serialize_trivial(serialized, b._has_mips);
	assert(!b._image_info.pNext);
	assert(!b._image_info.pQueueFamilyIndices);
	serialize_arbitrary(serialized, &(b._image_info), sizeof(b._image_info));
	assert(!b._view_info.pNext);
	serialize_arbitrary(serialized, &(b._view_info), sizeof(b._view_info));
}

BufferBuilderSerializer::BufferBuilderSerializer(const BufferBuilder& b) {
	serialize_trivial(serialized, b._mem_props);
	assert(!b._info.pNext);
	assert(!b._info.pQueueFamilyIndices);
	serialize_arbitrary(serialized, &(b._info), sizeof(b._info));
}

}
}