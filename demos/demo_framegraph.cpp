#include <iostream>
#include <chrono>
#include <thread>
#include <GLFW/glfw3.h>

#include "frame_graph.hpp"
#include "otcv.h"
#include "otcv_utils.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include "assets.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

const int init_window_width = 960;
const int init_window_height = 480;

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

//int window_width = init_window_width;
//int window_height = init_window_height;
//void window_resize_callback(GLFWwindow* window, int width, int height) {
//    window_width = width;
//    window_height = height;
//}

using namespace otcv;

struct PerspectiveCamera {
    PerspectiveCamera() {};
    PerspectiveCamera(
        glm::vec3 eye,
        glm::vec3 center,
        glm::vec3 up,
        float near,
        float far,
        float fov,
        float aspect) {
        this->eye = eye;
        this->center = center;
        glm::vec3 front = center - eye;
        glm::vec3 right = glm::cross(front, up);
        this->up = glm::normalize(glm::cross(right, front));
        this->near = near;
        this->far = far;
        this->fov = fov;
        this->aspect = aspect;
        this->view = update_view();
        this->proj = update_proj();
    }

    glm::mat4 update_view() {
        view = glm::lookAtRH(eye, center, up);
        return view;
    }

    glm::mat4 update_proj() {
        proj = glm::perspectiveRH_ZO(fov, aspect, near, far);
        proj[1][1] *= -1.0f;
        return proj;
    }

    glm::vec3 eye;
    glm::vec3 center;
    glm::vec3 up;
    float near;
    float far;
    float fov;
    float aspect;
    glm::mat4 view;
    glm::mat4 proj;
};

class Application {
public:
    void run() {
        init_window();
        init_vulkan_context();
        init_imgui();
        load_shaders();
        init_computes();
        init_desc_pool();
        init_objects();
        init_frame_contexts();
        configure_framegraph();
        main_loop();
    }

