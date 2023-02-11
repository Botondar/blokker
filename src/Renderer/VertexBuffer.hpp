#pragma once

//#include <vulkan/vulkan.h>
//#include <Common.hpp>

struct vertex_buffer_block
{
    u32 VertexCount;
    u32 VertexOffset;

    vertex_buffer_block* Next;
    vertex_buffer_block* Prev;
};

struct vertex_buffer
{
    static constexpr u32 MaxAllocationCount = 8192;
    memory_arena* Arena;

    u64 MemorySize;
    u64 MemoryUsage;
    VkDeviceMemory Memory;
    VkBuffer Buffer;
    u32 MaxVertexCount;
    
    vertex_buffer_block FreeBlockSentinel;
    vertex_buffer_block UsedBlockSentinel;
    vertex_buffer_block BlockPoolSentinel;
};

bool VB_Create(vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device, memory_arena* Arena);

vertex_buffer_block* VB_Allocate(vertex_buffer* VB, u32 VertexCount);
void VB_Free(vertex_buffer* VB, vertex_buffer_block* Block);

void VB_Defragment(vertex_buffer* VB);

u64 VB_GetAllocationMemoryOffset(vertex_buffer_block* Block);