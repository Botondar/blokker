#include "Renderer.hpp"

#include <Platform.hpp>
#include <Profiler.hpp>

#include "RenderDevice.cpp"
#include "RTHeap.cpp"
#include "StagingHeap.cpp"
#include "VertexBuffer.cpp"

Vulkan_DeclareFunctionPointer(vkCreateShadersEXT) = nullptr;
Vulkan_DeclareFunctionPointer(vkDestroyShaderEXT) = nullptr;
Vulkan_DeclareFunctionPointer(vkGetShaderBinaryDataEXT) = nullptr;
Vulkan_DeclareFunctionPointer(vkCmdBindShadersEXT) = nullptr;

struct shader_bin
{
    buffer VS;
    buffer FS;
};

static bool Renderer_InitializeFrameParams(renderer* Renderer);
static bool Renderer_ResizeRenderTargets(renderer* Renderer);
static bool Renderer_CreateVoxelTextureArray(renderer* Renderer, u32 Width, u32 Height, u32 MipCount, u32 ArrayCount,const u8* Data);
static bool Renderer_CreateImGuiTexture(renderer* Renderer, u32 Width, u32 Height, const u8* Data);

inline VkExtent2D VkExtent2DFromVec2i(vec2i Extent);

inline VkExtent2D VkExtent2DFromVec2i(vec2i Extent)
{
    VkExtent2D Result = { (u32)Extent.x, (u32)Extent.y };
    return(Result);
}

//
// Render API
//
render_frame* BeginRenderFrame(renderer* Renderer, bool DoResize)
{
    TIMED_FUNCTION();

    if (DoResize)
    {
        if (!Renderer_ResizeRenderTargets(Renderer))
        {
            FatalError("Failed to resize render targets");
        }
    }

    u64 FrameIndex = Renderer->CurrentFrameIndex++;
    u32 BufferIndex = (u32)(FrameIndex % 2);

    vulkan_render_frame* Frame = Renderer->FrameParams + BufferIndex;
    Frame->Renderer = Renderer;
    //Frame->FrameIndex = FrameIndex;
    //Frame->BufferIndex = BufferIndex;
    Frame->RenderExtent = vec2i{ (s32)Renderer->SwapchainSize.width, (s32)Renderer->SwapchainSize.height };
    Frame->PixelTransform = Mat4(2.0f / Frame->RenderExtent.x, 0.0f, 0.0f, -1.0f,
                                 0.0f, 2.0f / Frame->RenderExtent.y, 0.0f, -1.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f);

    Frame->SwapchainImageIndex = INVALID_INDEX_U32;

    Frame->DrawCount = 0;
    Frame->DrawList = (draw_cmd*)Frame->DrawMapping;
    Frame->DrawPositions = (vec2*)Frame->PositionMapping;
    Frame->VertexOffset = 0;

    {
        TIMED_BLOCK("WaitForPreviousFrames");
        vkWaitForFences(Renderer->RenderDevice.Device, 1, &Frame->RenderFinishedFence, VK_TRUE, UINT64_MAX);
        vkResetFences(Renderer->RenderDevice.Device, 1, &Frame->RenderFinishedFence);
    }

    VkResult Result = vkAcquireNextImageKHR(
        Renderer->RenderDevice.Device, Renderer->Swapchain,
        0,
        Frame->ImageAcquiredSemaphore,
        nullptr,
        &Frame->SwapchainImageIndex);
    if (Result != VK_SUCCESS)
    {
        assert(!"Can't acquire swapchain image");
        return nullptr;
    }

    Frame->SwapchainImage = Renderer->SwapchainImages[Frame->SwapchainImageIndex];
    Frame->SwapchainImageView = Renderer->SwapchainImageViews[Frame->SwapchainImageIndex];

    vkResetCommandPool(Renderer->RenderDevice.Device, Frame->CmdPool, 0/*VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT*/);

    VkCommandBufferBeginInfo CommonBeginInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(Frame->TransferCmdBuffer, &CommonBeginInfo);
    vkBeginCommandBuffer(Frame->PrimaryCmdBuffer, &CommonBeginInfo);

    VkImageMemoryBarrier BeginBarriers[] = 
    {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->SwapchainImage,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->DepthBuffer,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
    };
    constexpr u32 BeginBarrierCount = CountOf(BeginBarriers);
    
    vkCmdPipelineBarrier(Frame->PrimaryCmdBuffer,
                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         BeginBarrierCount, BeginBarriers);
    
    VkRenderingAttachmentInfo ColorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = Frame->SwapchainImageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue =  { .color = { 0.0f, 0.6f, 1.0f, 0.0f, }, },
    };
    VkRenderingAttachmentInfo DepthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = Frame->DepthBufferView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue =  { .depthStencil = { 1.0f, 0 }, },
    };
    VkRenderingAttachmentInfo StencilAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = { .depthStencil = { 1.0f, 0 }, },
    };
    
    VkRenderingInfo RenderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
        .renderArea = { { 0, 0 }, { (u32)Frame->RenderExtent.x, (u32)Frame->RenderExtent.y } },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ColorAttachment,
        .pDepthAttachment = &DepthAttachment,
        .pStencilAttachment = &StencilAttachment,
    };

    vkCmdBeginRendering(Frame->PrimaryCmdBuffer, &RenderingInfo);

    VkFormat ColorAttachmentFormats[] = 
    {
        Frame->Renderer->SurfaceFormat.format,
    };

    VkCommandBufferInheritanceRenderingInfo RenderingInheritanceInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = RenderingInfo.flags & (~VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT),
        .viewMask = 0,
        .colorAttachmentCount = RenderingInfo.colorAttachmentCount,
        .pColorAttachmentFormats = ColorAttachmentFormats,
        .depthAttachmentFormat = Frame->Renderer->DepthFormat,
        .stencilAttachmentFormat = Frame->Renderer->StencilFormat,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkCommandBufferInheritanceInfo InheritanceInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .pNext = &RenderingInheritanceInfo,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE,
        .occlusionQueryEnable = VK_FALSE,
        .queryFlags = 0,
        .pipelineStatistics = 0,
    };
    VkCommandBufferBeginInfo SecondaryBeginInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &InheritanceInfo,
    };
    vkBeginCommandBuffer(Frame->SceneCmdBuffer, &SecondaryBeginInfo);
    vkBeginCommandBuffer(Frame->ImmediateCmdBuffer, &SecondaryBeginInfo);
    vkBeginCommandBuffer(Frame->ImGuiCmdBuffer, &SecondaryBeginInfo);

    VkViewport Viewport = 
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (f32)Frame->RenderExtent.x,
        .height = (f32)Frame->RenderExtent.y,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    
    VkRect2D Scissor = 
    {
        .offset = { 0, 0 },
        .extent = { (u32)Frame->RenderExtent.x, (u32)Frame->RenderExtent.y },
    };
    
    vkCmdSetViewport(Frame->SceneCmdBuffer, 0, 1, &Viewport);
    vkCmdSetScissor(Frame->SceneCmdBuffer, 0, 1, &Scissor);
    vkCmdSetViewport(Frame->ImmediateCmdBuffer, 0, 1, &Viewport);
    vkCmdSetScissor(Frame->ImmediateCmdBuffer, 0, 1, &Scissor);
    vkCmdSetViewport(Frame->ImGuiCmdBuffer, 0, 1, &Viewport);
    vkCmdSetScissor(Frame->ImGuiCmdBuffer, 0, 1, &Scissor);
    return((render_frame*)Frame);
}

