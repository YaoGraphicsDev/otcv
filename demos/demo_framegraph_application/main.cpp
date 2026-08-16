#include "frame_graph_application.h"
#include "utils.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include <iostream>
#include <chrono>

using namespace otcv;

struct FrameGraphConfig {
	bool blend = true;
	bool billow = true;
	bool rebuild = false;
};

void configure_framegraph(fg::Application* app, std::shared_ptr<UtilObjects> uo, FrameGraphConfig fg_config) {
    otcv::Context otcv_context = app->otcv_context();
    uint32_t window_width = otcv_context.swapchain->image_info.extent.width;
    uint32_t window_height = otcv_context.swapchain->image_info.extent.height;
    std::shared_ptr<fg::FrameGraph> fg = app->framegraph();

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
        .size(uo->ribbon.vb->buffers[0]->builder._info.size)
        .host_access(otcv::BufferBuilder::Access::Invisible));

    fg::ResourceHandle vertices_displaced = fg->add_resource("DisplacedVertices",
        BufferBuilder()
        .size(uo->ribbon.vb->buffers[0]->builder._info.size)
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
        .format(otcv_context.swapchain->image_info.format));  // this is going to be copied to swapchain

    fg::ResourceHandle ui_overlayed = fg->add_resource("UIOverlayed",
        ImageBuilder()
        .size(window_width, window_height, 1)
        .format(otcv_context.swapchain->image_info.format));

    // passes
    fg::Pass& ui_overlay_pass = fg->add_pass("UIOverlayPass", fg::PassType::Graphics);
    ui_overlay_pass.access(fg::ResourceAccessType::ColorInOut, tonemapped, ui_overlayed);
    ui_overlay_pass.store_load_func(tonemapped, [](RenderingBegin::Attachment& attachment) {
        attachment
            .load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    });
    ui_overlay_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
        begin.area(window_width, window_height);
    });
    ui_overlay_pass.execute_func([app, uo](CommandBuffer* cmd, fg::PassContext& ctx) {
        uint32_t fn = app->frame_slot();
        ImGui_ImplOTCV_Exec(cmd, uo->imgui_meshes[fn].vb, uo->imgui_meshes[fn].ib);
    });

    fg::Pass& billow_texture_animation_pass = fg->add_pass("BillowTextureAnimation", fg::PassType::Compute);
    billow_texture_animation_pass.access(fg::ResourceAccessType::StorageImageOut, animated_billow_texture);
    billow_texture_animation_pass.execute_func([uo](CommandBuffer* cmd, fg::PassContext& ctx) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        float time = now / 1000.0f;
        cmd->cmd_bind_compute_pipeline(uo->comp_animate_billow);
        cmd->cmd_bind_descriptor_set(uo->comp_animate_billow, ctx.desc_set, 2);
        cmd->cmd_push_constant(uo->comp_animate_billow, "t", &time);
        cmd->cmd_dispatch(calc_group_count(64, 16), calc_group_count(64, 16)); // local size 16. Hardcoded 512 for the moment
    });

    fg::Pass& vertex_copy_pass = fg->add_pass("VertexCopyPass", fg::PassType::Transfer);
    vertex_copy_pass.access(fg::ResourceAccessType::TransferOut, vertices_base);
    vertex_copy_pass.pre_pass_func([uo](CommandBuffer* cmd) {
        cmd->cmd_buffer_memory_barrier(uo->ribbon.vb->buffers[0], ResourceState::Created, ResourceState::TransferSrc); // will get called every frame
    });
    vertex_copy_pass.execute_func([uo](CommandBuffer* cmd, fg::PassContext& ctx) {
        cmd->cmd_copy_buffer(uo->ribbon.vb->buffers[0], ctx.transfer_bufs[0]);
    });
    vertex_copy_pass.post_pass_func([](CommandBuffer* cmd) {
        // we dont care about what's going to happen to vertex buffer at this point
    });

    fg::Pass& height_pass = fg->add_pass("HeightDisplacement", fg::PassType::Compute);
    height_pass.access(fg::ResourceAccessType::TextureIn, animated_billow_texture);
    height_pass.access(fg::ResourceAccessType::SSBOInOut, vertices_base, vertices_displaced);
    height_pass.execute_func([uo](CommandBuffer* cmd, fg::PassContext& ctx) {
        cmd->cmd_bind_compute_pipeline(uo->comp_height_displacement);
        cmd->cmd_bind_descriptor_set(uo->comp_height_displacement, uo->ribbon.compute_desc_set, 0);
        cmd->cmd_bind_descriptor_set(uo->comp_height_displacement, ctx.desc_set, 2);
        float max_displacement = 0.2f;
        glm::vec2 patch_size = glm::vec2(1.0f);
        glm::ivec2 grid_count = glm::ivec2(kRibbonGridCount);
        int vertex_stride = 8;
        int pos_attr_offset = 0;
        int normal_attr_offset = 3;
        cmd->cmd_push_constant(uo->comp_height_displacement, "maxDisplacement", &max_displacement);
        cmd->cmd_push_constant(uo->comp_height_displacement, "patchSize", &patch_size);
        cmd->cmd_push_constant(uo->comp_height_displacement, "gridCount", &grid_count);
        cmd->cmd_push_constant(uo->comp_height_displacement, "vertexStride", &vertex_stride);
        cmd->cmd_push_constant(uo->comp_height_displacement, "positionAttributeOffset", &pos_attr_offset);
        cmd->cmd_push_constant(uo->comp_height_displacement, "normalAttributeOffset", &normal_attr_offset);
        cmd->cmd_dispatch(calc_group_count(kRibbonGridCount + 1, 16), calc_group_count(kRibbonGridCount + 1, 16)); // local size 16. Hardcoded 512 for the moment
    });

    fg::Pass& blend_pass = fg->add_pass("BlendPass", fg::PassType::Graphics);
    blend_pass.access(fg::ResourceAccessType::TextureIn, animated_vein_texture);
    blend_pass.access(fg::ResourceAccessType::ColorInOut, opaque, blended);
    blend_pass.store_load_func(opaque, [](RenderingBegin::Attachment& attachment) {
        attachment
            .load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    });
    blend_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
        begin.area(window_width, window_height);
    });
    blend_pass.execute_func([uo, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
        cmd->cmd_bind_graphics_pipeline(uo->screen_quad_overlay.graphics);
        float alpha = 0.2f;
        cmd->cmd_push_constant(uo->screen_quad_overlay.graphics, "alpha", &alpha);
        cmd->cmd_bind_descriptor_set(uo->screen_quad_overlay.graphics, uo->screen_quad_overlay.graphics_desc_set, 0);
        cmd->cmd_bind_descriptor_set(uo->screen_quad_overlay.graphics, ctx.desc_set, 1);
        cmd->cmd_bind_vertex_buffer(uo->screen_quad_overlay.vb);
        cmd->cmd_set_scissor(window_width, window_height);
        cmd->cmd_set_viewport(window_width, window_height);
        cmd->cmd_draw(3);
    });

    fg::Pass& tonemapping_pass = fg->add_pass("ToneMappingPass", fg::PassType::Graphics);
    tonemapping_pass.access(fg::ResourceAccessType::TextureIn, fg_config.blend ? blended : opaque);
    tonemapping_pass.access(fg::ResourceAccessType::ColorOut, tonemapped);
    tonemapping_pass.store_load_func(tonemapped, [](RenderingBegin::Attachment& attachment) {
        attachment
            .load_store(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
    });
    tonemapping_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
        begin.area(window_width, window_height);
    });
    tonemapping_pass.execute_func([uo, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
        cmd->cmd_bind_graphics_pipeline(uo->screen_quad_tonemap.graphics);
        cmd->cmd_bind_descriptor_set(uo->screen_quad_tonemap.graphics, uo->screen_quad_tonemap.graphics_desc_set, 0);
        cmd->cmd_bind_descriptor_set(uo->screen_quad_tonemap.graphics, ctx.desc_set, 1);
        cmd->cmd_bind_vertex_buffer(uo->screen_quad_tonemap.vb);
        cmd->cmd_set_scissor(window_width, window_height);
        cmd->cmd_set_viewport(window_width, window_height);
        cmd->cmd_draw(3);
    });

    fg::Pass& opaque_pass = fg->add_pass("Opaque", fg::PassType::Graphics);
    opaque_pass.access(fg::ResourceAccessType::TextureIn, animated_vein_texture);
    opaque_pass.access(fg::ResourceAccessType::DepthStencilOut, depth);
    opaque_pass.access(fg::ResourceAccessType::ColorOut, opaque);
    opaque_pass.access(fg::ResourceAccessType::VertexIn, fg_config.billow ? vertices_displaced : vertices_base);
    opaque_pass.store_load_func(opaque, [](RenderingBegin::Attachment& attachment) {
        attachment
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(0.3f, 0.3f, 0.3f, 1.0f);
    });
    opaque_pass.store_load_func(depth, [](RenderingBegin::Attachment& attachment) {
        attachment
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE) // TODO: store if used in later passes
            .clear_value(1.0f, 0.0f);
    });
    opaque_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
        begin.area(window_width, window_height);
    });
    opaque_pass.execute_func([uo, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
        // each object takes one draw call
        // ribbon
        cmd->cmd_bind_graphics_pipeline(uo->ribbon.graphics);
        cmd->cmd_bind_descriptor_set(uo->ribbon.graphics, uo->ribbon.graphics_desc_set, 0);
        cmd->cmd_bind_descriptor_set(uo->ribbon.graphics, ctx.desc_set, 1);
        cmd->cmd_push_constant(uo->ribbon.graphics, "useSolidColor", &uo->ribbon.use_solid_color);
        cmd->cmd_push_constant(uo->ribbon.graphics, "solidColor", &uo->ribbon.solid_color);
        cmd->cmd_push_constant(uo->ribbon.graphics, "doubleSided", &uo->ribbon.double_sided);
        cmd->cmd_push_constant(uo->ribbon.graphics, "opacity", &uo->ribbon.opacity);
        cmd->cmd_push_constant(uo->ribbon.graphics, "lightDirection", &uo->light_direction);
        cmd->cmd_push_constant(uo->ribbon.graphics, "viewPos", &uo->cam->eye);
        cmd->cmd_bind_raw_buffer_as_vertex(ctx.vertex_bufs[0], 0);
        // cmd->cmd_bind_vertex_buffer(ribbon.vb);
        cmd->cmd_bind_index_buffer(uo->ribbon.ib, VK_INDEX_TYPE_UINT16);
        cmd->cmd_set_scissor(window_width, window_height);
        cmd->cmd_set_viewport(window_width, window_height);
        cmd->cmd_draw_indexed(uo->ribbon.ib->builder._info.size / sizeof(uint16_t));
        // ground
        cmd->cmd_bind_graphics_pipeline(uo->ground.graphics);
        cmd->cmd_bind_descriptor_set(uo->ground.graphics, uo->ground.graphics_desc_set, 0);
        cmd->cmd_bind_descriptor_set(uo->ground.graphics, ctx.desc_set, 1);
        cmd->cmd_push_constant(uo->ground.graphics, "useSolidColor", &uo->ground.use_solid_color);
        cmd->cmd_push_constant(uo->ground.graphics, "solidColor", &uo->ground.solid_color);
        cmd->cmd_push_constant(uo->ground.graphics, "doubleSided", &uo->ground.double_sided);
        cmd->cmd_push_constant(uo->ground.graphics, "opacity", &uo->ground.opacity);
        cmd->cmd_push_constant(uo->ground.graphics, "lightDirection", &uo->light_direction);
        cmd->cmd_push_constant(uo->ground.graphics, "viewPos", &uo->cam->eye);
        cmd->cmd_bind_vertex_buffer(uo->ground.vb);
        cmd->cmd_bind_index_buffer(uo->ground.ib, VK_INDEX_TYPE_UINT16);
        cmd->cmd_set_scissor(window_width, window_height);
        cmd->cmd_set_viewport(window_width, window_height);
        cmd->cmd_draw_indexed(uo->ground.ib->builder._info.size / sizeof(uint16_t));
    });

    fg::Pass& vein_texture_animation_pass = fg->add_pass("VeinTextureAnimation", fg::PassType::Compute);
    vein_texture_animation_pass.access(fg::ResourceAccessType::StorageImageOut, animated_vein_texture);
    vein_texture_animation_pass.execute_func([uo](CommandBuffer* cmd, fg::PassContext& ctx) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        float time = now / 1000.0f;
        cmd->cmd_bind_compute_pipeline(uo->comp_animate_vein);
        cmd->cmd_bind_descriptor_set(uo->comp_animate_vein, ctx.desc_set, 2);
        cmd->cmd_push_constant(uo->comp_animate_vein, "t", &time);
        cmd->cmd_dispatch(calc_group_count(512, 16), calc_group_count(512, 16)); // local size 16. Hardcoded 512 for the moment
    });


    if (!fg->set_as_backbuffer(ui_overlayed)) {
        assert(false);
    }
}

