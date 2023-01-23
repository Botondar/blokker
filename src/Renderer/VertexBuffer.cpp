#include "VertexBuffer.hpp"

static void VerifyListIntegrity(vulkan_vertex_buffer_block* Sentinel)
{
    for (vulkan_vertex_buffer_block* It = Sentinel->Next;
         It != Sentinel;
         It = It->Next)
    {
        Assert(It == It->Next->Prev);
        Assert(It == It->Prev->Next);

        if (It->Next != Sentinel)
        {
            Assert(It->Next->VertexOffset >= It->VertexOffset);
        }
    }
    Assert(Sentinel == Sentinel->Prev->Next);
    Assert(Sentinel == Sentinel->Next->Prev);
}

static vulkan_vertex_buffer_block* GetBlockFromPool(vulkan_vertex_buffer* VB)
{
    vulkan_vertex_buffer_block* Sentinel = &VB->BlockPoolSentinel;
    if (Sentinel->Next == Sentinel)
    {
        static constexpr u32 BlockIncrementCount = 8192;

        vulkan_vertex_buffer_block* Blocks = PushArray<vulkan_vertex_buffer_block>(VB->Arena, BlockIncrementCount);
        Assert(Blocks);

        for (u32 i = 0 ; i < BlockIncrementCount; i++)
        {
            Blocks[i].Next = &Blocks[(i + 1) % BlockIncrementCount];
            Blocks[i].Next->Prev = &Blocks[i];
        }
        Sentinel->Next = Blocks;
        Sentinel->Prev = Blocks + (BlockIncrementCount - 1);
        Sentinel->Prev->Next = Sentinel->Next->Prev = Sentinel;
    }
    vulkan_vertex_buffer_block* Result = Sentinel->Next;
    Sentinel->Next = Result->Next;
    Sentinel->Next->Prev = Sentinel;
    Result->Next = Result->Prev = nullptr;
    return(Result);
}

static void InsertVertexBlock(vulkan_vertex_buffer_block* Sentinel, vulkan_vertex_buffer_block* Block)
{
    vulkan_vertex_buffer_block* InsertBefore = Sentinel;
    for (vulkan_vertex_buffer_block* It = Sentinel->Next; It != Sentinel; It = It->Next)
    {
        if (It->VertexOffset > Block->VertexOffset)
        {
            InsertBefore = It;
            break;
        }
    }

    Block->Next = InsertBefore;
    Block->Prev = InsertBefore->Prev;
    Block->Next->Prev = Block->Prev->Next = Block;

    //VerifyListIntegrity(Sentinel);
}

static void RemoveVertexBlock(vulkan_vertex_buffer_block* Sentinel, vulkan_vertex_buffer_block* Block)
{
    Block->Prev->Next = Block->Next;
    Block->Next->Prev = Block->Prev;
    Block->Prev = Block->Next = nullptr;

    //VerifyListIntegrity(Sentinel);
}

bool VB_Create(vulkan_vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device, memory_arena* Arena)
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
                    VB->Arena = Arena;
                    VB->MemorySize = Size;
                    VB->Memory = Memory;
                    VB->Buffer = Buffer;
                    VB->MaxVertexCount = VertexCount;
                    
                    VB->FreeBlockSentinel.Next = VB->FreeBlockSentinel.Prev = &VB->FreeBlockSentinel;
                    VB->UsedBlockSentinel.Next = VB->UsedBlockSentinel.Prev = &VB->UsedBlockSentinel;
                    VB->BlockPoolSentinel.Next = VB->BlockPoolSentinel.Prev = &VB->BlockPoolSentinel;

                    vulkan_vertex_buffer_block* FirstFreeBlock = GetBlockFromPool(VB);
                    FirstFreeBlock->VertexOffset = 0;
                    FirstFreeBlock->VertexCount = VB->MaxVertexCount;
                    InsertVertexBlock(&VB->FreeBlockSentinel, FirstFreeBlock);

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
    return(Result);
}

vulkan_vertex_buffer_block* VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount)
{
    vulkan_vertex_buffer_block* Result = nullptr;
    for (vulkan_vertex_buffer_block* It = VB->FreeBlockSentinel.Next; 
         It != &VB->FreeBlockSentinel;
         It = It->Next)
    {
        if (VertexCount <= It->VertexCount)
        {
            Result = It;
            break;
        }
    }

    if (Result)
    {
        RemoveVertexBlock(&VB->FreeBlockSentinel, Result);
        if (Result->VertexCount != VertexCount)
        {
            vulkan_vertex_buffer_block* NewFreeBlock = GetBlockFromPool(VB);
            NewFreeBlock->VertexCount = Result->VertexCount - VertexCount;
            NewFreeBlock->VertexOffset = Result->VertexOffset + VertexCount;
            InsertVertexBlock(&VB->FreeBlockSentinel, NewFreeBlock);

            Result->VertexCount = VertexCount;
        }
        InsertVertexBlock(&VB->UsedBlockSentinel, Result);
        VB->MemoryUsage += VertexCount * sizeof(terrain_vertex);
    }
    else
    {
        Assert(!"Failed to find sufficient vertex block");
    }
    return(Result);
}

void VB_Free(vulkan_vertex_buffer* VB, vulkan_vertex_buffer_block* Block)
{
    VB->MemoryUsage -= Block->VertexCount * sizeof(terrain_vertex);
    RemoveVertexBlock(&VB->UsedBlockSentinel, Block);
    InsertVertexBlock(&VB->FreeBlockSentinel, Block);

    for (vulkan_vertex_buffer_block* It = VB->FreeBlockSentinel.Next;
         It != &VB->FreeBlockSentinel;
         It = It->Next)
    {
        while (It->VertexOffset + It->VertexCount == It->Next->VertexOffset)
        {
            vulkan_vertex_buffer_block* Next = It->Next;
            It->VertexCount += It->Next->VertexCount;
            It->Next = It->Next->Next;
            It->Next->Prev = It;

            Next->Prev = &VB->BlockPoolSentinel;
            Next->Next = VB->BlockPoolSentinel.Next;
            Next->Prev->Next = Next->Next->Prev = Next;
        }
    }
}

void VB_Defragment(vulkan_vertex_buffer* VB)
{
    Assert(!"Unimplemented code path");
}

u64 VB_GetAllocationMemoryOffset(vulkan_vertex_buffer_block* Block)
{
    u64 Result = Block->VertexOffset * sizeof(terrain_vertex);
    return(Result);
}