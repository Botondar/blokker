#pragma once

//#include <vulkan/vulkan.h>
//#include <Common.hpp>

enum vb_block_flags : u32
{
    VBBLOCK_ALLOCATED_BIT = 1,
};

struct vulkan_vertex_buffer_block
{
    u32 VertexCount;
    u32 VertexOffset;
    u32 Flags; // vb_block_flags
    u32 AllocationIndex;
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

bool VB_Create(vulkan_vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device);

u32 VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount);
void VB_Free(vulkan_vertex_buffer* VB, u32 AllocationIndex);

void VB_Defragment(vulkan_vertex_buffer* VB);

u64 VB_GetAllocationMemoryOffset(const vulkan_vertex_buffer* VB, u32 AllocationIndex);