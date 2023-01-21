#pragma once

//#include <vulkan/vulkan.h>
//#include <Common.hpp>

struct vulkan_vertex_buffer_block
{
    u32 VertexCount;
    u32 VertexOffset;

    vulkan_vertex_buffer_block* Next;
    vulkan_vertex_buffer_block* Prev;
};

struct vulkan_vertex_buffer
{
    static constexpr u32 MaxAllocationCount = 8192;
    memory_arena* Arena;

    u64 MemorySize;
    u64 MemoryUsage;
    VkDeviceMemory Memory;
    VkBuffer Buffer;
    u32 MaxVertexCount;
    
    vulkan_vertex_buffer_block FreeBlockSentinel;
    vulkan_vertex_buffer_block UsedBlockSentinel;
    vulkan_vertex_buffer_block BlockPoolSentinel;
};

bool VB_Create(vulkan_vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device, memory_arena* Arena);

vulkan_vertex_buffer_block* VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount);
void VB_Free(vulkan_vertex_buffer* VB, u32 AllocationIndex);

void VB_Defragment(vulkan_vertex_buffer* VB);

u64 VB_GetAllocationMemoryOffset(vulkan_vertex_buffer_block* Block);