#include "imgui_impl_otcv.h"
#include "imgui.h"

#include "imgui_vert.h"
#include "imgui_frag.h"

void ImGui_ImplOTCV_Init(ImGui_ImplOTCV_InitInfo* info) {
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplOTCV_Data* bd = IM_NEW(ImGui_ImplOTCV_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_OTCV";

    IM_ASSERT(info->queue != nullptr);
    IM_ASSERT(!info->color_attachments.empty());

    bd->OTCVInitInfo = *info;

    ImGui_ImplOTCV_CreateOTCVObjects();
}

void ImGui_ImplOTCV_CreateOTCVObjects() {
    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    ImGui_ImplOTCV_InitInfo* v = &bd->OTCVInitInfo;

    // sampler
    {
        otcv::SamplerBuilder builder;
        bd->sampler = builder.filter(VK_FILTER_LINEAR, VK_FILTER_LINEAR).build();
    }
    // renderpass, store color attachment contents
    {
        otcv::RenderPassBuilder builder;
        builder
            .attachment()
            .format_samples(v->color_attachments[0]->builder._image_info.format)
            .layouts(v->intial_layout, v->final_layout)
            .load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE)
            .end()
            .subpass()
            .ref_color(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .end();

        if (v->pre_pass_wait_stage != VK_PIPELINE_STAGE_NONE && v->pre_pass_access != VK_ACCESS_NONE) {
            builder.dependencies()
                .src(VK_SUBPASS_EXTERNAL, v->pre_pass_wait_stage, v->pre_pass_access)
                .dst(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) // read operation occurs at load op, earliest possible stage
                .flags(0)
                .end();
        }
        if (v->post_pass_wait_stage != VK_PIPELINE_STAGE_NONE && v->post_pass_access != VK_ACCESS_NONE) {
            builder.dependencies()
                .src(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                .dst(VK_SUBPASS_EXTERNAL, v->post_pass_wait_stage, v->post_pass_access)
                .flags(0)
                .end();
        }

        bd->render_pass = builder.build();
    }

    // pipeline
    {
        otcv::ShaderModuleBuilder builder;
        builder
            .spirv_binary(imgui_vert_spv, sizeof(imgui_vert_spv))
            .push_constant(0, sizeof(float) * 4);
        bd->vertex_shader = builder.build();
    }
    {
        otcv::ShaderModuleBuilder builder;
        builder
            .spirv_binary(imgui_frag_spv, sizeof(imgui_frag_spv))
            .uniform(0, 0).type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).end();
        bd->fragment_shader = builder.build();
    }

    // vertex and index buffers.
    {
        otcv::VertexBufferBuilder vb_builder;
        otcv::BufferBuilder b_builder;
        b_builder.size(1024).host_access(otcv::BufferBuilder::Access::Incoherent);
        bd->vertex_buffer = vb_builder
            .add_binding(b_builder)
            .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(ImVec2))
            .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(ImVec2))
            .add_attribute(0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(ImU32))
            .build();
    }
    {
        otcv::BufferBuilder ib_builder;
        bd->index_buffer = ib_builder
            .size(1024)
            .usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            .host_access(otcv::BufferBuilder::Access::Incoherent)
            .build();
    }

    // framebuffers
    for (otcv::Image* i : v->color_attachments) {
        otcv::FramebufferBuilder builder;
        builder
            .render_pass(bd->render_pass)
            .size(i->builder._image_info.extent.width, i->builder._image_info.extent.height)
            .add_attachment(i);
        bd->frambuffers.push_back(builder.build());
    }

    // graphics pipeline
    {
        std::vector<VkDynamicState> dyn_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        otcv::GraphicsPipelineBuilder builder;
        builder
            .render_pass(bd->render_pass, 0)
            .shader_vertex(bd->vertex_shader)
            .shader_fragment(bd->fragment_shader)
            .vertex_state(bd->vertex_buffer->builder)
            .blend_attachment(0).end() // go with defaul alpha blend
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
        bd->pipeline = builder.build();
    }

    // acquire default font image data
    // if user wishes to specify custom font, font image needs to be generated on the fly
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    if (!v->font_path.empty()) {
        io.Fonts->AddFontFromFileTTF(v->font_path.c_str(), v->font_size_pixels);
    }
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    size_t texture_size = width * height * 4 * sizeof(char);
    // font texture image
    {
        otcv::ImageBuilder builder;
        bd->font_image = builder
            .size(width, height, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM)
            .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        bd->font_image->populate(pixels, texture_size, otcv::ResourceState::FragSample);
    }

    // descriptor pool, reset every frame
    {
        otcv::DescriptorPoolBuilder builder;
        builder
            .descriptor_set_capacity(4)
            .descriptor_type_capacity(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);
        bd->descriptor_pool = builder.build();
    }

    // command pool & command buffer
    {
        bd->command_pool = otcv::CommandPool::create(false, true);
        bd->command_buffer = bd->command_pool->allocate();
    }
}

