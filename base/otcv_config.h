#pragma once
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <vector>
#include <map>
#include <array>

#define OTCV_WINDOW GLFW;

namespace otcv {

	static uint32_t layer_count = 1;
	static const char* _layer_names[] = { "VK_LAYER_KHRONOS_validation" };
	static const char** layer_names = _layer_names;

	static uint32_t device_extension_count = 1;
	static const char* device_extension_names[] = { "VK_KHR_swapchain" };
	static const char** pp_device_extension_names = device_extension_names;

	static VkQueueFlagBits queue_required = VkQueueFlagBits(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);

	static VkSurfaceFormatKHR surface_format = { VK_FORMAT_B8G8R8A8_SRGB , VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	static VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;  // For some reason, mailbox mode is not supported on Intel integrated graphics
	static VkImageUsageFlags swapchain_image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	static VkFormatFeatureFlags swapchain_format_features = VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	
	// https://vkguide.dev/docs/chapter-4/descriptors/#:~:text=Allocating%20descriptor%20sets,those%20descriptor%20pools
	static bool allow_free_descriptor_set = false;
}