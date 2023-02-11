#pragma once

//#include <vulkan/vulkan.hpp>
//#include <Common.hpp>

enum class resource_type : u32
{
    Undefined = 0,
    Image,
    Buffer,
};

struct copy_operation
{
    struct 
    {
        resource_type Type;
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

struct staging_heap
{
    VkDevice Device;

    u64 HeapSize;
    u64 HeapOffset;
    u64 Granularity;
    VkDeviceMemory Heap;
    VkBuffer Buffer;

    u64 OutstandingCopyOps; // Bitmask
    copy_operation CopyOps[64];
};

bool StagingHeap_Create(
    staging_heap* Heap,
    u64 Size,
    struct renderer* Renderer);

bool StagingHeap_Copy(
    staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    u64 DestOffset, VkBuffer Dest,
    u64 SrcSize, const void* Src);

bool StagingHeap_CopyImage(
    staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    VkImage Image,
    u32 Width, u32 Height, u32 MipCount, u32 ArrayCount,
    VkFormat Format,
    const void* Src);
