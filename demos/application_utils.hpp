#pragma once

#include "otcv_utils.h"

#include "assets.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <GLFW/glfw3.h>

using namespace otcv;

struct UtilObjects {

    UtilObjects(GLFWwindow* window, otcv::Context otcv_context) {
        uint32_t width = otcv_context.swapchain->image_info.extent.width;
        uint32_t height = otcv_context.swapchain->image_info.extent.height;
        init_camera(width, height);
        init_imgui(window, otcv_context);
        load_shaders();
        init_computes();
        init_desc_pool();
        init_objects(otcv_context);
    }

    ~UtilObjects() {
        shutdown_imgui();
    }

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

    // shaders
    std::map<std::string, otcv::ShaderModule*> shader_blob;

    // Because everthing in this scene is static, declare a global descriptor pool
    // generally not a good practice
    std::shared_ptr<otcv::NaiveExpandableDescriptorPool> desc_pool;

    std::shared_ptr<PerspectiveCamera> cam = nullptr;
    //= PerspectiveCamera(
    //glm::vec3(4.0f, 4.0f, 4.0f),
    //glm::vec3(0.0f, 0.0f, 0.0f),
    //glm::vec3(0.0f, 1.0f, 0.0f),
    //0.05f,
    //20.0f,
    //glm::radians(60.0f),
    //(float)init_window_width / (float)init_window_height);

    glm::vec3 light_direction = glm::vec3(10.0f, -5.0f, 5.0f);

    // compute pipelines
    ComputePipeline* comp_animate_vein = nullptr;
    ComputePipeline* comp_animate_billow = nullptr;
    ComputePipeline* comp_height_displacement = nullptr;

    // objects
    struct Object {
        VertexBuffer* vb;
        Buffer* ib;
        otcv::GraphicsPipeline* graphics = nullptr;
        // otcv::ComputePipeline*      compute0 = nullptr;
        // otcv::ComputePipeline*      compute1 = nullptr;
        glm::mat4                   model_mat;
        std::shared_ptr<StaticUBO>  ubo = nullptr;
        DescriptorSet* graphics_desc_set = nullptr;
        DescriptorSet* compute_desc_set = nullptr;

        int         use_solid_color;
        glm::vec3   solid_color;
        int         double_sided;
        float       opacity;
    };
    Object ribbon;
    Object ground;
    Object screen_quad_overlay;
    Object screen_quad_tonemap;

    struct ImGuiMesh {
        otcv::VertexBuffer* vb = nullptr;
        otcv::Buffer* ib = nullptr;
    };
    std::vector<ImGuiMesh> imgui_meshes; // one per frame


    void init_camera(uint32_t width, uint32_t height) {
        cam.reset(new PerspectiveCamera(
            glm::vec3(4.0f, 4.0f, 4.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            0.05f,
            20.0f,
            glm::radians(60.0f),
            (float)width / (float)height));
    }

    void init_imgui(GLFWwindow* window, otcv::Context otcv_context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // io.DisplaySize = ImVec2(init_window_width, init_window_height);
        io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window, true);

        ImGui_ImplOTCV_InitInfo info;
        info.queue = otcv_context.queue;
        // can also add font here
        info.target_format = otcv_context.swapchain->image_info.format;
        ImGui_ImplOTCV_Init(&info);

        imgui_meshes.resize(otcv_context.swapchain->images.size());
        for (auto& mesh : imgui_meshes) {
            ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
            VertexBufferBuilder vb_builder = bd->vertex_buffer->builder;
            mesh.vb = vb_builder.build();
            BufferBuilder ib_builder = bd->index_buffer->builder;
            mesh.ib = ib_builder.build();
        }
    }

    void shutdown_imgui() {
        ImGui_ImplOTCV_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }

    void load_shaders() {
        shader_blob = otcv::load_shaders_from_dir("./spirv/");
    }

    void init_computes() {
        comp_animate_vein = otcv::ComputePipeline::create(shader_blob.at("animated_vein_texture.comp"));
        comp_animate_billow = otcv::ComputePipeline::create(shader_blob.at("animated_billow_texture.comp"));
        comp_height_displacement = otcv::ComputePipeline::create(shader_blob.at("grayscale_height_displacement_z.comp"));
    }

    void init_desc_pool() {
        desc_pool.reset(new otcv::NaiveExpandableDescriptorPool);
    }

    void init_objects(otcv::Context otcv_context) {
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
            .add_color_attachment_format(otcv_context.swapchain->image_info.format)
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
                glm::scale(glm::mat4(1.0f), { 1.5f, 1.0f, 1.0f });
            ribbon.ubo.reset(new StaticUBO(ubo_alignment));
            ribbon.ubo->set(StaticUBOAccess()["model"], &ribbon.model_mat);
            glm::mat4 pv = cam->proj * cam->view;
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
            glm::mat4 pv = cam->proj * cam->view;
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

    void update_camera(uint32_t width, uint32_t height) {
        cam->aspect = (float)width / (float)height;
        cam->update_view();
        cam->update_proj();
    }
};