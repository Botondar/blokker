#pragma once

#include <vulkan/vulkan.h>

#include <Common.hpp>
#include <Math.hpp>
#include <Shapes.hpp>

#include <Chunk.hpp>
#include <Camera.hpp>

#include <Renderer/RendererCommon.hpp>
#include <Renderer/RTHeap.hpp>
#include <Renderer/StagingHeap.hpp>
#include <Renderer/VertexBuffer.hpp>

struct vulkan_renderer;

struct vulkan_physical_device_desc 
{
    static constexpr u32 MaxQueueFamilyCount = 8;
    
    u32 QueueFamilyCount;
    VkQueueFamilyProperties QueueFamilyProps[MaxQueueFamilyCount];
    VkPhysicalDeviceProperties Props;
    VkPhysicalDeviceMemoryProperties MemoryProps;
};

struct renderer_frame_params
{
    u64 FrameIndex;
    u32 BufferIndex;

    camera Camera;
    mat4 ProjectionTransform;
    mat4 ViewTransform;

    VkCommandPool CmdPool;
    VkCommandBuffer CmdBuffer;

    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderFinishedSemaphore;

    VkFence RenderFinishedFence;

    VkImage DepthBuffer;
    VkImageView DepthBufferView;

    VkImage SwapchainImage;
    VkImageView SwapchainImageView;
    u32 SwapchainImageIndex;

    struct 
    {
        VkDeviceMemory Memory;
        VkBuffer Buffer;

        u64 Size;
        u64 At;

        void* Mapping;
    } VertexStack;

    vulkan_renderer* Renderer;
};

u64 Frame_PushToStack(renderer_frame_params* Frame, u64 Alignment, const void* Data, u64 Size);

struct vulkan_renderer 
{
    VkInstance Instance;
    VkDebugUtilsMessengerEXT DebugMessenger;

    VkPhysicalDevice PhysicalDevice;
    vulkan_physical_device_desc DeviceDesc;
    u32 DeviceLocalMemoryTypes;
    u32 HostVisibleMemoryTypes;
    u32 HostVisibleCoherentMemoryTypes;
    u32 DeviceLocalAndHostVisibleMemoryTypes;
    
    u64 NonCoherentAtomSize;
    VkDevice Device;
    
    u32 GraphicsFamilyIndex;
    u32 TransferFamilyIndex;
    VkQueue GraphicsQueue;
    VkQueue TransferQueue;

    VkSurfaceFormatKHR SurfaceFormat;
    VkSurfaceKHR Surface;
    VkSwapchainKHR Swapchain;
    VkSampleCountFlagBits Multisampling;
    u32 SwapchainImageCount;
    VkExtent2D SwapchainSize;
    VkImage SwapchainImages[16];
    VkImageView SwapchainImageViews[16];

    VkCommandPool TransferCmdPool;
    VkCommandBuffer TransferCmdBuffer;

    // Offscreen render buffers
    struct 
    {
        VkImage DepthBuffers[2];
        VkImageView DepthBufferViews[2];
    };
    vulkan_rt_heap RTHeap;

    u32 NextBufferIndex;
    renderer_frame_params FrameParams[16];

    vulkan_staging_heap StagingHeap;

    vulkan_vertex_buffer VB;

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
};

bool Renderer_ResizeRenderTargets(vulkan_renderer* Renderer);
bool Renderer_Initialize(vulkan_renderer* Renderer);

bool Renderer_CreateImGuiTexture(vulkan_renderer* Renderer, u32 Width, u32 Height, const u8* Data);

renderer_frame_params* Renderer_NewFrame(vulkan_renderer* Renderer);
void Renderer_SubmitFrame(vulkan_renderer* Renderer, renderer_frame_params* Frame);

void Renderer_BeginRendering(renderer_frame_params* Frame);
void Renderer_EndRendering(renderer_frame_params* Frame);

void Renderer_RenderChunks(renderer_frame_params* Frame, u32 Count, chunk_render_data* Chunks);

enum class outline_type : u32
{
    Outer = 0,
    Inner,
};

void Renderer_BeginImmediate(renderer_frame_params* Frame);
void Renderer_ImmediateBox(renderer_frame_params* Frame, aabb Box, u32 Color);
void Renderer_ImmediateBoxOutline(renderer_frame_params* Frame, f32 OutlineSize, aabb Box, u32 Color);
void Renderer_ImmediateRect2D(renderer_frame_params* Frame, vec2 p0, vec2 p1, u32 Color);
void Renderer_ImmediateRectOutline2D(renderer_frame_params* Frame, outline_type Type, f32 OutlineSize, vec2 p0, vec2 p1, u32 Color);

void Renderer_RenderImGui(renderer_frame_params* Frame);