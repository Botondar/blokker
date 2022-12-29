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
    if (EffectiveSize <= Arena->Size - Arena->Used)
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