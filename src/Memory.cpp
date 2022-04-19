#include "Memory.hpp"

#include <Platform.hpp>
#include <Intrinsics.hpp>

//
// Large memory block allocator
//

bool LMBA_Initialize(large_memory_block_allocator* Allocator)
{
    bool Result = false;

    assert(Allocator);
    *Allocator = {};

    Allocator->Memory = Platform_VirtualAlloc(nullptr, Allocator->MemorySize);
    if (Allocator->Memory)
    {
        Result = true;
    }

    return Result;
}

void LMBA_Shutdown(large_memory_block_allocator* Allocator)
{
    Platform_VirtualFree(Allocator->Memory, Allocator->MemorySize);
    Allocator->Memory = nullptr;
}

large_memory_block* LMBA_Allocate(large_memory_block_allocator* Allocator, large_memory_block* Prev /*= nullptr */)
{
    large_memory_block* Result = nullptr;

    u32 Index = INVALID_INDEX_U32;
    u32 IndexInArray = INVALID_INDEX_U32;
    u32 IndexInElement = INVALID_INDEX_U32;
    for (u32 i = 0; i < Allocator->FreeBlocksArrayCount; i++)
    {
        if (BitScanForward(&IndexInElement, ~Allocator->FreeBlocks[i]))
        {
            Index = IndexInElement + 64*i;
            IndexInArray = i;
            break;
        }
    }

    if (Index != INVALID_INDEX_U32)
    {
        Result = Allocator->Blocks + Index;
        Allocator->FreeBlocks[IndexInArray] |= (1ull << IndexInElement);

        Result->Memory = (void*)((u8*)Allocator->Memory + Index * large_memory_block::BlockSize);

        if (Prev)
        {
            large_memory_block* Last = Prev;
            while (Last)
            {
                Prev = Last;
                Last = Last->NextBlock;
            }
            Prev->NextBlock = Result;
        }
    }

    return Result;
}

void LMBA_Free(large_memory_block_allocator* Allocator, large_memory_block* Block)
{
    auto IndexFromMemoryBlock = [](large_memory_block_allocator* Allocator, large_memory_block* Block, u32* IndexInArray, u32* IndexInElement) -> u32
    {
        u64 Offset = (u64)Block->Memory - (u64)Allocator->Memory;
        u64 Result64 = Offset / Block->BlockSize;

        assert(Result64 < 0xFFFFFFFFu);

        u32 Result = (u32)Result64;

        if (IndexInArray)
        {
            *IndexInArray = Result / 64;
        }
        if (IndexInElement)
        {
            *IndexInElement = Result % 64;
        }

        return Result;
    };

    while (Block)
    {
        u32 IndexInArray, IndexInElement;
        u32 Index = IndexFromMemoryBlock(Allocator, Block, &IndexInArray, &IndexInElement);

        Allocator->FreeBlocks[IndexInArray] &= ~(1ull << IndexInElement);
        Block->Memory = nullptr;

        large_memory_block* Temp = Block;
        Block = Block->NextBlock;
        Temp->NextBlock = nullptr;
    }
}

//
// Bump allocator
//

bool Bump_Initialize(bump_allocator* Allocator)
{
    bool Result = false;

    assert(Allocator);
    *Allocator = {};

    Allocator->Memory = (u8*)Platform_VirtualAlloc(nullptr, Allocator->MemorySize);
    if (Allocator->Memory)
    {
        Allocator->At = Allocator->Memory;
        Allocator->LastAllocationSize = 0;
        Allocator->LastAllocation = nullptr;
        Result = true;
    }

    return Result;
}
void Bump_Shutdown(bump_allocator* Allocator)
{
    Platform_VirtualFree(Allocator->Memory, Allocator->MemorySize);
    *Allocator = {};
}

void* Bump_Allocate(bump_allocator* Allocator, u64 Size)
{
    void* Result = nullptr;

    u64 RemainingSize = Allocator->MemorySize - ((u64)Allocator->At - (u64)Allocator->Memory);
    if (Size <= RemainingSize)
    {
        Result = (void*)Allocator->At;
        Allocator->At += Size;

        Allocator->LastAllocationSize = Size;
        Allocator->LastAllocation = (u8*)Result;
    }

    return Result;
}

void* Bump_Append(bump_allocator* Allocator, void* Allocation, u64 Size)
{
    void* Result = nullptr;

    if (Allocation)
    {
        assert((u8*)Allocation == Allocator->LastAllocation);

        u64 RemainingSize = Allocator->MemorySize - ((u64)Allocator->At - (u64)Allocator->Memory);
        if (Size <= RemainingSize)
        {
            Allocator->At += Size;
            Allocator->LastAllocationSize += Size;

            Result = Allocation;
        }
        else
        {
            assert(!"Unimplemented code path");
        }
    }
    else
    {
        Result = Bump_Allocate(Allocator, Size);
    }

    return Result;
}

void Bump_Free(bump_allocator* Allocator, void* Allocation)
{
    if (Allocation && ((u8*)Allocation == Allocator->LastAllocation))
    {
        Allocator->At = Allocator->LastAllocation;
        Allocator->LastAllocationSize = 0;
        Allocator->LastAllocation = nullptr;
    }
}

void Bump_Reset(bump_allocator* Allocator)
{
    Allocator->At = Allocator->Memory;
    Allocator->LastAllocationSize = 0;
    Allocator->LastAllocation = nullptr;
}