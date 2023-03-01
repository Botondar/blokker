#pragma once

struct render_target_heap
{
    VkDevice Device;
    VkDeviceMemory Heap;
    u64 HeapSize;
    u64 HeapOffset;
    u32 MemoryTypeIndex;
};

bool RTHeap_Create(
    render_target_heap* Heap, 
    u64 Size, u32 MemoryTypeBase,
    VkDevice Device);

bool RTHeap_PushImage(
    render_target_heap* Heap,
    VkImage Image);
