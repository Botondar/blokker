#pragma once

#include <Common.hpp>
#include <vulkan/vulkan.h>

#include <Math.hpp>

inline constexpr u32 PackColor(u32 R, u32 G, u32 B, u32 A = 0xFF)
{
    R &= 0xFF;
    G &= 0xFF;
    B &= 0xFF;
    A &= 0xFF;

    u32 Result = 
        (A << 24) |
        (B << 16) |
        (G << 8) |
        (R << 0);
    return Result;
}

inline u32 PackColor(const vec3& v)
{
    u32 R = (u32)Round(255.0f * Clamp(v.x, 0.0f, 1.0f));
    u32 G = (u32)Round(255.0f * Clamp(v.x, 0.0f, 1.0f));
    u32 B = (u32)Round(255.0f * Clamp(v.x, 0.0f, 1.0f));

    u32 Result = PackColor(R, G, B);
    return Result;
}

struct vertex 
{
    vec3 P;
    vec3 UVW;
    u32 Color;
};

extern PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR_;
extern PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR_;

#define vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR_
#define vkCmdEndRenderingKHR vkCmdEndRenderingKHR_

struct vulkan_renderer;

struct vulkan_physical_device_desc 
{
    static constexpr u32 MaxQueueFamilyCount = 8;
    
    u32 QueueFamilyCount;
    VkQueueFamilyProperties QueueFamilyProps[MaxQueueFamilyCount];
    VkPhysicalDeviceProperties Props;
    VkPhysicalDeviceMemoryProperties MemoryProps;
};

struct vulkan_rt_heap
{
    VkDevice Device;
    VkDeviceMemory Heap;
    u64 HeapSize;
    u64 HeapOffset;
    u32 MemoryTypeIndex;
};


bool RTHeap_Create(
    vulkan_rt_heap* Heap, 
    u64 Size, u32 MemoryTypeBase, 
    u32 MemoryRequirementCount,
    const VkMemoryRequirements* MemoryRequirements,
    VkDevice Device);

bool RTHeap_PushImage(
    vulkan_rt_heap* Heap,
    VkImage Image);


enum class vulkan_resource_type : u32
{
    Undefined = 0,
    Image,
    Buffer,
};

struct vulkan_copy_operation
{
    struct 
    {
        vulkan_resource_type Type;
        union
        {
            VkImage Image;
            VkBuffer Buffer;
        };
    } Destination;
    u64 Offset;
    u64 Size;
    VkSemaphore Semaphore;
};

struct vulkan_staging_heap
{
    VkDevice Device;

    u64 HeapSize;
    u64 HeapOffset;
    u64 Granularity;
    VkDeviceMemory Heap;
    VkBuffer Buffer;

    u64 OutstandingCopyOps; // Bitmask
    vulkan_copy_operation CopyOps[64];
};

bool StagingHeap_Create(
    vulkan_staging_heap* Heap,
    u64 Size,
    vulkan_renderer* Renderer);

bool StagingHeap_Copy(
    vulkan_staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    u64 DestOffset, VkBuffer Dest,
    u64 SrcSize, const void* Src);

bool StagingHeap_CopyImage(
    vulkan_staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    VkImage Image,
    u32 Width, u32 Height, u32 ArrayCount,
    VkFormat Format,
    const void* Src);

enum vb_block_flags : u32
{
    VBBLOCK_ALLOCATED_BIT = 1,
};

struct vulkan_vertex_buffer_block
{
    u32 VertexCount;
    u32 VertexOffset;
    u32 Flags; // vb_block_flags
};

struct vulkan_vertex_buffer_allocation
{
    u32 BlockIndex;
};

struct vulkan_vertex_buffer
{
    static constexpr u32 MaxAllocationCount = 8192;
    
    u64 MemorySize;
    u64 MemoryUsage;
    VkDeviceMemory Memory;
    VkBuffer Buffer;
    u32 MaxVertexCount;
    
    // Allocation queue
    u32 FreeAllocationRead;
    u32 FreeAllocationWrite;
    u32 FreeAllocationCount;
    u32 FreeAllocationIndices[MaxAllocationCount];

    vulkan_vertex_buffer_allocation Allocations[MaxAllocationCount];

    u32 BlockCount;
    vulkan_vertex_buffer_block Blocks[MaxAllocationCount];
};

bool VB_Create(vulkan_vertex_buffer* VB, u64 Size, VkDevice Device);

u32 VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount);
void VB_Free(vulkan_vertex_buffer* VB, u32 AllocationIndex);

void VB_Defragment(vulkan_vertex_buffer* VB);

u64 VB_GetAllocationMemoryOffset(const vulkan_vertex_buffer* VB, u32 AllocationIndex);


// NOTE(boti): this _doesn't_ contain the swapchain images because that gets indexed
//             separately by the result of vkAcquireNextImageKHR
struct renderer_frame_params
{
    u64 FrameIndex;

    VkCommandPool CmdPool;
    VkCommandBuffer CmdBuffer;

    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderFinishedSemaphore;

    VkFence RenderFinishedFence;

    VkImage DepthBuffer;
    VkImageView DepthBufferView;
};

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

    u64 LatestFrameIndex;
    renderer_frame_params FrameParams[16];
};

bool Renderer_ResizeRenderTargets(vulkan_renderer* Renderer);
bool Renderer_Initialize(vulkan_renderer* Renderer);