#pragma once

#include <Common.hpp>

struct memory_arena
{
    u64 Used;
    u64 Size;
    u8* Base;
};

struct memory_arena_checkpoint
{
    memory_arena* Arena;
    u64 Used;
};

inline memory_arena InitializeArena(u64 Size, void* Base);

inline void* PushSize(memory_arena* Arena, u64 Size, u64 Alignment = 0);
template<typename T> inline T* PushStruct(memory_arena* Arena);
template<typename T> inline T* PushArray(memory_arena* Arena, u64 Count);

inline void ResetArena(memory_arena* Arena);
inline memory_arena_checkpoint ArenaCheckpoint(memory_arena* Arena);
inline bool RestoreArena(memory_arena* Arena, memory_arena_checkpoint Checkpoint);

//
// Implementation
//
inline memory_arena InitializeArena(u64 Size, void* Base)
{
    memory_arena Arena = {};
    Arena.Size = Size;
    Arena.Base = (u8*)Base;
    return(Arena);
}

inline void ResetArena(memory_arena* Arena)
{
    Arena->Used = 0;
}

inline memory_arena_checkpoint ArenaCheckpoint(memory_arena* Arena)
{
    memory_arena_checkpoint Checkpoint = 
    {
        .Arena = Arena,
        .Used = Arena->Used,
    };
    return(Checkpoint);
}

inline bool RestoreArena(memory_arena* Arena, memory_arena_checkpoint Checkpoint)
{
    bool Result = false;
    if (Arena == Checkpoint.Arena)
    {
        Arena->Used = Checkpoint.Used;
        Result = true;
    }
    return(Result);
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