    void draw_frame() {

        FrameContext& f = frame_ctxs[current_frame];
        f.frame_fence->wait();

        update_camera();

        immediate_gui();
        ImGui_ImplOTCV_BuildBuffers(imgui_meshes[current_frame].vb, imgui_meshes[current_frame].ib);

        if (framegraph_rebuid) {
            vkDeviceWaitIdle(_otcv_context.device->vk_device);
            configure_framegraph();
        }


        f.command_pool->reset();

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

        if (!fg->record(fg_record_inputs[current_frame])) {
            return;
        }


        // reset frame fence only after framegraph record.
        // Because FrameGraph::record() will try acquire from transient resource caches, which rely on fence state to tell if a piece of resource can be reused.
        f.frame_fence->reset();

        // put framegraph commands in a batch
        otcv::QueueSubmit submit;
        otcv::QueueSubmit::Batch& batch = submit.batch();
        for (auto pass_input : fg_record_inputs[current_frame].ordered_passes) {
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
            window_width = _otcv_context.swapchain->image_info.extent.width;
            window_height = _otcv_context.swapchain->image_info.extent.height;
            configure_framegraph();
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

    void copy_backbuffer_commands(otcv::CommandBuffer* cmd_buf, fg::PhysicalImagePtr backbuffer, uint32_t image_id) {
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
        region.extent(window_width, window_height);
        cmd_buf->cmd_image_copy(backbuffer->resource, _otcv_context.swapchain->mock_image(image_id), region);

        // swapchain image barrier
        cmd_buf->cmd_image_memory_barrier(
            _otcv_context.swapchain->mock_image(image_id),
            otcv::ResourceState::TransferDst,
            otcv::ResourceState::PresentReady);
    }

    void immediate_gui() {
        ImGuiIO& io = ImGui::GetIO();
        
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::Begin("Framegraph Configuration");

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            if (ImGui::Button("Rebuild")) {
                framegraph_rebuid = true;
            }
            else {
                framegraph_rebuid = false;
            }
            
            ImGui::Checkbox("Blend", &fg_config.blend);
            ImGui::Checkbox("Billow", &fg_config.billow);

            ImGui::End();
        }
        ImGui::Render();
    }

    void update_camera() {
        cam.aspect = (float)window_width / (float)window_height;
        cam.update_view();
        cam.update_proj();
    }

    void main_loop() {
        while (!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            if (!window_minimized) {
                draw_frame();
                current_frame = (current_frame + 1) % frame_ctxs.size();
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        vkDeviceWaitIdle(_otcv_context.device->vk_device);
    }

    void cleanup() {
        ImGui_ImplOTCV_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        otcv::destroy_context();
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    void init_window() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        
        _window = glfwCreateWindow(init_window_width, init_window_height, "Framegraph Demo", nullptr, nullptr);
        glfwSetWindowUserPointer(_window, this);

        // handle minimize/restore events
        glfwSetWindowIconifyCallback(_window, window_iconified_callback);
        // handle window resize events
        // glfwSetWindowSizeCallback(_window, window_resize_callback);
    }

    void init_vulkan_context() {
        _otcv_context = otcv::create_context(_window);
        for (uint32_t i = 0; i < _otcv_context.swapchain->images.size(); ++i) {
            _otcv_context.swapchain->mock_image(i)->initialize_state(otcv::ResourceState::PresentReady);
        }
        // initialize window height and width here because glfw framebuffer size, which is the size of swapchain images, may be different from window size
        window_width = _otcv_context.swapchain->image_info.extent.width;
        window_height = _otcv_context.swapchain->image_info.extent.height;
    }

    void init_imgui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // io.DisplaySize = ImVec2(init_window_width, init_window_height);
        io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
        ImGui::StyleColorsDark();
        
        ImGui_ImplGlfw_InitForVulkan(_window, true);
        
        ImGui_ImplOTCV_InitInfo info;
        info.queue = _otcv_context.queue;
        // can also add font here
        info.target_format = _otcv_context.swapchain->image_info.format;
        ImGui_ImplOTCV_Init(&info);

        imgui_meshes.resize(_otcv_context.swapchain->images.size());
        for (auto& mesh : imgui_meshes) {
            ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
            VertexBufferBuilder vb_builder = bd->vertex_buffer->builder;
            mesh.vb = vb_builder.build();
            BufferBuilder ib_builder = bd->index_buffer->builder;
            mesh.ib = ib_builder.build();
        }
    }

    void load_shaders() {
        shader_blob = otcv::load_shaders_from_dir("./spirv/");
    }

    void init_desc_pool() {
        desc_pool.reset(new NaiveExpandableDescriptorPool);
    }

    void init_objects() {
        VertexBufferBuilder pos3norm3uv2vbb = VertexBufferBuilder()
            .add_binding()
            .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))    // position
            .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))    // normal
            .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));      // uv
        // one-sides graphics pipeline
        GraphicsPipeline* one_sided = otcv::GraphicsPipelineBuilder()
            .pipline_rendering()
                .add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
                .depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
            .end()
            .shader_vertex(shader_blob.at("blinn_phong.vert"))
            .shader_fragment(shader_blob.at("blinn_phong.frag"))
            .vertex_state(pos3norm3uv2vbb)
            .cull_back_face()
            .depth_test()
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
            .build();
        // double-sided graphics pipeline
        GraphicsPipeline* double_sided = otcv::GraphicsPipelineBuilder()
            .pipline_rendering()
                .add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
                .depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
            .end()
            .shader_vertex(shader_blob.at("blinn_phong.vert"))
            .shader_fragment(shader_blob.at("blinn_phong.frag"))
            .vertex_state(pos3norm3uv2vbb)
            .depth_test()
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
            .build();

        VertexBufferBuilder pos3uv2vbb = VertexBufferBuilder()
            .add_binding()
            .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))    // position
            .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));      // uv
        // transparent overlay pipeline
        GraphicsPipeline* transparent_overlay = otcv::GraphicsPipelineBuilder()
            .pipline_rendering()
                .add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
            .end()
            .shader_vertex(shader_blob.at("screen_quad.vert"))
            .shader_fragment(shader_blob.at("screen_quad_alpha.frag"))
            .vertex_state(pos3uv2vbb)
            .blend_attachment(0).end() // default alpha blend
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
            .build();
        // tone mapping graphics pipeline
        GraphicsPipeline* tone_mapping = otcv::GraphicsPipelineBuilder()
            .pipline_rendering()
                .add_color_attachment_format(_otcv_context.swapchain->image_info.format)
            .end()
            .shader_vertex(shader_blob.at("screen_quad.vert"))
            .shader_fragment(shader_blob.at("tone_mapping.frag"))
            .vertex_state(pos3uv2vbb)
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
            .build();

        // UBO alignment
        Std140AlignmentType ubo_alignment;
        ubo_alignment.add(Std140AlignmentType::InlineType::Mat4, "model");
        ubo_alignment.add(Std140AlignmentType::InlineType::Mat4, "projView");
        
        // samplers
        Sampler* linear_sampler = SamplerBuilder().build();
        Sampler* nearest_sampler = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();

        // ribbon
        {
            ribbon.vb = VertexBufferBuilder()
                .add_binding(BufferBuilder()
                    .size(kRibbonVertexCount * (sizeof(glm::vec3) * 2 + sizeof(glm::vec2)))
                    .usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                    .host_access(BufferBuilder::Access::Invisible), kRibbonVertices)
                .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3)) // position
                .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3)) // normal
                .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2)) // uv
                .build();
            ribbon.ib = BufferBuilder()
                .size(kRibbonIndexCount * sizeof(uint16_t))
                .usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                .host_access(BufferBuilder::Access::Invisible)
                .build();
            ribbon.ib->populate(kRibbonIndices);
            ribbon.graphics = double_sided;
            ribbon.model_mat =
                glm::translate(glm::mat4(1.0f), { 2.0f, 2.0f, 0.0f }) *
                glm::rotate(glm::mat4(1.0f), glm::pi<float>() / 6.0f, { 0.0f, -1.0f, 0.0f }) *
                glm::scale(glm::mat4(1.0f), {1.5f, 1.0f, 1.0f});
            ribbon.ubo.reset(new StaticUBO(ubo_alignment));
            ribbon.ubo->set(StaticUBOAccess()["model"], &ribbon.model_mat);
            glm::mat4 pv = cam.proj * cam.view;
            ribbon.ubo->set(StaticUBOAccess()["projView"], &pv);
            ribbon.graphics_desc_set = desc_pool->allocate(ribbon.graphics->desc_set_layouts[0]);
            ribbon.graphics_desc_set->bind_buffer(0, ribbon.ubo->_buf);
            ribbon.graphics_desc_set->bind_sampler(1, &linear_sampler);
            ribbon.compute_desc_set = desc_pool->allocate(comp_height_displacement->desc_set_layouts[0]);
            ribbon.compute_desc_set->bind_sampler(0, &linear_sampler);
            // ribbon.compute_desc_set->bind_buffer(1, ribbon.vb->buffers[0]);

            ribbon.use_solid_color = 1;
            ribbon.solid_color = glm::vec3(0.878, 0.239, 0.573);
            ribbon.double_sided = 1;
            ribbon.opacity = 1.0f;
        }
        // ground
        {
            ground.vb = VertexBufferBuilder()
                .add_binding(BufferBuilder()
                    .size(kBoxVertexCount * (sizeof(glm::vec3) * 2 + sizeof(glm::vec2)))
                    .usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
                    .host_access(BufferBuilder::Access::Invisible), kBoxVertices)
                .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3)) // position
                .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3)) // normal
                .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2)) // uv
                .build();

            ground.ib = BufferBuilder()
                .size(kBoxIndexCount * sizeof(uint16_t))
                .usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                .host_access(BufferBuilder::Access::Invisible)
                .build();
            ground.ib->populate(kBoxIndices);
            ground.graphics = one_sided;
            ground.model_mat =
                glm::translate(glm::mat4(1.0f), { 0.0f, -0.15f, 0.0f }) *
                glm::scale(glm::mat4(1.0f), { 10.0f, 0.3f, 10.0f });
            ground.ubo.reset(new StaticUBO(ubo_alignment));
            ground.ubo->set(StaticUBOAccess()["model"], &ground.model_mat);
            glm::mat4 pv = cam.proj * cam.view;
            ground.ubo->set(StaticUBOAccess()["projView"], &pv);
            ground.graphics_desc_set = desc_pool->allocate(ground.graphics->desc_set_layouts[0]);
            ground.graphics_desc_set->bind_buffer(0, ground.ubo->_buf);
            ground.graphics_desc_set->bind_sampler(1, &linear_sampler);

            ground.use_solid_color = 1;
            ground.solid_color = glm::vec3(0.224, 0.412, 0.231);
            ground.double_sided = 0;
            ground.opacity = 1.0f;
        }
        // screen quads
        {
            screen_quad_overlay.vb = screen_quad_ndc();
            screen_quad_overlay.graphics = transparent_overlay;
            screen_quad_overlay.graphics_desc_set = desc_pool->allocate(screen_quad_overlay.graphics->desc_set_layouts[0]);
            screen_quad_overlay.graphics_desc_set->bind_sampler(0, &linear_sampler);
        }
        {
            screen_quad_tonemap.vb = screen_quad_ndc();
            screen_quad_tonemap.graphics = tone_mapping;
            screen_quad_tonemap.graphics_desc_set = desc_pool->allocate(screen_quad_tonemap.graphics->desc_set_layouts[0]);
            screen_quad_tonemap.graphics_desc_set->bind_sampler(0, &nearest_sampler);
        }
    }

    void init_computes() {
        comp_animate_vein = otcv::ComputePipeline::create(shader_blob.at("animated_vein_texture.comp"));
        comp_animate_billow = otcv::ComputePipeline::create(shader_blob.at("animated_billow_texture.comp"));
        comp_height_displacement = otcv::ComputePipeline::create(shader_blob.at("grayscale_height_displacement_z.comp"));
    }

    void configure_framegraph() {
        img_allocator.reset(new fg::TransientImageCache);
        // img_allocator->set_expire_interval(30);
        buf_allocator.reset(new fg::TransientBufferCache);
        // buf_allocator->set_expire_interval(30);
        fg.reset(new fg::FrameGraph(img_allocator, buf_allocator));

        // resources
        fg::ResourceHandle animated_vein_texture = fg->add_resource("AnimatedVeinTexture",
            ImageBuilder()
            .size(512, 512, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM));

        fg::ResourceHandle animated_billow_texture = fg->add_resource("AnimatedBillowTexture",
            ImageBuilder()
            .size(64, 64, 1)
            .format(VK_FORMAT_R32_SFLOAT));

        fg::ResourceHandle vertices_base = fg->add_resource("VerticesBase",
            BufferBuilder()
            .size(ribbon.vb->buffers[0]->builder._info.size)
            .host_access(otcv::BufferBuilder::Access::Invisible));
        
        fg::ResourceHandle vertices_displaced = fg->add_resource("DisplacedVertices",
            BufferBuilder()
            .size(ribbon.vb->buffers[0]->builder._info.size)
            .host_access(otcv::BufferBuilder::Access::Invisible));

        fg::ResourceHandle depth = fg->add_resource("Depth",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

        fg::ResourceHandle opaque = fg->add_resource("Opaque",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));

        fg::ResourceHandle blended = fg->add_resource("blended",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));

        fg::ResourceHandle tonemapped = fg->add_resource("Tonemapped",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(_otcv_context.swapchain->image_info.format));  // this is going to be copied to swapchain

        fg::ResourceHandle ui_overlayed = fg->add_resource("UIOverlayed",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(_otcv_context.swapchain->image_info.format));

        // passes
        fg::Pass& ui_overlay_pass = fg->add_pass("UIOverlayPass", fg::PassType::Graphics);
        ui_overlay_pass.access(fg::ResourceAccessType::ColorInOut, tonemapped, ui_overlayed);
        ui_overlay_pass.store_load_func(tonemapped, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
        });
        ui_overlay_pass.render_area_func([&](RenderingBegin& begin) {
            begin.area(window_width, window_height);
        });
        ui_overlay_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            ImGui_ImplOTCV_Exec(cmd, imgui_meshes[current_frame].vb, imgui_meshes[current_frame].ib);
        });

        fg::Pass& billow_texture_animation_pass = fg->add_pass("BillowTextureAnimation", fg::PassType::Compute);
        billow_texture_animation_pass.access(fg::ResourceAccessType::StorageImageOut, animated_billow_texture);
        billow_texture_animation_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            float time = now / 1000.0f;
            cmd->cmd_bind_compute_pipeline(comp_animate_billow);
            cmd->cmd_bind_descriptor_set(comp_animate_billow, ctx.desc_set, 2);
            cmd->cmd_push_constant(comp_animate_billow, "t", &time);
            cmd->cmd_dispatch(calc_group_count(64, 16), calc_group_count(64, 16)); // local size 16. Hardcoded 512 for the moment
        });

        fg::Pass& vertex_copy_pass = fg->add_pass("VertexCopyPass", fg::PassType::Transfer);
        vertex_copy_pass.access(fg::ResourceAccessType::TransferOut, vertices_base);
        vertex_copy_pass.pre_pass_func([&](CommandBuffer* cmd) {
            cmd->cmd_buffer_memory_barrier(ribbon.vb->buffers[0], ResourceState::Created, ResourceState::TransferSrc); // will get called every frame
        });
        vertex_copy_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_copy_buffer(ribbon.vb->buffers[0], ctx.transfer_bufs[0]);
        });
        vertex_copy_pass.post_pass_func([&](CommandBuffer* cmd) {
            // we dont care about what's going to happen to vertex buffer at this point
        });

        fg::Pass& height_pass = fg->add_pass("HeightDisplacement", fg::PassType::Compute);
        height_pass.access(fg::ResourceAccessType::TextureIn, animated_billow_texture);
        height_pass.access(fg::ResourceAccessType::SSBOInOut, vertices_base, vertices_displaced);
        height_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_bind_compute_pipeline(comp_height_displacement);
            cmd->cmd_bind_descriptor_set(comp_height_displacement, ribbon.compute_desc_set, 0);
            cmd->cmd_bind_descriptor_set(comp_height_displacement, ctx.desc_set, 2);
            float max_displacement = 0.2f;
            glm::vec2 patch_size = glm::vec2(1.0f);
            glm::ivec2 grid_count = glm::ivec2(kRibbonGridCount);
            int vertex_stride = 8;
            int pos_attr_offset = 0;
            int normal_attr_offset = 3;
            cmd->cmd_push_constant(comp_height_displacement, "maxDisplacement", &max_displacement);
            cmd->cmd_push_constant(comp_height_displacement, "patchSize", &patch_size);
            cmd->cmd_push_constant(comp_height_displacement, "gridCount", &grid_count);
            cmd->cmd_push_constant(comp_height_displacement, "vertexStride", &vertex_stride);
            cmd->cmd_push_constant(comp_height_displacement, "positionAttributeOffset", &pos_attr_offset);
            cmd->cmd_push_constant(comp_height_displacement, "normalAttributeOffset", &normal_attr_offset);
            cmd->cmd_dispatch(calc_group_count(kRibbonGridCount + 1, 16), calc_group_count(kRibbonGridCount + 1, 16)); // local size 16. Hardcoded 512 for the moment
        });

        fg::Pass& blend_pass = fg->add_pass("BlendPass", fg::PassType::Graphics);
        blend_pass.access(fg::ResourceAccessType::TextureIn, animated_vein_texture);
        blend_pass.access(fg::ResourceAccessType::ColorInOut, opaque, blended);
        blend_pass.store_load_func(opaque, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
        });
        blend_pass.render_area_func([&](RenderingBegin& begin) {
            begin.area(window_width, window_height);
        });
        blend_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_bind_graphics_pipeline(screen_quad_overlay.graphics);
            float alpha = 0.2f;
            cmd->cmd_push_constant(screen_quad_overlay.graphics, "alpha", &alpha);
            cmd->cmd_bind_descriptor_set(screen_quad_overlay.graphics, screen_quad_overlay.graphics_desc_set, 0);
            cmd->cmd_bind_descriptor_set(screen_quad_overlay.graphics, ctx.desc_set, 1);
            cmd->cmd_bind_vertex_buffer(screen_quad_overlay.vb);
            cmd->cmd_set_scissor(window_width, window_height);
            cmd->cmd_set_viewport(window_width, window_height);
            cmd->cmd_draw(3);
        });

        fg::Pass& tonemapping_pass = fg->add_pass("ToneMappingPass", fg::PassType::Graphics);
        tonemapping_pass.access(fg::ResourceAccessType::TextureIn, fg_config.blend ? blended : opaque);
        tonemapping_pass.access(fg::ResourceAccessType::ColorOut, tonemapped);
        tonemapping_pass.store_load_func(tonemapped, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
        });
        tonemapping_pass.render_area_func([&](RenderingBegin& begin) {
            begin.area(window_width, window_height);
        });
        tonemapping_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_bind_graphics_pipeline(screen_quad_tonemap.graphics);
            cmd->cmd_bind_descriptor_set(screen_quad_tonemap.graphics, screen_quad_tonemap.graphics_desc_set, 0);
            cmd->cmd_bind_descriptor_set(screen_quad_tonemap.graphics, ctx.desc_set, 1);
            cmd->cmd_bind_vertex_buffer(screen_quad_tonemap.vb);
            cmd->cmd_set_scissor(window_width, window_height);
            cmd->cmd_set_viewport(window_width, window_height);
            cmd->cmd_draw(3);
        });

        fg::Pass& opaque_pass = fg->add_pass("Opaque", fg::PassType::Graphics);
        opaque_pass.access(fg::ResourceAccessType::TextureIn, animated_vein_texture);
        opaque_pass.access(fg::ResourceAccessType::DepthStencilOut, depth);
        opaque_pass.access(fg::ResourceAccessType::ColorOut, opaque);
        opaque_pass.access(fg::ResourceAccessType::VertexIn, fg_config.billow ? vertices_displaced : vertices_base);
        opaque_pass.store_load_func(opaque, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
                .clear_value(0.3f, 0.3f, 0.3f, 1.0f);
        });
        opaque_pass.store_load_func(depth, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE) // TODO: store if used in later passes
                .clear_value(1.0f, 0.0f);
        });
        opaque_pass.render_area_func([&](RenderingBegin& begin) {
            begin.area(window_width, window_height);
        });
        opaque_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            // each object takes one draw call
            // ribbon
            cmd->cmd_bind_graphics_pipeline(ribbon.graphics);
            cmd->cmd_bind_descriptor_set(ribbon.graphics, ribbon.graphics_desc_set, 0);
            cmd->cmd_bind_descriptor_set(ribbon.graphics, ctx.desc_set, 1);
            cmd->cmd_push_constant(ribbon.graphics, "useSolidColor", &ribbon.use_solid_color);
            cmd->cmd_push_constant(ribbon.graphics, "solidColor", &ribbon.solid_color);
            cmd->cmd_push_constant(ribbon.graphics, "doubleSided", &ribbon.double_sided);
            cmd->cmd_push_constant(ribbon.graphics, "opacity", &ribbon.opacity);
            cmd->cmd_push_constant(ribbon.graphics, "lightDirection", &light_direction);
            cmd->cmd_push_constant(ribbon.graphics, "viewPos", &cam.eye);
            cmd->cmd_bind_raw_buffer_as_vertex(ctx.vertex_bufs[0], 0);
            // cmd->cmd_bind_vertex_buffer(ribbon.vb);
            cmd->cmd_bind_index_buffer(ribbon.ib, VK_INDEX_TYPE_UINT16);
            cmd->cmd_set_scissor(window_width, window_height);
            cmd->cmd_set_viewport(window_width, window_height);
            cmd->cmd_draw_indexed(ribbon.ib->builder._info.size / sizeof(uint16_t));
            // ground
            cmd->cmd_bind_graphics_pipeline(ground.graphics);
            cmd->cmd_bind_descriptor_set(ground.graphics, ground.graphics_desc_set, 0);
            cmd->cmd_bind_descriptor_set(ground.graphics, ctx.desc_set, 1);
            cmd->cmd_push_constant(ground.graphics, "useSolidColor", &ground.use_solid_color);
            cmd->cmd_push_constant(ground.graphics, "solidColor", &ground.solid_color);
            cmd->cmd_push_constant(ground.graphics, "doubleSided", &ground.double_sided);
            cmd->cmd_push_constant(ground.graphics, "opacity", &ground.opacity);
            cmd->cmd_push_constant(ground.graphics, "lightDirection", &light_direction);
            cmd->cmd_push_constant(ground.graphics, "viewPos", &cam.eye);
            cmd->cmd_bind_vertex_buffer(ground.vb);
            cmd->cmd_bind_index_buffer(ground.ib, VK_INDEX_TYPE_UINT16);
            cmd->cmd_set_scissor(window_width, window_height);
            cmd->cmd_set_viewport(window_width, window_height);
            cmd->cmd_draw_indexed(ground.ib->builder._info.size / sizeof(uint16_t));
        });

        fg::Pass& vein_texture_animation_pass = fg->add_pass("VeinTextureAnimation", fg::PassType::Compute);
        vein_texture_animation_pass.access(fg::ResourceAccessType::StorageImageOut, animated_vein_texture);
        vein_texture_animation_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            float time = now / 1000.0f;
            // float time = frame_count / 60.0f;
            cmd->cmd_bind_compute_pipeline(comp_animate_vein);
            cmd->cmd_bind_descriptor_set(comp_animate_vein, ctx.desc_set, 2);
            cmd->cmd_push_constant(comp_animate_vein, "t", &time);
            cmd->cmd_dispatch(calc_group_count(512, 16), calc_group_count(512, 16)); // local size 16. Hardcoded 512 for the moment
        });


        if (!fg->set_as_backbuffer(ui_overlayed)) {
            return;
        }

        std::pair<bool, std::vector<fg::FrameGraph::FrameRecordInput>> compile_result = 
            std::move(fg->compile(_otcv_context.swapchain->images.size(), ResourceState::TransferSrc));

        if (!compile_result.first) {
            return;
        }
        fg_record_inputs = compile_result.second; // shouldnt use move. As move does not destroy shared_ptr memory
    }

    void init_frame_contexts() {
        frame_ctxs.resize(_otcv_context.swapchain->mock_images.size());
        for (FrameContext& ctx : frame_ctxs) {
            ctx.frame_fence = Fence::create();
            ctx.image_available_semaphore = Semaphore::create();
            ctx.transfer_finished_semaphore = Semaphore::create();
            ctx.command_pool = CommandPool::create(true, false); // this command pool will reset every frame for the framegraph
            ctx.copy_command_buffer = ctx.command_pool->allocate();
        }
    }

    // Boilerplate stuff
    GLFWwindow* _window = nullptr;
    otcv::Context _otcv_context;

    // shaders
    std::map<std::string, ShaderModule*> shader_blob;

    // Because everthing in this scene is static, declare a global descriptor pool
    // generally not a good practice
    std::shared_ptr<NaiveExpandableDescriptorPool> desc_pool;

    PerspectiveCamera cam = PerspectiveCamera(
        glm::vec3(4.0f, 4.0f, 4.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.05f,
        20.0f,
        glm::radians(60.0f),
        (float)init_window_width / (float)init_window_height);

    glm::vec3 light_direction = glm::vec3(10.0f, -5.0f, 5.0f);

    // compute pipelines
    ComputePipeline* comp_animate_vein = nullptr;
    ComputePipeline* comp_animate_billow = nullptr;
    ComputePipeline* comp_height_displacement = nullptr;

    // objects
    struct Object {
        VertexBuffer*               vb;
        Buffer*                     ib;
        otcv::GraphicsPipeline*     graphics = nullptr;
        // otcv::ComputePipeline*      compute0 = nullptr;
        // otcv::ComputePipeline*      compute1 = nullptr;
        glm::mat4                   model_mat;
        std::shared_ptr<StaticUBO>  ubo = nullptr;
        DescriptorSet*              graphics_desc_set = nullptr;
        DescriptorSet*              compute_desc_set = nullptr;

        int         use_solid_color;
        glm::vec3   solid_color;
        int         double_sided;
        float       opacity;
    };
    Object ribbon;
    Object ground;
    Object screen_quad_overlay;
    Object screen_quad_tonemap;

    std::shared_ptr<fg::TransientImageCache> img_allocator;
    std::shared_ptr<fg::TransientBufferCache> buf_allocator;
    std::shared_ptr<fg::FrameGraph> fg;
    std::vector<fg::FrameGraph::FrameRecordInput> fg_record_inputs;

    struct FrameContext {
        // synchronization
        otcv::Fence*        frame_fence = nullptr;
        otcv::Semaphore*    image_available_semaphore = nullptr;
        otcv::Semaphore*    transfer_finished_semaphore = nullptr;

        // command buffers
        otcv::CommandPool*                  command_pool = nullptr;
        otcv::CommandBuffer*                copy_command_buffer;
    };
    std::vector<FrameContext> frame_ctxs;

    struct ImGuiMesh {
        otcv::VertexBuffer* vb = nullptr;
        otcv::Buffer* ib = nullptr;
    };
    std::vector<ImGuiMesh> imgui_meshes; // one per frame

    uint32_t current_frame = 0;

    int window_width;
    int window_height;

    bool framegraph_rebuid = false;
    
    struct FrameGraphConfig {
        bool blend = true;
        bool billow = true;
    };
    FrameGraphConfig fg_config;
};

int main(int argc, char** argv)
{
    Application app;
    app.run();
    app.cleanup();

    return 0;
}


