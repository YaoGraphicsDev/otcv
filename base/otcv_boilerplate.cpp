#include "otcv.h"
#include "otcv_config.h"
#include "otcv_utils_internal.h"
#include "otcv_globals.h"

#include <iostream>
#include <vector>
#include <list>
#include <memory>
#include <algorithm>

#if OTCV_WINDOW == GLFW
#include <glfw/glfw3.h>
#endif

namespace otcv {

std::shared_ptr<Instance> g_instance = nullptr;
std::shared_ptr<Surface> g_surface = nullptr;
PhysicalDevice g_physical_device{};
std::shared_ptr<Device> g_device = nullptr;
Queue g_queue{};
std::shared_ptr<CommandPool> g_command_pool = nullptr;
std::shared_ptr<Swapchain> g_swapchain = nullptr;

bool layer_supported(const std::string& name) {
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

    auto find_layer_result = std::find_if(layers.begin(), layers.end(), [&](VkLayerProperties& l) {
        return std::string(l.layerName) == name;
    });

    if (find_layer_result == layers.end()) {
        return false;
    }
    else {
        return true;
    }
}

void create_instance() {
    for (int i = 0; i < layer_count; ++i) {
        if (!layer_supported(std::string(layer_names[i]))) {
            std::cout << "layer " << layer_names[i] << " not supported!" << std::endl;
            exit(1);
        }
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "";
    app_info.pEngineName = "OTCVulkan";
    app_info.apiVersion = VK_API_VERSION_1_3;

    uint32_t window_extension_count = 0;
    const char** window_extensions = nullptr;
#if OTCV_WINDOW == GLFW
    // glfw extension
    window_extensions = glfwGetRequiredInstanceExtensions(&window_extension_count);
#endif

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = window_extension_count;
    create_info.ppEnabledExtensionNames = window_extensions;
    g_instance.reset(new Instance(create_info));
}

void create_surface(void* window) {
    g_surface.reset(new Surface(window));
}

// check device's support for queue family.
// Return queue family index if certain family is supported
/// Return -1 if not
uint32_t queue_family_support(VkQueueFlagBits flag, VkPhysicalDevice device) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    if (queue_family_count == 0) {
        return -1;
    }

    std::vector<VkQueueFamilyProperties> props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, props.data());

    for (uint32_t i = 0; i < props.size(); ++i) {
        if (props[i].queueFlags & flag) {
            return i;
        }
    }

    return -1;
}

void pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_instance->vk_instance, &device_count, nullptr);

    if (device_count == 0) {
        std::cout << "No physical device found!" << std::endl;
        exit(1);
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(g_instance->vk_instance, &device_count, devices.data());

    for (auto& d : devices) {
        VkPhysicalDeviceProperties prop;
        VkPhysicalDeviceFeatures feat;

        vkGetPhysicalDeviceProperties(d, &prop);
        vkGetPhysicalDeviceFeatures(d, &feat);

        // Check if device is a GPU
        if (!(prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
            prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) {
            continue;
        }

        // TODO: Check all device features. See how features and extensions are initialized in create_device()
        //if (feat.samplerAnisotropy == VK_FALSE) {
        //    continue;
        //}
            //VkPhysicalDeviceDynamicRenderingFeatures featuresCheck{};
    //featuresCheck.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

    //VkPhysicalDeviceFeatures2 features2{};
    //features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    //features2.pNext = &featuresCheck;

    //vkGetPhysicalDeviceFeatures2(g_physical_device.vk_physical_device, &features2);

    //if (featuresCheck.dynamicRendering) {
    //    std::cout << "Dynamic rendering is enabled.\n";
    //}
    //else {
    //    std::cout << "Dynamic rendering is NOT enabled!\n";
    //}

        // Check graphics queue family
        uint32_t& index = g_physical_device.queue_family_index;
        if ((index = queue_family_support(VkQueueFlagBits(queue_required), d)) == -1) {
            continue;
        }

        // Check if the selected queue family supports presentation
        // Normally it would
        VkBool32 present_supported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, index, g_surface->vk_surface, &present_supported);
        if (!present_supported) {
            std::cout << "queue familt index " << index << " does not support presentation queue" << std::endl;
            exit(1);
        }

        g_physical_device.vk_physical_device = d;
        break;
    }
}