void EndRenderFrame(render_frame* Frame_)
{
    TIMED_FUNCTION();
    vulkan_render_frame* Frame = (vulkan_render_frame*)Frame_;

    {
        vkCmdBindPipeline(Frame->SceneCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->Pipeline);
        vkCmdBindDescriptorSets(Frame->SceneCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->PipelineLayout,
                                0, 1, &Frame->Renderer->DescriptorSet, 0, nullptr);

        VkDeviceSize VertexBufferOffset = 0;
        vkCmdBindVertexBuffers(Frame->SceneCmdBuffer, 0, 1, &Frame->Renderer->VB.Buffer, &VertexBufferOffset);
        vkCmdBindVertexBuffers(Frame->SceneCmdBuffer, 1, 1, &Frame->PositionBuffer, &VertexBufferOffset);
    
        mat4 VP = Frame->ProjectionTransform * Frame->ViewTransform;

        vkCmdPushConstants(
            Frame->SceneCmdBuffer,
            Frame->Renderer->PipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(mat4), &VP);
        vkCmdDrawIndirect(Frame->SceneCmdBuffer, Frame->DrawBuffer, 0, Frame->DrawCount, sizeof(VkDrawIndirectCommand));

        vkEndCommandBuffer(Frame->SceneCmdBuffer);
        vkCmdExecuteCommands(Frame->PrimaryCmdBuffer, 1, &Frame->SceneCmdBuffer);

        vkEndCommandBuffer(Frame->ImmediateCmdBuffer);
        vkCmdExecuteCommands(Frame->PrimaryCmdBuffer, 1, &Frame->ImmediateCmdBuffer);

        vkEndCommandBuffer(Frame->ImGuiCmdBuffer);
        vkCmdExecuteCommands(Frame->PrimaryCmdBuffer, 1, &Frame->ImGuiCmdBuffer);

        vkCmdEndRendering(Frame->PrimaryCmdBuffer);

        VkImageMemoryBarrier EndBarriers[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Frame->SwapchainImage,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Frame->DepthBuffer,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };
        constexpr u32 EndBarrierCount = CountOf(EndBarriers);

        vkCmdPipelineBarrier(Frame->PrimaryCmdBuffer,
                             VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             EndBarrierCount, EndBarriers);
    }

    vkEndCommandBuffer(Frame->TransferCmdBuffer);
    vkEndCommandBuffer(Frame->PrimaryCmdBuffer);

    VkPipelineStageFlags TransferWaitStage = VK_PIPELINE_STAGE_NONE;
    VkSemaphore WaitSemaphores[] = 
    {
        Frame->TransferFinishedSemaphore,
        Frame->ImageAcquiredSemaphore,
    };
    VkPipelineStageFlags WaitStageMask[] = 
    {
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
    VkSubmitInfo Submits[] = 
    {
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = &TransferWaitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &Frame->TransferCmdBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &Frame->TransferFinishedSemaphore,
        },
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = CountOf(WaitSemaphores),
            .pWaitSemaphores = WaitSemaphores,
            .pWaitDstStageMask = WaitStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &Frame->PrimaryCmdBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &Frame->RenderFinishedSemaphore,
        },
    };
    vkQueueSubmit(Frame->Renderer->RenderDevice.GraphicsQueue, CountOf(Submits), Submits, Frame->RenderFinishedFence);

    {
        TIMED_BLOCK("Present");

        VkPresentInfoKHR PresentInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &Frame->RenderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &Frame->Renderer->Swapchain,
            .pImageIndices = &Frame->SwapchainImageIndex,
            .pResults = nullptr,
        };

        vkQueuePresentKHR(Frame->Renderer->RenderDevice.GraphicsQueue, &PresentInfo);
    }
}

bool UploadVertexBlock(render_frame* Frame_, 
                      vertex_buffer_block* Block,
                      u64 DataSize0, const void* Data0, 
                      u64 DataSize1, const void* Data1)
{
    bool Result = false;

    vulkan_render_frame* Frame = (vulkan_render_frame*)Frame_;

    staging_heap* Heap = &Frame->Renderer->StagingHeap;
    vertex_buffer* VertexBuffer = &Frame->Renderer->VB;

    u64 TotalSize = DataSize0 + DataSize1;
    Assert(TotalSize <= Heap->HeapSize);
    Assert(TotalSize == Block->VertexCount * sizeof(terrain_vertex));

    u64 AtomSize = Frame->Renderer->RenderDevice.NonCoherentAtomSize;
    u64 Offset = AlignTo(Heap->HeapOffset, AtomSize);

    // TODO(boti): We need to ensure that we're not overwriting the previous frame's data!
    if (Heap->HeapSize - Heap->HeapOffset < TotalSize)
    {
        Offset = 0;
    }

    VkMappedMemoryRange Range = 
    {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = Heap->Heap,
        .offset = Offset,
        .size = AlignTo(TotalSize, AtomSize),
    };

    void* Mapping = nullptr;
    if (vkMapMemory(Frame->Renderer->RenderDevice.Device, Range.memory, Range.offset, Range.size, 0, &Mapping) == VK_SUCCESS)
    {
        memcpy(Mapping, Data0, DataSize0);
        if (DataSize1)
        {
            memcpy(OffsetPtr(Mapping, DataSize0), Data1, DataSize1);
        }
        vkFlushMappedMemoryRanges(Frame->Renderer->RenderDevice.Device, 1, &Range);
        vkUnmapMemory(Frame->Renderer->RenderDevice.Device, Heap->Heap);

        VkBufferCopy Region = 
        {
            .srcOffset = Offset,
            .dstOffset = Block->VertexOffset * sizeof(terrain_vertex),
            .size = TotalSize,
        };
        vkCmdCopyBuffer(Frame->TransferCmdBuffer, Heap->Buffer, VertexBuffer->Buffer, 1, &Region);
        Result = true;
    }
    else
    {
        UnhandledError("Failed to map staging heap memory");
    }

    Heap->HeapOffset = Range.offset + Range.size;
    return(Result);
}

void FreeVertexBlock(render_frame* Frame, vertex_buffer_block* Block)
{
    VB_Free(&Frame->Renderer->VB, Block);
}

