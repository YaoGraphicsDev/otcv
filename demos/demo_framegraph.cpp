#include <iostream>
#include <GLFW/glfw3.h>

#include "frame_graph.hpp"
#include "otcv.h"
#include "otcv_utils.h"

#include "assets.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

const int window_width = 1920;
const int window_height = 960;

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
        load_shaders();
        init_computes();
        init_desc_pool();
        init_objects();
        init_frame_contexts();
        init_framegraph();
        main_loop();
    }

    void draw_frame() {
        FrameContext& f = frame_ctxs[current_frame];
        f.frame_fence->wait();

        f.command_pool->reset();

        uint32_t swapchain_image_id = 0;
        vkAcquireNextImageKHR(
            _otcv_context.device->vk_device,
            _otcv_context.swapchain->vk_swapchain, UINT64_MAX,
            f.image_available_semaphore->vk_semaphore, VK_NULL_HANDLE,
            &swapchain_image_id);

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
        _otcv_context.queue->present(present);

        img_allocator->end_frame_recording(f.frame_fence);
        buf_allocator->end_frame_recording(f.frame_fence);
        current_frame = (current_frame + 1) % frame_ctxs.size();
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

    void main_loop() {
        while (!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            draw_frame();
        }
        vkDeviceWaitIdle(_otcv_context.device->vk_device);
    }

    void cleanup() {
        otcv::destroy_context();
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    void init_window() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        _window = glfwCreateWindow(window_width, window_height, "Framegraph Demo", nullptr, nullptr);
        glfwSetWindowUserPointer(_window, this);
    }

    void init_vulkan_context() {
        _otcv_context = otcv::create_context(_window);
        for (uint32_t i = 0; i < _otcv_context.swapchain->images.size(); ++i) {
            _otcv_context.swapchain->mock_image(i)->initialize_state(otcv::ResourceState::PresentReady);
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
        // full screen graphics pipeline
        GraphicsPipeline* full_screen = otcv::GraphicsPipelineBuilder()
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
            ribbon.solid_color = glm::vec3(0.102f, 0.859f, 0.843f);
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
                glm::scale(glm::mat4(1.0f), { 5.0f, 0.3f, 5.0f });
            ground.ubo.reset(new StaticUBO(ubo_alignment));
            ground.ubo->set(StaticUBOAccess()["model"], &ground.model_mat);
            glm::mat4 pv = cam.proj * cam.view;
            ground.ubo->set(StaticUBOAccess()["projView"], &pv);
            ground.graphics_desc_set = desc_pool->allocate(ground.graphics->desc_set_layouts[0]);
            ground.graphics_desc_set->bind_buffer(0, ground.ubo->_buf);
            ground.graphics_desc_set->bind_sampler(1, &linear_sampler);
            ground.use_solid_color = 0;
            ground.double_sided = 0;
            ground.opacity = 1.0f;
        }
        // screen quad
        {
            screen_quad.vb = screen_quad_ndc();
            screen_quad.graphics = full_screen;
            screen_quad.graphics_desc_set = desc_pool->allocate(screen_quad.graphics->desc_set_layouts[0]);
            screen_quad.graphics_desc_set->bind_sampler(0, &nearest_sampler);
        }
    }

    void init_computes() {
        comp_animate_texture = otcv::ComputePipeline::create(shader_blob.at("animated_texture.comp"));
        comp_height_displacement = otcv::ComputePipeline::create(shader_blob.at("grayscale_height_displacement_z.comp"));
    }

    void init_framegraph() {
        img_allocator.reset(new fg::TransientImageCache);
        buf_allocator.reset(new fg::TransientBufferCache);
        fg.reset(new fg::FrameGraph(img_allocator, buf_allocator));

        // resources
        fg::ResourceHandle animated_texture = fg->add_resource("AnimatedTexture",
            ImageBuilder()
            .size(512, 512, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM));

        fg::ResourceHandle vertices_base = fg->add_resource("VerticesBase",
            BufferBuilder()
            .size(ribbon.vb->buffers[0]->builder._info.size)
            .host_access(otcv::BufferBuilder::Access::Invisible));
        
        fg::ResourceHandle vertices_displaced = fg->add_resource("displacedVertices",
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

        fg::ResourceHandle tonemapped = fg->add_resource("Tonemapped", // this is going to be copied to swapchain
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(_otcv_context.swapchain->image_info.format));

        // passes
        fg::Pass& vertex_copy_pass = fg->add_pass("VertexCopyPass", fg::PassType::Transfer);
        vertex_copy_pass.access(fg::ResourceAccessType::TransferOut, vertices_base);
        vertex_copy_pass.pre_pass_func([&](CommandBuffer* cmd) {
            cmd->cmd_buffer_memory_barrier(ribbon.vb->buffers[0], ResourceState::Created, ResourceState::TransferSrc);
        });
        vertex_copy_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_copy_buffer(ribbon.vb->buffers[0], ctx.transfer_bufs[0]);
        });
        vertex_copy_pass.post_pass_func([&](CommandBuffer* cmd) {
            // we dont care about what's going to happen to vertex buffer at this point
        });

        fg::Pass& height_pass = fg->add_pass("HeightDisplacement", fg::PassType::Compute);
        height_pass.access(fg::ResourceAccessType::TextureIn, animated_texture);
        height_pass.access(fg::ResourceAccessType::SSBOInOut, vertices_base, vertices_displaced);
        height_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_bind_compute_pipeline(comp_height_displacement);
            cmd->cmd_bind_descriptor_set(comp_height_displacement, ribbon.compute_desc_set, 0);
            cmd->cmd_bind_descriptor_set(comp_height_displacement, ctx.desc_set, 2);
            float max_displacement = 0.1f;
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

        fg::Pass& tonemapping_pass = fg->add_pass("ToneMappingPass", fg::PassType::Graphics);
        tonemapping_pass.access(fg::ResourceAccessType::TextureIn, opaque);
        tonemapping_pass.access(fg::ResourceAccessType::ColorOut, tonemapped);
        tonemapping_pass.store_load_func(tonemapped, [&](RenderingBegin::Attachment& attachment) {
            attachment
                .load_store(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
        });
        tonemapping_pass.render_area_func([&](RenderingBegin& begin) {
            begin.area(window_width, window_height);
        });
        tonemapping_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            cmd->cmd_bind_graphics_pipeline(screen_quad.graphics);
            cmd->cmd_bind_descriptor_set(screen_quad.graphics, screen_quad.graphics_desc_set, 0);
            cmd->cmd_bind_descriptor_set(screen_quad.graphics, ctx.desc_set, 1);
            cmd->cmd_bind_vertex_buffer(screen_quad.vb);
            cmd->cmd_set_scissor(window_width, window_height);
            cmd->cmd_set_viewport(window_width, window_height);
            cmd->cmd_draw(3);
        });

        fg::Pass& opaque_pass = fg->add_pass("Opaque", fg::PassType::Graphics);
        opaque_pass.access(fg::ResourceAccessType::TextureIn, animated_texture);
        opaque_pass.access(fg::ResourceAccessType::DepthStencilOut, depth);
        opaque_pass.access(fg::ResourceAccessType::ColorOut, opaque);
        opaque_pass.access(fg::ResourceAccessType::VertexIn, vertices_displaced);
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

        fg::Pass& texture_animation_pass = fg->add_pass("TextureAnimation", fg::PassType::Compute);
        texture_animation_pass.access(fg::ResourceAccessType::StorageImageOut, animated_texture);
        texture_animation_pass.execute_func([&](CommandBuffer* cmd, fg::PassContext& ctx) {
            static int frame_count = 0;
            ++frame_count;
            float time = frame_count / 60.0f;
            cmd->cmd_bind_compute_pipeline(comp_animate_texture);
            cmd->cmd_bind_descriptor_set(comp_animate_texture, ctx.desc_set, 2);
            cmd->cmd_push_constant(comp_animate_texture, "t", &time);
            cmd->cmd_dispatch(calc_group_count(512, 16), calc_group_count(512, 16)); // local size 16. Hardcoded 512 for the moment
        });


        if (!fg->set_as_backbuffer(tonemapped)) {
            return;
        }

        std::pair<bool, std::vector<fg::FrameGraph::FrameRecordInput>> compile_result = 
            std::move(fg->compile(_otcv_context.swapchain->images.size(), ResourceState::TransferSrc));

        if (!compile_result.first) {
            return;
        }
        fg_record_inputs = std::move(compile_result.second);
        // TODO: remember to reset framegraph if you intend to build a different frame
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
        glm::vec3(5.0f, 5.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.05f,
        20.0f,
        glm::radians(60.0f),
        (float)window_width / (float)window_height);

    glm::vec3 light_direction = glm::vec3(10.0f, -5.0f, 5.0f);

    // compute pipelines
    ComputePipeline* comp_animate_texture = nullptr;
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
    Object screen_quad;

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

    uint32_t current_frame = 0;

    const uint32_t FGDescSet = 3;
    
};

int main(int argc, char** argv)
{
    Application app;
    app.run();
    app.cleanup();

    return 0;
}


