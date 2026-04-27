#pragma once

#include "frame_graph.hpp"

#include <GLFW/glfw3.h>

namespace otcv {
namespace fg {

class Application {
public:
	struct Config {
		// we call this desired because glfw framebuffer size, which is the size of swapchain images, may be different from window size
		int desired_window_width;
		int desired_window_height;
	};
	Application(const Config& config);

	~Application();

	uint32_t n_frames_in_flight() {
		return _otcv_context.swapchain->images.size();
	}

	GLFWwindow* window() {
		return _window;
	}

	otcv::Context otcv_context() {
		return _otcv_context;
	}

	std::shared_ptr<FrameGraph> framegraph() {
		return fg;
	}

	uint32_t current_frame() {
		return _current_frame;
	}

	typedef std::function<void(uint32_t)> SyncFrameUpdateFunc;
	void synchronized_frame_update(SyncFrameUpdateFunc frame_update) {
		this->frame_update_cb = frame_update;
	}
	
	typedef std::function<void(fg::Application*)> FGBuildFunc;
	void register_framegraph_rebuild(FGBuildFunc fg_build) {
		this->fg_build_cb = fg_build;
		fg_need_rebuild = true;
	}

	void run();
	
private:
	void init_window(int width, int height);
	void init_vulkan_context();
	void init_frame_contexts();
	void draw_frame();
	void build_framegraph();
	void copy_backbuffer_commands(otcv::CommandBuffer* cmd_buf, fg::PhysicalImagePtr backbuffer, uint32_t image_id);

	// Boilerplate stuff
	GLFWwindow* _window = nullptr;
	otcv::Context _otcv_context;

	std::shared_ptr<fg::TransientImageCache> img_allocator = nullptr;
	std::shared_ptr<fg::TransientBufferCache> buf_allocator = nullptr;
	std::shared_ptr<fg::FrameGraph> fg = nullptr;
	std::vector<fg::FrameGraph::FrameRecordInput> fg_record_inputs;

	// int swapchain_width = -1;
	// int swapchain_height = -1;

	struct FrameContext {
		// synchronization
		otcv::Fence* frame_fence = nullptr;
		otcv::Semaphore* image_available_semaphore = nullptr;
		otcv::Semaphore* transfer_finished_semaphore = nullptr;

		// command buffers
		otcv::CommandPool* copy_command_pool = nullptr;
		otcv::CommandBuffer* copy_command_buffer;
	};
	std::vector<FrameContext> frame_ctxs;

	SyncFrameUpdateFunc frame_update_cb = nullptr;
	FGBuildFunc fg_build_cb = nullptr;
	bool fg_need_rebuild = false;

	uint32_t _current_frame = 0;
};

}
}