vertex_buffer_block* AllocateAndUploadVertexBlock(render_frame* Frame,
                                                  u64 DataSize0, const void* Data0,
                                                  u64 DataSize1, const void* Data1)
{
    u32 VertexCount = (u32)((DataSize0 + DataSize1) / sizeof(terrain_vertex));
    vertex_buffer_block* Block = VB_Allocate(&Frame->Renderer->VB, VertexCount);
    if (Block)
    {
        if (UploadVertexBlock(Frame, Block, DataSize0, Data0, DataSize1, Data1) == false)
        {
            VB_Free(&Frame->Renderer->VB, Block);
            Block = nullptr;
        }
    }
    return(Block);
}

void RenderVertexBlock(render_frame* Frame, vertex_buffer_block* VertexBlock, vec2 P)
{
    if (Frame->DrawCount < Frame->MaxDrawCount)
    {
        u32 Index = Frame->DrawCount++;
        Frame->DrawPositions[Index] = P;
        Frame->DrawList[Index] = 
        {
            .VertexCount = VertexBlock->VertexCount,
            .InstanceCount = 1,
            .VertexOffset = VertexBlock->VertexOffset,
            .InstanceOffset = Index,
        };
    }
    else
    {
        UnhandledError("Draw buffer out of memory");
    }
}

bool ImTriangleList(render_frame* Frame_, 
                    mat4 Transform, f32 DepthBias,
                    u32 VertexCount, const vertex* VertexData)
{
    bool Result = false;

    vulkan_render_frame* Frame = (vulkan_render_frame*)Frame_;

    u64 Offset = AlignTo(Frame->VertexOffset, alignof(vertex));
    u32 DataSize = VertexCount * sizeof(vertex);
    if (Offset + DataSize <= Frame->VertexSize)
    {
        memcpy(OffsetPtr(Frame->VertexMapping, Offset), VertexData, DataSize);
        Frame->VertexOffset = Offset + DataSize;

        vkCmdBindPipeline(Frame->ImmediateCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->ImPipeline);
        vkCmdSetPrimitiveTopology(Frame->ImmediateCmdBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        vkCmdBindVertexBuffers(Frame->ImmediateCmdBuffer, 0, 1, &Frame->VertexBuffer, &Offset);
        vkCmdPushConstants(Frame->ImmediateCmdBuffer, Frame->Renderer->ImPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                           0, sizeof(Transform), &Transform);
        // TODO(boti): depth bias should probably be separately parameterized
        vkCmdSetDepthBias(Frame->ImmediateCmdBuffer, DepthBias, 0.0f, DepthBias);
        vkCmdDraw(Frame->ImmediateCmdBuffer, VertexCount, 1, 0, 0);

        Result = true;
    }
    else
    {
        UnhandledError("Immediate renderer out of memory");
    }
    return(Result);
}

void ImBox(render_frame* Frame, aabb Box, u32 Color, f32 DepthBias /*= 0.0f*/)
{
    TIMED_FUNCTION();

    const vertex VertexData[] =
    {
        // EAST
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
                                        
        // WEST                         
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // NORTH                        
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // SOUTH                        
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
                                        
        // TOP                          
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // BOTTOM                       
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
    };
    constexpr u32 VertexCount = CountOf(VertexData);
    mat4 Transform = Frame->ProjectionTransform * Frame->ViewTransform;
    ImTriangleList(Frame, Transform, DepthBias, VertexCount, VertexData);
}

void ImBoxOutline(render_frame* Frame, aabb Box, u32 Color, f32 OutlineSize)
{
    TIMED_FUNCTION();

    const aabb Boxes[] = 
    {
        // Bottom
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Max.x, Box.Min.y + OutlineSize, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Max.y, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Min.z + OutlineSize }),

        // Top
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Max.z }, { Box.Max.x, Box.Min.y + OutlineSize, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Max.z }, { Box.Min.x + OutlineSize, Box.Max.y, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Max.z }, { Box.Max.x - OutlineSize, Box.Max.y, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Max.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Max.z - OutlineSize }),

        // Side
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Min.y + OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Min.y + OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Max.y - OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Max.x, Box.Max.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Max.z }),
    };
    constexpr u32 BoxCount = CountOf(Boxes);
    for (u32 i = 0; i < BoxCount; i++)
    {
        ImBox(Frame, Boxes[i], Color, -1.0f);
    }
}

void ImRect2D(render_frame* Frame, vec2 p0, vec2 p1, u32 Color)
{
    TIMED_FUNCTION();

    vertex VertexData[] = 
    {
        { { p1.x, p0.y, 0.0f }, {}, Color }, 
        { { p1.x, p1.y, 0.0f }, {}, Color },
        { { p0.x, p0.y, 0.0f }, {}, Color },
        { { p0.x, p1.y, 0.0f }, {}, Color },
    };
    vertex VertexDataExploded[] = 
    {
        VertexData[0], VertexData[1], VertexData[2],
        VertexData[2], VertexData[1], VertexData[3],
    };
    u32 VertexCount = CountOf(VertexDataExploded);
    ImTriangleList(Frame, Frame->PixelTransform, 0.0f, VertexCount, VertexDataExploded);
}

void ImRectOutline2D(render_frame* Frame, vec2 p0, vec2 p1, u32 Color, f32 OutlineSize, outline_type Type)
{
    TIMED_FUNCTION();

    vec2 P0, P1;
    if (Type == outline_type::Outer)
    {
        P0 = p0;
        P1 = p1;
    }
    else if (Type == outline_type::Inner)
    {
        P0 = p0 + vec2{ OutlineSize, OutlineSize };
        P1 = p1 + vec2{ -OutlineSize, -OutlineSize };
    }
    else
    {
        P0 = {};
        P1 = {};
        assert(!"Invalid code path");
    }

    // Top
    ImRect2D(Frame,
             vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
             vec2{ P1.x + OutlineSize, P0.y               },
             Color);
    // Left
    ImRect2D(Frame, 
             vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
             vec2{ P0.x              , P1.y + OutlineSize },
             Color);
    // Right
    ImRect2D(Frame, 
             vec2{ P1.x              , P0.y - OutlineSize },
             vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
             Color);
    // Bottom
    ImRect2D(Frame, 
             vec2{ P0.x - OutlineSize, P1.y               },
             vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
             Color);
}