ImGui_ImplOTCV_Data* ImGui_ImplOTCV_GetBackendData() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplOTCV_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

void ImGui_ImplOTCV_BuildBuffers() {
    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    IM_ASSERT(bd);

    ImDrawData* draw_data = ImGui::GetDrawData();
    // check if index buffer and vertex buffer need to resize
    int vb_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    int ib_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
    if (vb_size > bd->vertex_buffer->buffers[0]->builder._info.size) {
        bd->vertex_buffer->resize(0, vb_size);
    }
    if (ib_size > bd->index_buffer->builder._info.size) {
        otcv::BufferBuilder builder = bd->index_buffer->builder;
        bd->index_buffer->destroy();
        bd->index_buffer = builder.size(ib_size).build();
    }

    uint32_t vtx_offset = 0;
    uint32_t idx_offset = 0;
    for (int i = 0; i < draw_data->CmdListsCount; ++i) {
        const ImDrawList* cmd_list = draw_data->CmdLists[i];
        uint32_t vtx_block_size = cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        uint32_t idx_block_size = cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        bd->vertex_buffer->buffers[0]->copy_host_mapped(cmd_list->VtxBuffer.Data, vtx_offset, vtx_block_size);
        bd->index_buffer->copy_host_mapped(cmd_list->IdxBuffer.Data, idx_offset, idx_block_size);

        vtx_offset += vtx_block_size;
        idx_offset += idx_block_size;
    }
    bd->vertex_buffer->buffers[0]->flush();
    bd->index_buffer->flush();
}

