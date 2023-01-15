#pragma once

#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <cstring>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;

typedef u32 b32;

typedef float  f32;
typedef double f64;

static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

#define CountOf(arr) (sizeof(arr) / sizeof((arr)[0]))

#define KiB(s) (1024*s)
#define MiB(s) (1024*KiB(s))
#define GiB(s) (1024llu*MiB(s))

constexpr u32 INVALID_INDEX_U32 = 0xFFFFFFFFu;
constexpr u64 INVALID_INDEX_U64 = 0xFFFFFFFFFFFFFFFFu;

struct buffer
{
    u64 Size;
    u8* Data;
};

template<typename T>
inline T* PointerByteOffset(T* Base, u64 Offset)
{
    T* Result = (T*)((u8*)Base + Offset);
    return Result;
}

inline u32 SafeU64ToU32(u64 Value)
{
    assert(Value <= 0xFFFFFFFFu);
    return (u32)Value;
}

inline u64 AlignTo(u64 Value, u64 Alignment)
{
    u64 Offset = (Alignment -  (Value % Alignment)) % Alignment;
    u64 Result = Value + Offset;
    return Result;
}

inline u64 AlignToPow2(u64 Value, u64 Alignment)
{
    u64 Result = (Value + (Alignment - 1)) & (~(Alignment - 1));
    return Result;
}