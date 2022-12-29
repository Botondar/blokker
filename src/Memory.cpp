#include "Memory.hpp"

#include <Platform.hpp>
#include <Intrinsics.hpp>

//
// Memory arena
//

inline memory_arena InitializeArena(u64 Size, void* Base)
{
    memory_arena Arena = {};
    Arena.Size = Size;
    Arena.Base = (u8*)Base;
    return(Arena);
}

inline void* PushSize(memory_arena* Arena, u64 Size, u64 Alignment /*= 0*/)
{
    void* Result = nullptr;

    u64 EffectiveSize = Size;
    if (Alignment)
    {
        EffectiveSize = AlignToPow2(Size, Alignment);
    }

    u64 Offset = EffectiveSize - Size;
    if (EffectiveSize <= Arena->Size)
    {
        Result = (void*)(Arena->Base + Arena->Used + Offset);
        Arena->Used += EffectiveSize;
    }
    else
    {
        // TODO
    }

    return(Result);
}

template<typename T>
inline T* PushStruct(memory_arena* Arena)
{
    T* Result = (T*)PushSize(Arena, sizeof(T), alignof(T));
    return(Result);
}
template<typename T>
inline T* PushArray(memory_arena* Arena, u64 Count)
{
    T* Result = (T*)PushSize(Arena, sizeof(T) * Count, alignof(T));
    return Result;
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