void RenderImGui(render_frame* Frame_, const ImDrawData* DrawData)
{
    TIMED_FUNCTION();
    vulkan_render_frame* Frame = (vulkan_render_frame*)Frame_;

    if (DrawData && (DrawData->TotalVtxCount > 0) && (DrawData->TotalIdxCount > 0))
    {
        u64 VertexDataOffset = AlignTo(Frame->VertexOffset, sizeof(ImDrawVert));
        u64 VertexDataSize = ((u64)DrawData->TotalVtxCount * sizeof(ImDrawVert));
        u64 VertexDataEnd = VertexDataOffset + VertexDataSize;

        u64 IndexDataOffset = AlignTo(VertexDataEnd, sizeof(ImDrawIdx));
        u64 IndexDataSize = (u64)DrawData->TotalIdxCount * sizeof(ImDrawIdx);
        u64 IndexDataEnd = IndexDataOffset + IndexDataSize;

        u64 ImGuiDataSize = VertexDataSize + IndexDataSize;
        u64 ImGuiDataEnd = IndexDataEnd;

        if (ImGuiDataEnd <= Frame->VertexSize)
        {
            Frame->VertexOffset = ImGuiDataEnd;

            //vkCmdBindPipeline(Frame->ImGuiCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->ImGuiPipeline);
            VkShaderStageFlagBits Stages[] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
            VkShaderEXT Shaders[] = { Frame->Renderer->ImGuiVS, Frame->Renderer->ImGuiFS };

            vkCmdBindShadersEXT(Frame->ImGuiCmdBuffer, CountOf(Stages), Stages, Shaders);
            vkCmdPushConstants(
                Frame->ImGuiCmdBuffer, 
                VK_NULL_HANDLE,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(Frame->PixelTransform), &Frame->PixelTransform);
            vkCmdBindDescriptorSets(
                Frame->ImGuiCmdBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, 
                VK_NULL_HANDLE,
                0, 1, &Frame->Renderer->ImGuiDescriptorSet, 0, nullptr);

            vkCmdBindVertexBuffers(Frame->ImGuiCmdBuffer, 0, 1, &Frame->VertexBuffer, &VertexDataOffset);
            VkIndexType IndexType = (sizeof(ImDrawIdx) == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            vkCmdBindIndexBuffer(Frame->ImGuiCmdBuffer, Frame->VertexBuffer, IndexDataOffset, IndexType);

            ImDrawVert* VertexDataAt = (ImDrawVert*)OffsetPtr(Frame->VertexMapping, VertexDataOffset);
            ImDrawIdx* IndexDataAt = (ImDrawIdx*)OffsetPtr(Frame->VertexMapping, IndexDataOffset);

            u32 VertexOffset = 0;
            u32 IndexOffset = 0;
            for (int CmdListIndex = 0; CmdListIndex < DrawData->CmdListsCount; CmdListIndex++)
            {
                ImDrawList* CmdList = DrawData->CmdLists[CmdListIndex];

                memcpy(VertexDataAt, CmdList->VtxBuffer.Data, (u64)CmdList->VtxBuffer.Size * sizeof(ImDrawVert));
                VertexDataAt += CmdList->VtxBuffer.Size;

                memcpy(IndexDataAt, CmdList->IdxBuffer.Data, (u64)CmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
                IndexDataAt += CmdList->IdxBuffer.Size;

                for (int CmdBufferIndex = 0; CmdBufferIndex < CmdList->CmdBuffer.Size; CmdBufferIndex++)
                {
                    ImDrawCmd* Command = &CmdList->CmdBuffer[CmdBufferIndex];
                    Assert((u64)Command->TextureId == Frame->Renderer->ImGuiTextureID);

                    VkRect2D Scissor = 
                    {
                        .offset = { (s32)Command->ClipRect.x, (s32)Command->ClipRect.y },
                        .extent = 
                        { 
                            .width = (u32)Command->ClipRect.z,
                            .height = (u32)Command->ClipRect.w,
                        },
                    };
                    vkCmdSetScissor(Frame->ImGuiCmdBuffer, 0, 1, &Scissor);
                    vkCmdDrawIndexed(
                        Frame->ImGuiCmdBuffer, Command->ElemCount, 1, 
                        Command->IdxOffset + IndexOffset, 
                        Command->VtxOffset + VertexOffset, 
                        0);
                }

                VertexOffset += CmdList->VtxBuffer.Size;
                IndexOffset += CmdList->IdxBuffer.Size;
            }

            // Reset the scissor in case someone wants to render after us
            VkRect2D Scissor = 
            {
                .offset = { 0, 0 },
                .extent = VkExtent2DFromVec2i(Frame->RenderExtent),
            };
            vkCmdSetScissor(Frame->ImGuiCmdBuffer, 0, 1, &Scissor);
            vkCmdBindShadersEXT(Frame->ImGuiCmdBuffer, CountOf(Stages), Stages, nullptr);
        }
        else
        {
            Platform.DebugPrint("WARNING: not enough memory for ImGui\n");
        }
    }
}

static bool Renderer_ResizeRenderTargets(renderer* Renderer)
{
    bool Result = true;

    vkDeviceWaitIdle(Renderer->RenderDevice.Device);
    VkSurfaceCapabilitiesKHR SurfaceCaps = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceCaps) == VK_SUCCESS)
    {
        VkExtent2D Extent = SurfaceCaps.currentExtent;
        VkSwapchainCreateInfoKHR SwapchainInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = Renderer->Surface,
            .minImageCount = 2,
            .imageFormat = Renderer->SurfaceFormat.format,
            .imageColorSpace = Renderer->SurfaceFormat.colorSpace,
            .imageExtent = Extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = 
#if 1
                VK_PRESENT_MODE_FIFO_KHR,
#else
                VK_PRESENT_MODE_MAILBOX_KHR,
#endif
            .clipped = VK_FALSE,
            .oldSwapchain = Renderer->Swapchain,
        };

        if (vkCreateSwapchainKHR(Renderer->RenderDevice.Device, &SwapchainInfo, nullptr, &Renderer->Swapchain) == VK_SUCCESS)
        {
            vkDestroySwapchainKHR(Renderer->RenderDevice.Device, SwapchainInfo.oldSwapchain, nullptr);
            Renderer->SwapchainSize = Extent;

            Renderer->SwapchainImageCount = Renderer->MaxSwapchainImageCount;
            vkGetSwapchainImagesKHR(Renderer->RenderDevice.Device, Renderer->Swapchain, &Renderer->SwapchainImageCount, Renderer->SwapchainImages);
            for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
            {
                vkDestroyImageView(Renderer->RenderDevice.Device, Renderer->SwapchainImageViews[i], nullptr);
                VkImageViewCreateInfo ViewInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .image = Renderer->SwapchainImages[i],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = Renderer->SurfaceFormat.format,
                    .components = {},
                    .subresourceRange = 
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };

                if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &Renderer->SwapchainImageViews[i]) == VK_SUCCESS)
                {
                }
                else
                {
                    FatalError("Failed to create swapchain image view");
                }
            }

            if (Renderer->RTHeap.Heap)
            {
                Renderer->RTHeap.HeapOffset = 0;
            }
            else
            {
                if (RTHeap_Create(&Renderer->RTHeap, 64*1024*1024,
                                  Renderer->RenderDevice.MemoryTypes.DeviceLocal,
                                  Renderer->RenderDevice.Device))
                {
                }
                else
                {
                    FatalError("Failed to create render target heap");
                }
            }

            vkDestroyImageView(Renderer->RenderDevice.Device, Renderer->DepthBufferView, nullptr);
            vkDestroyImage(Renderer->RenderDevice.Device, Renderer->DepthBuffer, nullptr);

            VkImageCreateInfo DepthInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = Renderer->DepthFormat,
                .extent = { Extent.width, Extent.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            if (vkCreateImage(Renderer->RenderDevice.Device, &DepthInfo, nullptr, &Renderer->DepthBuffer) == VK_SUCCESS)
            {
                if (RTHeap_PushImage(&Renderer->RTHeap, Renderer->DepthBuffer))
                {
                    VkImageViewCreateInfo DepthViewInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .image = Renderer->DepthBuffer,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D,
                        .format = Renderer->DepthFormat,
                        .components = {},
                        .subresourceRange = 
                        {
                            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    };
                    if (vkCreateImageView(Renderer->RenderDevice.Device, &DepthViewInfo, nullptr, &Renderer->DepthBufferView) == VK_SUCCESS)
                    {
                        // HACK(boti):
                        for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
                        {
                            Renderer->FrameParams[i].DepthBuffer = Renderer->DepthBuffer;
                            Renderer->FrameParams[i].DepthBufferView = Renderer->DepthBufferView;
                        }
                    }
                    else
                    {
                        FatalError("Failed to create depth buffer view");
                    }
                }
                else
                {
                    FatalError("Failed to allocate depth buffer memory");
                }
            }
            else
            {
                FatalError("Failed to create depth buffer");
            }
        }
        else
        {
            FatalError("Failed to create swapchain");
        }
    }
    else
    {
        FatalError("vulkan GetSurfaceCaps failed");
    }

    return(Result);
}

