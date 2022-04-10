#pragma once

//#include <vulkan/vulkan.h>
//#include <Common.hpp>

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
