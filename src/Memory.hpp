#pragma once

#include <Common.hpp>

struct memory_arena
{
    u64 Used;
    u64 Size;
    u8* Base;
};

inline void* PushSize(memory_arena* Arena, u64 Size, u64 Alignment = 0);
template<typename T>
inline T* PushStruct(memory_arena* Arena);
template<typename T>
inline T* PushArray(memory_arena* Arena, u64 Count);

struct bump_allocator
{
    static constexpr u64 MemorySize = 64 * 1024 * 1024;
    u8* At;
    u8* Memory;

    u64 LastAllocationSize;
    u8* LastAllocation;
};

bool Bump_Initialize(bump_allocator* Allocator);
void Bump_Shutdown(bump_allocator* Allocator);

void* Bump_Allocate(bump_allocator* Allocator, u64 Size);
void* Bump_Append(bump_allocator* Allocator, u64 Size);
void Bump_Free(bump_allocator* Allocator, void* Allocation);

void Bump_Reset(bump_allocator* Allocator);

template<typename T>
class CBumpArray
{
public:
    CBumpArray(bump_allocator* Allocator);
    CBumpArray(const CBumpArray& Other) = delete;
    CBumpArray(CBumpArray&& Other);
    ~CBumpArray();

    CBumpArray& operator=(const CBumpArray& Other) = delete;
    CBumpArray& operator=(CBumpArray&& Other) = delete;

    T& operator[](u64 Index) { return Data[Index]; }
    const T& operator[](u64 Index) const { return Data[Index]; }

    void PushBack(T Value);

    T* GetData() const { return Data; }
    u64 GetCount() const { return Count; } 
    u64 GetMaxCount() const { return MaxCount; }
private:
    static constexpr u64 SizeIncrement = 1024;

    u64 MaxCount;
    u64 Count;
    T* Data;

    bump_allocator* Allocator;
};

//
// CBumpArray implementation
//

template<typename T>
CBumpArray<T>::CBumpArray(bump_allocator* Allocator) :
    Allocator(Allocator),
    MaxCount(0),
    Count(0),
    Data(nullptr)
{ }

template<typename T>
CBumpArray<T>::CBumpArray(CBumpArray<T>&& Other) :
    Allocator(Other.Allocator),
    MaxCount(Other.MaxCount),
    Count(Other.Count),
    Data(Other.Data)
{ }

template<typename T>
CBumpArray<T>::~CBumpArray()
{
    Bump_Free(Allocator, (void*)Data);

    MaxCount = 0;
    Count = 0;
    Data = nullptr;
}

template<typename T>
void CBumpArray<T>::PushBack(T Value)
{
    while (Count + 1 > MaxCount)
    {
        Data = (T*)Bump_Append(Allocator, Data, SizeIncrement);
        MaxCount += SizeIncrement;
    }

    Data[Count++] = Value;
}