//
// Initialization
//
renderer* CreateRenderer(memory_arena* Arena, memory_arena* TransientArena,
                         const renderer_init_info* RendererInfo)
{
    renderer* Renderer = PushStruct<renderer>(Arena);
    if (!Renderer)
    {
        return nullptr;
    }

    if (!RendererInfo->TextureData || !RendererInfo->ImGuiTextureData)
    {
        return nullptr;
    }

    if (!CreateRenderDevice(&Renderer->RenderDevice))
    {
        return nullptr;
    }

    Renderer->Surface = Platform.CreateVulkanSurface(Renderer->RenderDevice.Instance);
    if (Renderer->Surface == VK_NULL_HANDLE)
    {
        return nullptr;
    }

    Renderer->DepthFormat = VK_FORMAT_D32_SFLOAT;
    Renderer->StencilFormat = VK_FORMAT_UNDEFINED;

    VkResult Result = VK_SUCCESS;
    {
        VkSurfaceCapabilitiesKHR SurfaceCaps;
        Result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceCaps);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }

        // TODO
        if ((SurfaceCaps.currentExtent.width == 0) || (SurfaceCaps.currentExtent.height == 0))
        {
            return nullptr;
        }

        u32 SurfaceFormatCount = 0;
        VkSurfaceFormatKHR SurfaceFormats[64];
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, nullptr);
            if (SurfaceFormatCount > 64)
            {
                return nullptr;
            }
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, SurfaceFormats);
        }
        
        // NOTE: the order of this array defines which formats are preferred over others
        const VkFormat Formats[] = 
        {
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_SRGB,
        };
        constexpr u32 FormatCount = CountOf(Formats);

        bool FormatFound = false;
        for (u32 i = 0; i < FormatCount && !FormatFound; i++)
        {
            for (u32 j = 0; j < SurfaceFormatCount && !FormatFound; j++)
            {
                if (SurfaceFormats[j].format == Formats[i])
                {
                    FormatFound = true;
                    Renderer->SurfaceFormat = SurfaceFormats[i];
                }
            }
        }
        if (!FormatFound)
        {
            return nullptr;
        }
    }

    Renderer_ResizeRenderTargets(Renderer);

    {
        VkCommandPoolCreateInfo CmdPoolInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Renderer->RenderDevice.TransferFamilyIndex,
        };

        Result = vkCreateCommandPool(Renderer->RenderDevice.Device, &CmdPoolInfo, nullptr, &Renderer->TransferCmdPool);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }

        VkCommandBufferAllocateInfo AllocInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = Renderer->TransferCmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        Result = vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &AllocInfo, &Renderer->TransferCmdBuffer);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }
    }

    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        
    }

    if (!Renderer_InitializeFrameParams(Renderer))
    {
        return nullptr;
    }
    
    if (!StagingHeap_Create(&Renderer->StagingHeap, 256*1024*1024, Renderer))
    {
        return nullptr;
    }

    if (!VB_Create(&Renderer->VB, Renderer->RenderDevice.MemoryTypes.DeviceLocal, 1024*1024*1024, Renderer->RenderDevice.Device, Arena))
    {
        return nullptr;
    }

    auto LoadShader = [](const char* Path, memory_arena* Arena) -> shader_bin
    {
        shader_bin Result = {};
        constexpr u64 MaxPathLength = 256;
        char Filename[MaxPathLength] = {};

        u64 PathLength = CopyZString(MaxPathLength, Filename, Path);
        if (CopyZString(MaxPathLength - PathLength, Filename + PathLength, ".vs") == 3)
        {
            Result.VS = Platform.LoadEntireFile(Filename, Arena);
            CopyZString(MaxPathLength - PathLength, Filename + PathLength, ".fs");
            Result.FS = Platform.LoadEntireFile(Filename, Arena);
        }
        return(Result);
    };

    auto LoadAndCompileShaders = [Renderer, TransientArena, LoadShader](const char* Path, VkShaderModule* VS, VkShaderModule* FS) -> bool
    {
        bool Result = false;
        memory_arena_checkpoint Checkpoint = ArenaCheckpoint(TransientArena);
        shader_bin ShaderBinary = LoadShader(Path, TransientArena);
        if (ShaderBinary.VS.Size > 0 && ShaderBinary.VS.Size > 0)
        {
            VkShaderModuleCreateInfo VSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = ShaderBinary.VS.Size,
                .pCode = (u32*)ShaderBinary.VS.Data,
            };
            VkShaderModuleCreateInfo FSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = ShaderBinary.FS.Size,
                .pCode = (u32*)ShaderBinary.FS.Data,
            };
            if ((vkCreateShaderModule(Renderer->RenderDevice.Device, &VSInfo, nullptr, VS) == VK_SUCCESS) &&
                (vkCreateShaderModule(Renderer->RenderDevice.Device, &FSInfo, nullptr, FS) == VK_SUCCESS))
            {
                Result = true;
            }
        }

        RestoreArena(TransientArena, Checkpoint);
        return(Result);
    };

    // Descriptors
    {
        // Static sampler
        {
            VkSamplerCreateInfo SamplerInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .mipLodBias = 0.0f,
                .anisotropyEnable = VK_TRUE,
                .maxAnisotropy = 8.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = VK_LOD_CLAMP_NONE,
                .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                .unnormalizedCoordinates = VK_FALSE,
            };
            VkSampler Sampler = VK_NULL_HANDLE;
            if (vkCreateSampler(Renderer->RenderDevice.Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
            {
                Renderer->Sampler = Sampler;
            }
            else
            {
                return nullptr;
            }
        }

        // Set layout
        {
            VkDescriptorSetLayoutBinding Bindings[] = 
            {
                {
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = nullptr,
                },
                {
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &Renderer->Sampler,
                },
                {
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_ALL,
                    .pImmutableSamplers = nullptr,
                },
            };
            constexpr u32 BindingCount = CountOf(Bindings);

            VkDescriptorSetLayoutCreateInfo CreateInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = BindingCount,
                .pBindings = Bindings,
            };
            VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(Renderer->RenderDevice.Device, &CreateInfo, nullptr, &Layout) == VK_SUCCESS)
            {
                Renderer->DescriptorSetLayout = Layout;
            }
            else
            {
                return nullptr;
            }
        }

        // Descriptor pool + set
        {
            VkDescriptorPoolSize PoolSizes[] = 
            {
                { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, },
                { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, },
                { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, },
            };
            constexpr u32 PoolSizeCount = CountOf(PoolSizes);

            VkDescriptorPoolCreateInfo PoolInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = PoolSizeCount,
                .pPoolSizes = PoolSizes,
            };

            VkDescriptorPool Pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(Renderer->RenderDevice.Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo AllocInfo =
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .descriptorPool = Pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &Renderer->DescriptorSetLayout,
                };

                VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(Renderer->RenderDevice.Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    Renderer->DescriptorPool = Pool;
                    Renderer->DescriptorSet = DescriptorSet;
                }
                else
                {
                    vkDestroyDescriptorPool(Renderer->RenderDevice.Device, Pool, nullptr);
                    return nullptr;
                }
            }
            else
            {
                return nullptr;
            }
        }
    }

    // Main pipeline
    {
#if ENABLE_VK_SHADER_OBJECT
        {
            memory_arena_checkpoint Checkpoint = ArenaCheckpoint(TransientArena);
            shader_bin ShaderBinary = LoadShader("shader/shader", TransientArena);
            Assert((ShaderBinary.VS.Size > 0) && (ShaderBinary.FS.Size > 0));

            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };

            VkShaderCreateInfoEXT Infos[] = 
            {
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.VS.Size,
                    .pCode = ShaderBinary.VS.Data,
                    .pName = "main",
                    .setLayoutCount = 1,
                    .pSetLayouts = &Renderer->DescriptorSetLayout,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .nextStage = 0,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.FS.Size,
                    .pCode = ShaderBinary.FS.Data,
                    .pName = "main",
                    .setLayoutCount = 1,
                    .pSetLayouts = &Renderer->DescriptorSetLayout,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
            };
            constexpr u32 InfoCount = CountOf(Infos);

            VkShaderEXT Shaders[InfoCount];
            Result = vkCreateShadersEXT(Renderer->RenderDevice.Device, InfoCount, Infos, nullptr, Shaders);
            Assert(Result == VK_SUCCESS);
            Renderer->MainVS = Shaders[0];
            Renderer->MainFS = Shaders[1];
            RestoreArena(TransientArena, Checkpoint);
        }