int main() {
	fg::Application::Config config;
	config.desired_window_width = 960;
	config.desired_window_height = 480;
	std::shared_ptr<fg::Application> fg_app = std::make_shared<fg::Application>(config);

	// emulate any system other than framegraph. Real world systems are usually more complex than this
	std::shared_ptr<UtilObjects> util_objs = std::make_shared<UtilObjects>(fg_app->window(), fg_app->otcv_context());

	auto immediate_gui = [](FrameGraphConfig& fg_config) {
		ImGuiIO& io = ImGui::GetIO();

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		{
			ImGui::Begin("Framegraph Configuration");

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			if (ImGui::Button("Rebuild")) {
				fg_config.rebuild = true;
			}
			else {
				fg_config.rebuild = false;
			}

			ImGui::Checkbox("Blend", &fg_config.blend);
			ImGui::Checkbox("Billow", &fg_config.billow);

			ImGui::End();
		}
		ImGui::Render();

		return fg_config;
	};

    FrameGraphConfig fg_config;
    fg_config.blend = true;
    fg_config.billow = true;

	auto frame_update = [&](fg::Application* app) {
		uint32_t width = app->otcv_context().swapchain->image_info.extent.width;
		uint32_t height = app->otcv_context().swapchain->image_info.extent.height;
		util_objs->update_camera(width, height);

		immediate_gui(fg_config);

		if (fg_config.rebuild) {
			app->register_framegraph_rebuild(std::bind(configure_framegraph, std::placeholders::_1, util_objs, fg_config));
		}

        uint32_t frame_index = app->frame_slot();
		ImGui_ImplOTCV_BuildBuffers(util_objs->imgui_meshes[frame_index].vb, util_objs->imgui_meshes[frame_index].ib);
	};


    fg_app->register_framegraph_rebuild(std::bind(configure_framegraph, std::placeholders::_1, util_objs, fg_config));

	fg_app->synchronized_frame_update(frame_update);

	fg_app->run();

	util_objs = nullptr;
	fg_app = nullptr;

	return 0;
}
