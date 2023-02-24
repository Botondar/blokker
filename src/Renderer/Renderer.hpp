#pragma once

#define BLOKKER_RENDERER_REWRITE 1

#include <Common.hpp>
#include <Intrinsics.hpp>
#include <Math.hpp>
#include <Platform.hpp>
#include <Shapes.hpp>

#include <Chunk.hpp>
#include <Camera.hpp>

#include <Renderer/RenderDevice.hpp>

#include <Renderer/RendererCommon.hpp>
#include <Renderer/RTHeap.hpp>
#include <Renderer/StagingHeap.hpp>
#include <Renderer/VertexBuffer.hpp>

extern platform_api Platform;

struct render_frame
{
    u64 FrameIndex;
    u32 BufferIndex;

    VkExtent2D RenderExtent;

    camera Camera;
    mat4 ProjectionTransform;
    mat4 ViewTransform;
    mat4 PixelTransform;

    VkCommandPool CmdPool;
    VkCommandBuffer PrimaryCmdBuffer;
    VkCommandBuffer TransferCmdBuffer;

    VkCommandBuffer SceneCmdBuffer;
    VkCommandBuffer ImmediateCmdBuffer;
    VkCommandBuffer ImGuiCmdBuffer;

    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderFinishedSemaphore;

    VkSemaphore TransferFinishedSemaphore;

    VkFence RenderFinishedFence;

    VkImage DepthBuffer;
    VkImageView DepthBufferView;

    VkImage SwapchainImage;
    VkImageView SwapchainImageView;
    u32 SwapchainImageIndex;

    struct
    {
        VkBuffer Buffer;
        u64 BufferSize;

        VkDeviceMemory Memory; // Shared between all the frame params
        
        void* Mapping;
    } FrameUniformBuffer;

    struct 
    {
        static constexpr u64 VertexStackSize = MiB(64);

        VkDeviceMemory Memory;
        VkBuffer Buffer;

        u64 Size;
        u64 At;

        void* Mapping;
    } VertexStack;

    struct 
    {
        static constexpr u64 MaxDrawCount = 128 * 1024;
        static constexpr u64 MemorySize = MaxDrawCount * sizeof(VkDrawIndirectCommand);

        VkDeviceMemory Memory;
        VkBuffer Buffer;

        u64 DrawIndex;
        VkDrawIndirectCommand* Commands;
    } DrawCommands;

    struct 
    {
        static constexpr u64 MemorySize = MiB(4);
        static constexpr u64 MaxChunkCount = MemorySize / sizeof(vec2);

        VkDeviceMemory Memory;
        VkBuffer Buffer;

        u64 ChunkAt;
        vec2* Mapping;
    } ChunkPositions;

    struct renderer* Renderer;
};

u64 Frame_PushToStack(render_frame* Frame, u64 Alignment, const void* Data, u64 Size);

struct renderer 
{
    render_device RenderDevice;

    VkSurfaceFormatKHR SurfaceFormat;
    VkSurfaceKHR Surface;
    VkSwapchainKHR Swapchain;
    VkSampleCountFlagBits Multisampling;
    u32 SwapchainImageCount;
    VkExtent2D SwapchainSize;
    static constexpr u32 MaxSwapchainImageCount = 16;
    VkImage SwapchainImages[MaxSwapchainImageCount];
    VkImageView SwapchainImageViews[MaxSwapchainImageCount];

    VkCommandPool TransferCmdPool;
    VkCommandBuffer TransferCmdBuffer;

    // Offscreen render buffers
    VkImage DepthBuffer;
    VkImageView DepthBufferView;
    render_target_heap RTHeap;

    VkFormat DepthFormat;
    VkFormat StencilFormat;

    render_frame FrameParams[MaxSwapchainImageCount];

    staging_heap StagingHeap;

    vertex_buffer VB;

    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;

    VkPipelineLayout ImPipelineLayout;
    VkPipeline ImPipeline;

    VkPipelineLayout ImGuiPipelineLayout;
    VkPipeline ImGuiPipeline;

    VkSampler Sampler;
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorPool DescriptorPool;
    VkDescriptorSet DescriptorSet;

    VkSampler ImGuiSampler;
    VkDescriptorSetLayout ImGuiSetLayout;
    VkDescriptorPool ImGuiDescriptorPool;
    VkDescriptorSet ImGuiDescriptorSet;

    VkImage Tex;
    VkImageView TexView;
    VkDeviceMemory TexMemory;

    VkImage ImGuiTex;
    VkImageView ImGuiTexView;
    VkDeviceMemory ImGuiTexMemory;

    static constexpr u32 ImGuiTextureID = 1;

    u64 CurrentFrameIndex;
};

bool Renderer_ResizeRenderTargets(renderer* Renderer);
bool Renderer_Initialize(renderer* Renderer, memory_arena* Arena);

// NOTE(boti): Texture data must be RGBA8 format
bool Renderer_CreateVoxelTextureArray(renderer* Renderer, 
                                      u32 Width, u32 Height, 
                                      u32 MipCount, u32 ArrayCount,
                                      const u8* Data);
bool Renderer_CreateImGuiTexture(renderer* Renderer, u32 Width, u32 Height, const u8* Data);

render_frame* Renderer_NewFrame(renderer* Renderer, bool DoResize);
void Renderer_SubmitFrame(renderer* Renderer, render_frame* Frame);

bool Renderer_UploadVertexData(render_frame* Frame, 
                               vertex_buffer_block* Block,
                               u64 DataSize0, const void* Data0,
                               u64 DataSize1, const void* Data1);

bool Renderer_PushTriangleList(render_frame* Frame, 
                               u32 VertexCount, const vertex* VertexData, 
                               mat4 Transform, f32 DepthBias);

void Renderer_RenderChunks(render_frame* Frame, u32 Count, chunk_render_data* Chunks);

enum class outline_type : u32
{
    Outer = 0,
    Inner,
};

void Renderer_BeginImmediate(render_frame* Frame);
void Renderer_ImmediateBox(render_frame* Frame, aabb Box, u32 Color, f32 DepthBias = 0.0f);
void Renderer_ImmediateBoxOutline(render_frame* Frame, f32 OutlineSize, aabb Box, u32 Color);
void Renderer_ImmediateRect2D(render_frame* Frame, vec2 p0, vec2 p1, u32 Color);
void Renderer_ImmediateRectOutline2D(render_frame* Frame, outline_type Type, f32 OutlineSize, vec2 p0, vec2 p1, u32 Color);

void Renderer_RenderImGui(render_frame* Frame, const ImDrawData* DrawData);