#endif

        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &Renderer->DescriptorSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->PipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return nullptr;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        if (LoadAndCompileShaders("shader/shader", &VSModule, &FSModule) == false)
        {
            FatalError("Failed to load shader");
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBindings[]
        {
            // Vertices
            {
                .binding = 0,
                .stride = sizeof(terrain_vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },

            // (instanced) chunk positions
            {
                .binding = 1,
                .stride = sizeof(vec2),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
            },
        };
        constexpr u32 VertexBindingCount = CountOf(VertexBindings);

        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32_UINT,
                .offset = offsetof(terrain_vertex, P),
            },
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32_UINT,
                .offset = offsetof(terrain_vertex, TexCoord),
            },
            // ChunkP
            {
                .location = ATTRIB_CHUNK_P,
                .binding = 1,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = 0,
            },

        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = VertexBindingCount,
            .pVertexBindingDescriptions = VertexBindings,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfo DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = Renderer->DepthFormat,
            .stencilAttachmentFormat = Renderer->StencilFormat,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->PipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->Pipeline);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
    }

    // ImGui pipeline
    {
        // Sampler
        {
            VkSamplerCreateInfo SamplerInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .mipLodBias = 0.0f,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = {},
                .unnormalizedCoordinates = VK_FALSE,
            };

            VkSampler Sampler;
            if (vkCreateSampler(Renderer->RenderDevice.Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
            {
                Renderer->ImGuiSampler = Sampler;
            }
            else
            {
                return nullptr;
            }
        }

        // Create descriptor set
        {
            VkDescriptorSetLayoutBinding Binding = 
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &Renderer->ImGuiSampler,
            };

            VkDescriptorSetLayoutCreateInfo SetLayoutInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = 1,
                .pBindings = &Binding,
            };
            VkDescriptorSetLayout SetLayout;
            if (vkCreateDescriptorSetLayout(Renderer->RenderDevice.Device, &SetLayoutInfo, nullptr, &SetLayout) == VK_SUCCESS)
            {
                Renderer->ImGuiSetLayout = SetLayout;
            }
            else
            {
                return nullptr;
            }

            VkDescriptorPoolSize PoolSize = 
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
            };

            VkDescriptorPoolCreateInfo PoolInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = 1,
                .pPoolSizes = &PoolSize,
            };
            VkDescriptorPool Pool;
            if (vkCreateDescriptorPool(Renderer->RenderDevice.Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .descriptorPool = Pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &Renderer->ImGuiSetLayout,
                };
                VkDescriptorSet DescriptorSet;
                if (vkAllocateDescriptorSets(Renderer->RenderDevice.Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    Renderer->ImGuiDescriptorPool = Pool;
                    Renderer->ImGuiDescriptorSet = DescriptorSet;
                }
                else
                {
                    return nullptr;
                }
            }
            else
            {
                return nullptr;
            }
        }

#if ENABLE_VK_SHADER_OBJECT
        {
            memory_arena_checkpoint Checkpoint = ArenaCheckpoint(TransientArena);
            shader_bin ShaderBinary = LoadShader("shader/imguishader", TransientArena);
            Assert(ShaderBinary.VS.Size > 0 && ShaderBinary.FS.Size > 0);

            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };

            VkShaderCreateInfoEXT Infos[] = 
            {
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.VS.Size,
                    .pCode = ShaderBinary.VS.Data,
                    .pName = "main",
                    .setLayoutCount = 1,
                    .pSetLayouts = &Renderer->ImGuiSetLayout,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .nextStage = 0,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.FS.Size,
                    .pCode = ShaderBinary.FS.Data,
                    .pName = "main",
                    .setLayoutCount = 1,
                    .pSetLayouts = &Renderer->ImGuiSetLayout,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
            };

            VkShaderEXT Shaders[2];
            Result = vkCreateShadersEXT(Renderer->RenderDevice.Device, CountOf(Infos), Infos, nullptr, Shaders);
            Assert(Result == VK_SUCCESS);
            Renderer->ImGuiVS = Shaders[0];
            Renderer->ImGuiFS = Shaders[1];
            RestoreArena(TransientArena, Checkpoint);
        }
