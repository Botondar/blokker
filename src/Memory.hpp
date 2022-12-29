#pragma once

#include <Common.hpp>

struct memory_arena
{
    u64 Used;
    u64 Size;
    u8* Base;
};

inline memory_arena InitializeArena(u64 Size, void* Base);

inline void* PushSize(memory_arena* Arena, u64 Size, u64 Alignment = 0);
template<typename T>
inline T* PushStruct(memory_arena* Arena);
template<typename T>
inline T* PushArray(memory_arena* Arena, u64 Count);