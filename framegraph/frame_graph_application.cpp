#include "frame_graph_application.h"

#include <iostream>
#include <chrono>
#include <thread>

#include "frame_graph.hpp"
#include "otcv.h"
#include "otcv_utils.h"


namespace otcv {
namespace fg {

Application::Application(const Config& config) {
    init_window(config.desired_window_width, config.desired_window_height);
    init_vulkan_context();
    init_frame_contexts();
}

Application::~Application() {
    // TODO: reciprocate constructor
    otcv::destroy_context();
    glfwDestroyWindow(_window);
    glfwTerminate();
}

bool window_minimized = false;
void window_iconified_callback(GLFWwindow* window, int iconified) {
    if (iconified == GLFW_TRUE) {
        // minimized
        window_minimized = true;
    }
    else {
        // restored
        window_minimized = false;
    }
}

void Application::init_window(int width, int height) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    _window = glfwCreateWindow(width, height, "Framegraph Demo", nullptr, nullptr);
    glfwSetWindowUserPointer(_window, this);

    // handle minimize/restore events
    glfwSetWindowIconifyCallback(_window, window_iconified_callback);
}

void Application::init_vulkan_context() {
    _otcv_context = otcv::create_context(_window);
    for (uint32_t i = 0; i < _otcv_context.swapchain->images.size(); ++i) {
        _otcv_context.swapchain->mock_image(i)->initialize_state(otcv::ResourceState::PresentReady);
    }
}

void Application::init_frame_contexts() {
    frame_ctxs.resize(_otcv_context.swapchain->mock_images.size());
    for (FrameContext& ctx : frame_ctxs) {
        ctx.frame_fence = Fence::create();
        ctx.image_available_semaphore = Semaphore::create();
        ctx.transfer_finished_semaphore = Semaphore::create();
        ctx.copy_command_pool = CommandPool::create(true, false); // this command pool will reset every frame for the framegraph
        ctx.copy_command_buffer = ctx.copy_command_pool->allocate();
    }
}

void Application::run() {
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();
        if (!window_minimized) {
            draw_frame();
            _current_frame = (_current_frame + 1) % frame_ctxs.size();
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    vkDeviceWaitIdle(_otcv_context.device->vk_device);
}

void Application::build_framegraph() {
    img_allocator.reset(new fg::TransientImageCache);
    // img_allocator->set_expire_interval(30);
    buf_allocator.reset(new fg::TransientBufferCache);
    // buf_allocator->set_expire_interval(30);
    fg.reset(new fg::FrameGraph(img_allocator, buf_allocator));

    fg_build_cb(this);

    std::pair<bool, std::vector<fg::FrameGraph::FrameRecordInput>> compile_result =
        std::move(fg->compile(_otcv_context.swapchain->images.size(), ResourceState::TransferSrc));

    if (!compile_result.first) {
        assert(false);
        return;
    }
    fg_record_inputs = compile_result.second; // shouldnt use move. As move does not destroy shared_ptr memory
}

void Application::draw_frame() {
    FrameContext& f = frame_ctxs[_current_frame];
    f.frame_fence->wait();

    if (frame_update_cb) {
        frame_update_cb(_current_frame);
    }

    if (fg_need_rebuild) {
        fg_need_rebuild = false;
        vkDeviceWaitIdle(_otcv_context.device->vk_device);
        build_framegraph();
    }

    if (!fg) {
        assert(fg_record_inputs.size() == 0);
        f.frame_fence->reset();
        // empty submit
        otcv::QueueSubmit submit;
        submit.signal(f.frame_fence);
        _otcv_context.queue->submit(submit);
        return;
    }

    f.copy_command_pool->reset();

    uint32_t swapchain_image_id = 0;
    VkResult acquire_result = vkAcquireNextImageKHR(
        _otcv_context.device->vk_device,
        _otcv_context.swapchain->vk_swapchain, UINT64_MAX,
        f.image_available_semaphore->vk_semaphore, VK_NULL_HANDLE,
        &swapchain_image_id);
    if (acquire_result != VK_SUCCESS) {
        std::cout << "Unrecognized acquire error. error code = " << acquire_result << std::endl;
        return;
    }

    if (!fg->record(fg_record_inputs[_current_frame])) {
        assert(false);
        return;
    }

    // reset frame fence only after framegraph record.
    // Because FrameGraph::record() will try acquire from transient resource caches, which rely on fence state to tell if a piece of resource can be reused.
    f.frame_fence->reset();

    // put framegraph commands in a batch
    otcv::QueueSubmit submit;
    otcv::QueueSubmit::Batch& batch = submit.batch();
    for (auto pass_input : fg_record_inputs[_current_frame].ordered_passes) {
        batch.add_command_buffer(pass_input.cmd_buf);
    }
    batch.end();

    // blit :
    /*
    * commands: (look out for swapchain image index)
    *	backbuffer image state transition
    *	swapchain image state transition
    *	copy command
    *	swapchain image state transition
    *
    * submit:
    *	wait on image finished semaphore, at transfer stage
    *	signal the transfer finished semaphore
    *	signal the frame fence
    */

    // put backbuffer copy commands in a batch
    f.copy_command_buffer->record(std::bind(&Application::copy_backbuffer_commands, this, std::placeholders::_1, fg->backbuffer(), swapchain_image_id));
    submit.batch()
        .add_command_buffer(f.copy_command_buffer)
        .add_wait(f.image_available_semaphore, VK_PIPELINE_STAGE_TRANSFER_BIT)
        .add_signal(f.transfer_finished_semaphore)
        .end()
        .signal(f.frame_fence);

    // submit all
    _otcv_context.queue->submit(submit);

    otcv::QueuePresent present;
    present
        .image_index(swapchain_image_id)
        .add_wait(f.transfer_finished_semaphore);
    VkResult present_result = _otcv_context.queue->present(present);

    // window resize event
    // at least this works on vulkan 1.3, rtx 5050.
    // Other dirvers may return this error at acquire next image or may not return at all.
    // If that happens we may need to handle resize in glfw resize callback
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(_otcv_context.device->vk_device);
        _otcv_context.swapchain->recreate(_window);
        for (uint32_t i = 0; i < _otcv_context.swapchain->images.size(); ++i) {
            _otcv_context.swapchain->mock_image(i)->initialize_state(otcv::ResourceState::PresentReady);
        }
        build_framegraph();
        return; // reset framegraph and img/buf allocators. Return immediately. No more operations for this frame from this point on
    }
    else if (present_result != VK_SUCCESS) {
        std::cout << "Unrecognized present error. error code = " << present_result << std::endl;
        assert(false);
        return;
    }

    img_allocator->end_frame_recording(f.frame_fence);
    buf_allocator->end_frame_recording(f.frame_fence);
    std::cout << "img_allocator capacity = " << img_allocator->capacity() << ", size = " << img_allocator->alive_count() << std::endl;
    std::cout << "buf_allocator capacity = " << buf_allocator->capacity() << ", size = " << buf_allocator->alive_count() << std::endl;
}

void Application::copy_backbuffer_commands(otcv::CommandBuffer* cmd_buf, fg::PhysicalImagePtr backbuffer, uint32_t image_id) {
    if (!backbuffer) {
        return;
    }

    // backbuffer barrier
    cmd_buf->cmd_image_memory_barrier(
        backbuffer->resource,
        backbuffer->state,
        otcv::ResourceState::TransferSrc);
    backbuffer->state = otcv::ResourceState::TransferSrc;

    // swapchain image barrier
    cmd_buf->cmd_image_memory_barrier(
        _otcv_context.swapchain->mock_image(image_id),
        otcv::ResourceState::PresentAvailableForTransferDst,
        otcv::ResourceState::TransferDst);

    ImageCopy region;
    uint32_t width = _otcv_context.swapchain->image_info.extent.width;
    uint32_t height = _otcv_context.swapchain->image_info.extent.height;
    region.extent(width, height);
    cmd_buf->cmd_image_copy(backbuffer->resource, _otcv_context.swapchain->mock_image(image_id), region);

    // swapchain image barrier
    cmd_buf->cmd_image_memory_barrier(
        _otcv_context.swapchain->mock_image(image_id),
        otcv::ResourceState::TransferDst,
        otcv::ResourceState::PresentReady);
}

}
}