void create_device() {
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = g_physical_device.queue_family_index;
    queue_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_feature{};
    device_feature.samplerAnisotropy = VK_TRUE;
    device_feature.fillModeNonSolid = VK_TRUE;
    device_feature.multiDrawIndirect = VK_TRUE;

    VkPhysicalDeviceVulkan11Features v11_feature{};
    v11_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    v11_feature.shaderDrawParameters = true;

    // for descriptor indexing (bindless),
    // flags listed in the first column need to be enabled when creating descriptor sets and pools.
    // To be able to use these flags, the features listed in the second column have to be enabled.
    // VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT                    descriptorBindingPartiallyBound
    // VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT          descriptorBindingVariableDescriptorCount
    // VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT                  descriptorBindingUniformBufferUpdateAfterBind(for ubo), descriptorBindingSampledImageUpdateAfterBind(for image sampler) 
    VkPhysicalDeviceVulkan12Features v12_feature{};
    v12_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12_feature.pNext = &v11_feature;
    v12_feature.drawIndirectCount = VK_TRUE;
    v12_feature.scalarBlockLayout = VK_TRUE;
    v12_feature.descriptorIndexing = VK_TRUE;
    v12_feature.runtimeDescriptorArray = VK_TRUE;
    v12_feature.descriptorBindingPartiallyBound = VK_TRUE;
    v12_feature.descriptorBindingVariableDescriptorCount = VK_TRUE;
    v12_feature.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    v12_feature.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    v12_feature.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    v12_feature.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceVulkan13Features v13_feature{};
    v13_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    v13_feature.pNext = &v12_feature;
    v13_feature.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures2 device_feature_2{};
    device_feature_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    device_feature_2.pNext = &v13_feature;
	device_feature_2.features = device_feature;

    std::vector<const char*> non_core_device_extension_names = { 
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
    };

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &device_feature_2;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledLayerCount = layer_count;
    device_info.ppEnabledLayerNames = layer_names;
    device_info.enabledExtensionCount = non_core_device_extension_names.size();
    device_info.ppEnabledExtensionNames = non_core_device_extension_names.data(); // assume physical device supports this
    // device_info.pEnabledFeatures = &feat_info

    g_device.reset(new Device(device_info));
}

void get_queue() {
    vkGetDeviceQueue(g_device->vk_device, g_physical_device.queue_family_index, 0, &g_queue.vk_queue);
}

bool check_surface_format(VkSurfaceFormatKHR format) {
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &format_count, nullptr);
    if (format_count == 0) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &format_count, formats.data());
    for (const auto& f : formats) {
        if (f.format == format.format && f.colorSpace == format.colorSpace) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(g_physical_device.vk_physical_device, format.format, &props);
            // We check the optimal tiling properties.
            // A surface image's parameters:
            // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCreateSwapchainKHR.html#_description
            if (props.optimalTilingFeatures & swapchain_format_features) {
                return true;
            }
        }
    }

    return false;
}

bool check_present_mode(VkPresentModeKHR mode) {
    uint32_t mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &mode_count, nullptr);
    if (mode_count == 0) {
        return false;
    }
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &mode_count, modes.data());
    for (const auto& m : modes) {
        if (m == mode) {
            return true;
        }
    }

    return false;
}

void create_swapchain(void* window_data) {
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device.vk_physical_device, g_surface->vk_surface, &surface_caps);

    if (!check_surface_format(surface_format)) {
        std::cout << "Surface format not supported" << std::endl;
        exit(1);
    }

    if (!check_present_mode(present_mode)) {
        std::cout << "Present mode not supported" << std::endl;
        exit(1);
    }

    // Get the window size, measured in pixels
    // WIDTH and HEIGHT values set at glfw initialization are measured in screen coordinates.
    // Screen coordinates and pixels may not be the same on MacOS. Very likely to take the same value on Windows.
    int width_pixels;
    int height_pixels;
#if OTCV_WINDOW == GLFW
    glfwGetFramebufferSize((GLFWwindow*)window_data, &width_pixels, &height_pixels);
#else
    std::cout << "Window systems other that glfw not supported" << std::endl;
    exit(1);
#endif

    VkExtent2D window_extent;
    // TODO: windows extent may not be of the exact value as that of window dimensions. Deal with this
    window_extent.width = std::clamp((uint32_t)width_pixels, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
    window_extent.height = std::clamp((uint32_t)height_pixels, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);
    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = g_surface->vk_surface;
    if (surface_caps.maxImageCount == 0) {
        create_info.minImageCount = surface_caps.minImageCount + 1;
    }
    else {
        create_info.minImageCount = std::min(surface_caps.minImageCount + 1, surface_caps.maxImageCount);
    }
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = window_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = swapchain_image_usage;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // because graphics and presentation queue families are the same.
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;
    create_info.preTransform = surface_caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    g_swapchain.reset(new Swapchain(create_info));
}

Context create_context(void* window_data) {
    create_instance();
    create_surface(window_data);
    pick_physical_device();
    if (g_physical_device.vk_physical_device == VK_NULL_HANDLE) {
        std::cout << "Cannot find suitable physical device" << std::endl;
    }

    create_device();
    get_queue();
    create_swapchain(window_data);
    CommandPool::create(true, true, false);

    Context ctx;
    ctx.instance = g_instance.get();
    ctx.surface = g_surface.get();
    ctx.physical_device = &g_physical_device;
    ctx.device = g_device.get();
    ctx.queue = &g_queue;
    ctx.command_pool = g_command_pool.get();
    ctx.swapchain = g_swapchain.get();

    return ctx;
}

Context get_context() {
    Context ctx;
    ctx.instance = g_instance.get();
    ctx.surface = g_surface.get();
    ctx.physical_device = &g_physical_device;
    ctx.device = g_device.get();
    ctx.queue = &g_queue;
    ctx.command_pool = g_command_pool.get();
    ctx.swapchain = g_swapchain.get();
    
    return ctx;
}

void destroy_context() {
    g_user_shader_modules.clear();
    g_user_buffers.clear();
    g_user_images.clear();
    g_user_render_passes.clear();
    g_user_command_pools.clear();
    g_user_semaphores.clear();
    g_user_fences.clear();
    g_user_vertex_buffers.clear();
    g_user_compute_pipelines.clear();
    g_user_graphics_pipelines.clear();
    g_user_descriptor_pools.clear();
    g_user_samplers.clear();
    g_user_framebuffers.clear();
}

}