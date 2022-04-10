#include "VertexBuffer.hpp"

bool VB_Create(vulkan_vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device)
{
    assert(VB);
    memset(VB, 0, sizeof(vulkan_vertex_buffer));
    bool Result = false;

    Size = AlignTo(Size, sizeof(terrain_vertex));
    u64 VertexCount64 = Size / sizeof(terrain_vertex);
    assert(VertexCount64 <= 0xFFFFFFFF);

    u32 VertexCount = (u32)VertexCount64;

    VkBufferCreateInfo BufferInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = Size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer Buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Device, Buffer, &MemoryRequirements);
        if (Size < MemoryRequirements.size)
        {
            Size = MemoryRequirements.size;
        }

        MemoryTypes &= MemoryRequirements.memoryTypeBits;
        u32 MemoryType = 0;
        if (BitScanForward(&MemoryType, MemoryTypes) != 0)
        {

            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = Size,
                .memoryTypeIndex = MemoryType,
            };

            VkDeviceMemory Memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindBufferMemory(Device, Buffer, Memory, 0) == VK_SUCCESS)
                {
                    VB->MemorySize = Size;
                    VB->Memory = Memory;
                    VB->Buffer = Buffer;
                    VB->MaxVertexCount = VertexCount;
                    
                    // Init memory blocks
                    {
                        VB->BlockCount = 1;
                        VB->Blocks[0] = 
                        {
                            .VertexCount = VB->MaxVertexCount,
                            .VertexOffset = 0,
                            .Flags = 0,
                        };
                    }

                    // Init allocation queue
                    VB->FreeAllocationRead = 0;
                    VB->FreeAllocationWrite = 0;
                    VB->FreeAllocationCount = vulkan_vertex_buffer::MaxAllocationCount;
                    for (u32 i = 0; i < vulkan_vertex_buffer::MaxAllocationCount; i++)
                    {
                        VB->FreeAllocationIndices[i] = i;
                    }

                    Result = true;
                }
                else
                {
                    vkFreeMemory(Device, Memory, nullptr);
                    vkDestroyBuffer(Device, Buffer, nullptr);
                }
            }
            else
            {
                vkDestroyBuffer(Device, Buffer, nullptr);
            }
        }
    }

    return Result;
}

u32 VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount)
{
    assert(VB);
    
    u32 Result = INVALID_INDEX_U32;
    if (VB->FreeAllocationCount)
    {
        u32 AllocationIndex = VB->FreeAllocationIndices[VB->FreeAllocationRead];
        vulkan_vertex_buffer_allocation* Allocation = VB->Allocations + AllocationIndex;
        Allocation->BlockIndex = INVALID_INDEX_U32;

        auto FindAllocation = [&]() -> void
        {
            for (u32 i = 0; i < VB->BlockCount; i++)
            {
                vulkan_vertex_buffer_block* Block = VB->Blocks + i;
                if (!(Block->Flags & VBBLOCK_ALLOCATED_BIT) && (VertexCount <= Block->VertexCount))
                {
                    if (Block->VertexCount == VertexCount)
                    {
                        Block->Flags |= VBBLOCK_ALLOCATED_BIT;
                        Allocation->BlockIndex = i;
                    }
                    else
                    {
                        if (VB->BlockCount != vulkan_vertex_buffer::MaxAllocationCount)
                        {
                            vulkan_vertex_buffer_block* NextBlock = &VB->Blocks[VB->BlockCount++];
                            assert(!(NextBlock->Flags & VBBLOCK_ALLOCATED_BIT));

                            NextBlock->VertexCount = Block->VertexCount - VertexCount;
                            NextBlock->VertexOffset = Block->VertexOffset + VertexCount;
                            NextBlock->Flags &= ~VBBLOCK_ALLOCATED_BIT;
                            NextBlock->AllocationIndex = INVALID_INDEX_U32;

                            Block->VertexCount = VertexCount;
                            Block->Flags |= VBBLOCK_ALLOCATED_BIT;
                            Block->AllocationIndex = AllocationIndex;

                            Allocation->BlockIndex = i;
                        }
                        else
                        {
                            assert(!"Unimplemented code path");
                        }
                    }

                    break;
                }
            }
        };
        FindAllocation();

        // Defragment and try again
        if (Allocation->BlockIndex == INVALID_INDEX_U32)
        {
            VB_Defragment(VB);
            FindAllocation();
        }

        if (Allocation->BlockIndex != INVALID_INDEX_U32)
        {
            VB->FreeAllocationRead++;
            VB->FreeAllocationRead %= vulkan_vertex_buffer::MaxAllocationCount;
            VB->FreeAllocationCount--;

            VB->MemoryUsage += (u64)VertexCount * sizeof(terrain_vertex);

            Result = AllocationIndex;
        }
        else
        {
            assert(!"Fatal error");
        }
    }
    return Result;
}

void VB_Free(vulkan_vertex_buffer* VB, u32 AllocationIndex)
{
    assert(VB);
    
    if (AllocationIndex != INVALID_INDEX_U32)
    {
        assert(AllocationIndex < vulkan_vertex_buffer::MaxAllocationCount);
        
        vulkan_vertex_buffer_block* Block = VB->Blocks + VB->Allocations[AllocationIndex].BlockIndex;

        Block->Flags &= ~VBBLOCK_ALLOCATED_BIT;
        Block->AllocationIndex = INVALID_INDEX_U32;

        VB->FreeAllocationIndices[VB->FreeAllocationWrite++] = AllocationIndex;
        VB->FreeAllocationWrite %= vulkan_vertex_buffer::MaxAllocationCount;
        VB->FreeAllocationCount++;

        VB->MemoryUsage -= (u64)Block->VertexCount * sizeof(terrain_vertex);
    }
}

void VB_Defragment(vulkan_vertex_buffer* VB)
{
    TIMED_FUNCTION();

    assert(VB);

    u32 FreeBlockCount = 0;
    u32 FreeBlocks[VB->MaxAllocationCount];
    u32 AllocatedBlockCount = 0;
    u32 AllocatedBlocks[VB->MaxAllocationCount];

    for (u32 i = 0; i < VB->BlockCount; i++)
    {
        vulkan_vertex_buffer_block* Block = VB->Blocks + i;
        if (Block->Flags & VBBLOCK_ALLOCATED_BIT)
        {
            AllocatedBlocks[AllocatedBlockCount++] = i;
        }
        else
        {
            FreeBlocks[FreeBlockCount++] = i;
        }
    }

    assert(!"Unimplemented code path");
}

u64 VB_GetAllocationMemoryOffset(const vulkan_vertex_buffer* VB, u32 AllocationIndex)
{
    assert(VB);

    u32 BlockIndex = VB->Allocations[AllocationIndex].BlockIndex;
    u64 Result = (u64)VB->Blocks[BlockIndex].VertexOffset * sizeof(terrain_vertex);
    return Result;
}