#endif

        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &Renderer->ImGuiSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->ImGuiPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return nullptr;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        if (LoadAndCompileShaders("shader/imguishader", &VSModule, &FSModule) == false)
        {
            FatalError("Failed to load ImGui shader");
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(ImDrawVert),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, pos),
            },
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, uv),
            },
            // Color
            {
                .location = ATTRIB_COLOR,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(ImDrawVert, col),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = Renderer->DepthFormat,
            .stencilAttachmentFormat = Renderer->StencilFormat,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->ImGuiPipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImGuiPipeline);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
    }

    // ImPipeline
    {
#if ENABLE_VK_SHADER_OBJECT
        {
            memory_arena_checkpoint Checkpoint = ArenaCheckpoint(TransientArena);
            shader_bin ShaderBinary = LoadShader("shader/imshader", TransientArena);
            Assert(ShaderBinary.VS.Size > 0 && ShaderBinary.FS.Size > 0);

            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };

            VkShaderCreateInfoEXT Infos[] = 
            {
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.VS.Size,
                    .pCode = ShaderBinary.VS.Data,
                    .pName = "main",
                    .setLayoutCount = 0,
                    .pSetLayouts = nullptr,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
                {
                    .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    .pNext = nullptr,
                    .flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT ,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .nextStage = 0,
                    .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                    .codeSize = ShaderBinary.FS.Size,
                    .pCode = ShaderBinary.FS.Data,
                    .pName = "main",
                    .setLayoutCount = 0,
                    .pSetLayouts = nullptr,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &PushConstants,
                    .pSpecializationInfo = nullptr,
                },
            };

            VkShaderEXT Shaders[2];
            Result = vkCreateShadersEXT(Renderer->RenderDevice.Device, CountOf(Infos), Infos, nullptr, Shaders);
            Assert(Result == VK_SUCCESS);
            Renderer->ImVS = Shaders[0];
            Renderer->ImFS = Shaders[1];
            RestoreArena(TransientArena, Checkpoint);
        }
#endif

        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 0,
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->ImPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return nullptr;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        if (LoadAndCompileShaders("shader/imshader", &VSModule, &FSModule) == false)
        {
            FatalError("Failed to load immediate mode shader");
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, P),
            },
#if 0
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, UVW),
            },
#endif
            // Color
            {
                .location = ATTRIB_COLOR,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(vertex, Color),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_TRUE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = Renderer->DepthFormat,
            .stencilAttachmentFormat = Renderer->StencilFormat,
        };

        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->ImPipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImPipeline);
        if (Result != VK_SUCCESS)
        {
            return nullptr;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
    }

    if (!Renderer_CreateVoxelTextureArray(Renderer, RendererInfo->TextureWidth, RendererInfo->TextureHeight,
                                          RendererInfo->TextureMipCount, RendererInfo->TextureArrayCount, (u8*)RendererInfo->TextureData))
    {
        return nullptr;
    }

    if (!Renderer_CreateImGuiTexture(Renderer, RendererInfo->ImGuiTextureWidth, RendererInfo->ImGuiTextureHeight, 
                                     (u8*)RendererInfo->ImGuiTextureData))
    {
        return nullptr;
    }
    else
    {
        ImGui::GetIO().Fonts->SetTexID((ImTextureID)(u64)Renderer->ImGuiTextureID);
    }

    return(Renderer);
}

bool Renderer_CreateVoxelTextureArray(renderer* Renderer, 
                                      u32 Width, u32 Height, 
                                      u32 MipCount, u32 ArrayCount,
                                      const u8* Data)
{
    bool Result = false;

    Assert(Renderer->Tex == VK_NULL_HANDLE &&
           Renderer->TexView == VK_NULL_HANDLE &&
           Renderer->TexMemory == VK_NULL_HANDLE);

    VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;

    VkImageCreateInfo ImageInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = Format,
        .extent = { Width, Height, 1 },
        .mipLevels = MipCount,
        .arrayLayers = ArrayCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,

    };
    VkImage Image = VK_NULL_HANDLE;
    if (vkCreateImage(Renderer->RenderDevice.Device, &ImageInfo, nullptr, &Image) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Renderer->RenderDevice.Device, Image, &MemoryRequirements);

        u32 MemoryTypes = Renderer->RenderDevice.MemoryTypes.DeviceLocal;
        MemoryTypes &= MemoryRequirements.memoryTypeBits;

        u32 MemoryTypeIndex = 0;
        if (BitScanForward(&MemoryTypeIndex, MemoryTypes))
        {
            VkMemoryAllocateInfo AllocInfo =
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = MemoryRequirements.size,
                .memoryTypeIndex = MemoryTypeIndex,
            };

            VkDeviceMemory Memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindImageMemory(Renderer->RenderDevice.Device, Image, Memory, 0) == VK_SUCCESS)
                {
                    VkImageViewCreateInfo ViewInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .image = Image,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                        .format = Format,
                        .components = {},
                        .subresourceRange = 
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = MipCount,
                            .baseArrayLayer = 0,
                            .layerCount = ArrayCount,
                        },
                    };
                    VkImageView View = VK_NULL_HANDLE;
                    if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &View) == VK_SUCCESS)
                    {
                        if (StagingHeap_CopyImage(
                            &Renderer->StagingHeap, Renderer->RenderDevice.TransferQueue,
                            Renderer->TransferCmdBuffer,
                            Image,
                            Width, Height, MipCount, ArrayCount, Format, Data))
                        {
                            VkDescriptorImageInfo ImageDescriptor = 
                            {
                                .sampler = VK_NULL_HANDLE,
                                .imageView = View,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            };
                            VkWriteDescriptorSet DescriptorWrite = 
                            {
                                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .pNext = nullptr,
                                .dstSet = Renderer->DescriptorSet,
                                .dstBinding = 0,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                .pImageInfo = &ImageDescriptor,
                                .pBufferInfo = nullptr,
                                .pTexelBufferView = nullptr,
                            };
                            vkUpdateDescriptorSets(Renderer->RenderDevice.Device, 1, &DescriptorWrite, 0, nullptr);

                            Renderer->Tex = Image;
                            Renderer->TexView = View;
                            Renderer->TexMemory = Memory;

                            Memory = VK_NULL_HANDLE;
                            View = VK_NULL_HANDLE;
                            Image = VK_NULL_HANDLE;

                            Result = true;
                        }
                        vkDestroyImageView(Renderer->RenderDevice.Device, View, nullptr);
                    }
                }
            }
            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
        }
        vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
    }

    return(Result);
}