void ImGui_ImplOTCV_Commands(otcv::CommandBuffer* command_buffer, uint32_t image_index, ImGui_ImplOTCV_SynchronizationInfo* info) {
    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    ImGui_ImplOTCV_InitInfo* v = &bd->OTCVInitInfo;

    if (info->attachments_pre_pass_state != otcv::ResourceState::Null) {
        command_buffer->cmd_image_memory_barrier(v->color_attachments[image_index],
            info->attachments_pre_pass_state, otcv::ResourceState::ColorAttachment);
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);

    {
        otcv::RenderPassBegin begin;
        begin
            .framebuffer(bd->frambuffers[image_index])
            .area(bd->frambuffers[image_index]->builder._info.width,
                bd->frambuffers[image_index]->builder._info.height); // do not clear
        command_buffer->cmd_begin_render_pass(bd->render_pass, begin);

        command_buffer->cmd_bind_graphics_pipeline(bd->pipeline);

        command_buffer->cmd_bind_vertex_buffer(bd->vertex_buffer);
        command_buffer->cmd_bind_index_buffer(bd->index_buffer, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

        command_buffer->cmd_set_viewport((float)fb_width, (float)fb_height);

        float push_consts[4];
        // scaling
        push_consts[0] = 2.0f / draw_data->DisplaySize.x;
        push_consts[1] = 2.0f / draw_data->DisplaySize.y;
        // translation
        push_consts[2] = -1.0f - draw_data->DisplayPos.x * push_consts[0];
        push_consts[3] = -1.0f - draw_data->DisplayPos.y * push_consts[1];
        command_buffer->cmd_push_constant(bd->pipeline, push_consts);

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > fb_width) { clip_max.x = (float)fb_width; }
                if (clip_max.y > fb_height) { clip_max.y = (float)fb_height; }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                VkRect2D scissor{};
                scissor.offset.x = (int32_t)(clip_min.x);
                scissor.offset.y = (int32_t)(clip_min.y);
                scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
                scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
                vkCmdSetScissor(command_buffer->vk_command_buffer, 0, 1, &scissor);

                command_buffer->cmd_bind_descriptor_set(bd->pipeline, bd->descriptor_set);

                // Draw
                command_buffer->cmd_draw_indexed(
                    pcmd->ElemCount,
                    pcmd->IdxOffset + global_idx_offset,
                    pcmd->VtxOffset + global_vtx_offset);
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }

        command_buffer->cmd_end_render_pass(bd->render_pass);
    }

    if (info->attachments_post_pass_state != otcv::ResourceState::Null) {
        command_buffer->cmd_image_memory_barrier(v->color_attachments[image_index],
            otcv::ResourceState::ColorAttachment, info->attachments_post_pass_state);
    }
}

void ImGui_ImplOTCV_RenderDrawData(uint32_t attachment_id, ImGui_ImplOTCV_SynchronizationInfo* info) {
    IM_ASSERT(info->wait_for_semaphores.size() == info->wait_for_stages.size());

    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    ImGui_ImplOTCV_InitInfo* v = &bd->OTCVInitInfo;

    bd->descriptor_pool->reset();
    bd->descriptor_set = bd->descriptor_pool->allocate(&bd->pipeline->desc_set_layouts[0]);
    bd->descriptor_set->bind_image_sampler(0, &bd->font_image, &bd->sampler);

    bd->command_buffer->reset();
    /* this has to be called here because it involves index& vertex buffer destroy / creation
    * need it to be synchronized
    */
    ImGui_ImplOTCV_BuildBuffers();
    bd->command_buffer->record(std::bind(&ImGui_ImplOTCV_Commands, std::placeholders::_1, attachment_id, info));

    {
        otcv::QueueSubmit submit;
        otcv::QueueSubmit::Batch& submit_batch = submit.batch();
        submit_batch.add_command_buffer(bd->command_buffer);
        for (uint32_t idx = 0; idx < info->wait_for_semaphores.size();  ++idx) {
            submit_batch.add_wait(info->wait_for_semaphores[idx], info->wait_for_stages[idx]);
        }
        for (uint32_t idx = 0; idx < info->signal_semaphores.size(); ++idx) {
            submit_batch.add_signal(info->signal_semaphores[idx]);
        }
        submit_batch.end();
        if (info->signal_fence) {
            submit.signal(info->signal_fence);
        }

        v->queue->submit(submit);
    }
}

void ImGui_ImplOTCV_DestroyDeviceObjects() {
    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    ImGui_ImplOTCV_InitInfo* v = &bd->OTCVInitInfo;

    bd->sampler->destroy();
    bd->render_pass->destroy();
    bd->vertex_shader->destroy();
    bd->fragment_shader->destroy();
    bd->vertex_buffer->destroy();   
    bd->index_buffer->destroy();
    for (otcv::Framebuffer* fb : bd->frambuffers) {
        fb->destroy();
    }
    bd->pipeline->destroy();
    bd->font_image->destroy();
    bd->descriptor_pool->destroy();
    bd->command_pool->destroy();    
}

void ImGui_ImplOTCV_Shutdown() {
    ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOTCV_DestroyDeviceObjects();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    IM_DELETE(bd);
}