bool Renderer_CreateImGuiTexture(renderer* Renderer, u32 Width, u32 Height, const u8* Data)
{
    assert(Renderer);
    bool Result = false;

    VkImageCreateInfo ImageInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { Width, Height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage Image;
    if (vkCreateImage(Renderer->RenderDevice.Device, &ImageInfo, nullptr, &Image) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Renderer->RenderDevice.Device, Image, &MemoryRequirements);

        u32 MemoryTypes = Renderer->RenderDevice.MemoryTypes.DeviceLocal & MemoryRequirements.memoryTypeBits;
        u32 MemoryType = 0;
        if (BitScanForward(&MemoryType, MemoryTypes) != 0)
        {
            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = MemoryRequirements.size,
                .memoryTypeIndex = MemoryType,
            };
            VkDeviceMemory Memory;
            if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindImageMemory(Renderer->RenderDevice.Device, Image, Memory, 0) == VK_SUCCESS)
                {
                    VkImageViewCreateInfo ViewInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .image = Image,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D,
                        .format = VK_FORMAT_R8_UNORM,
                        .components = 
                        {
                            .r = VK_COMPONENT_SWIZZLE_ONE,
                            .g = VK_COMPONENT_SWIZZLE_ONE,
                            .b = VK_COMPONENT_SWIZZLE_ONE,
                            .a = VK_COMPONENT_SWIZZLE_R,
                        },
                        .subresourceRange = 
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    };

                    if (StagingHeap_CopyImage(
                            &Renderer->StagingHeap, 
                            Renderer->RenderDevice.TransferQueue, 
                            Renderer->TransferCmdBuffer, 
                            Image,
                            Width, Height, 1, 1, VK_FORMAT_R8_UNORM,
                            Data))
                        {
                            VkImageView ImageView;
                            if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
                            {
                                VkDescriptorImageInfo DescriptorImageInfo = 
                                {
                                    .sampler = VK_NULL_HANDLE,
                                    .imageView = ImageView,
                                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                };
                                VkWriteDescriptorSet DescriptorWrite = 
                                {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .pNext = nullptr,
                                    .dstSet = Renderer->ImGuiDescriptorSet,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo = &DescriptorImageInfo,
                                    .pBufferInfo = nullptr,
                                    .pTexelBufferView = nullptr,
                                };
                                vkUpdateDescriptorSets(Renderer->RenderDevice.Device, 1, &DescriptorWrite, 0, nullptr);

                                Renderer->ImGuiTex = Image;
                                Renderer->ImGuiTexMemory = Memory;
                                Renderer->ImGuiTexView = ImageView;
                                Result = true;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                        }
                }
                else
                {
                    vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                    vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                }
            }
            else
            {
                vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
            }
        }
        else
        {
            vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
        }
    }

    return Result;
}

static bool Renderer_InitializeFrameParams(renderer* Renderer)
{
    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        vulkan_render_frame* Frame = Renderer->FrameParams + i;
        {
            VkCommandPoolCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = Renderer->RenderDevice.GraphicsFamilyIndex,
            };
            if (vkCreateCommandPool(Renderer->RenderDevice.Device, &Info, nullptr, &Frame->CmdPool) != VK_SUCCESS)
            {
                return false;
            }
        }
        
        {
            VkCommandBufferAllocateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = Frame->CmdPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &Info, &Frame->PrimaryCmdBuffer) != VK_SUCCESS)
            {
                return false;
            }
            if (vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &Info, &Frame->TransferCmdBuffer) != VK_SUCCESS)
            {
                return false;
            }
            
            VkCommandBufferAllocateInfo SecondaryInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = Frame->CmdPool,
                .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &SecondaryInfo, &Frame->SceneCmdBuffer) != VK_SUCCESS)
            {
                return false;
            }
            if (vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &SecondaryInfo, &Frame->ImmediateCmdBuffer) != VK_SUCCESS)
            {
                return false;
            }
            if (vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &SecondaryInfo, &Frame->ImGuiCmdBuffer) != VK_SUCCESS)
            {
                return false;
            }
        }

        {
            VkFenceCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            if (vkCreateFence(Renderer->RenderDevice.Device, &Info, nullptr, &Frame->RenderFinishedFence) != VK_SUCCESS)
            {
                return false;
            }
        }
        {
            VkSemaphoreCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };
            if (vkCreateSemaphore(Renderer->RenderDevice.Device, &Info, nullptr, &Frame->ImageAcquiredSemaphore) != VK_SUCCESS)
            {
                return false;
            }
            if (vkCreateSemaphore(Renderer->RenderDevice.Device, &Info, nullptr, &Frame->RenderFinishedSemaphore) != VK_SUCCESS)
            {
                return false;
            }
            if (vkCreateSemaphore(Renderer->RenderDevice.Device, &Info, nullptr, &Frame->TransferFinishedSemaphore) != VK_SUCCESS)
            {
                return false;
            }
        }
        // Create per frame vertex stack
        {
            u64 VertexSize = MiB(64);
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = VertexSize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };
            VkBuffer Buffer;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == VertexSize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = VertexSize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            // NOTE(boti): persistent mapping
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, VK_WHOLE_SIZE, 0, &Mapping) == VK_SUCCESS)
                            {
                                Frame->VertexMemory = Memory;
                                Frame->VertexBuffer = Buffer;
                                Frame->VertexSize = VertexSize;
                                Frame->VertexOffset = 0;
                                Frame->VertexMapping = Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Create per frame (indirect) command buffer
        u32 DrawCountPerFrame = 1u << 18;
        {
            u64 DrawMemorySize = (u64)DrawCountPerFrame * sizeof(draw_cmd);
            VkBufferCreateInfo BufferInfo =
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = DrawMemorySize,
                .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer Buffer = VK_NULL_HANDLE;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == DrawMemorySize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = DrawMemorySize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, DrawMemorySize, 0, &Mapping) == VK_SUCCESS)
                            {
                                // TODO(boti): We should only set this if both the draw buffer and the instance buffer
                                //             have been successfully created.
                                Frame->MaxDrawCount = DrawCountPerFrame;
                                Frame->DrawMemory = Memory;
                                Frame->DrawBuffer = Buffer;
                                Frame->DrawMapping = Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Create per frame instance data
        {
            u64 InstanceDataMemorySize = DrawCountPerFrame * sizeof(vec2);
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = InstanceDataMemorySize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer Buffer = VK_NULL_HANDLE;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == InstanceDataMemorySize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = InstanceDataMemorySize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, InstanceDataMemorySize, 0, &Mapping) == VK_SUCCESS)
                            {
                                Frame->PositionMemory = Memory;
                                Frame->PositionBuffer = Buffer;
                                Frame->PositionMapping = Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    return true;
}