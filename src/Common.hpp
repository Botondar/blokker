#pragma once

#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <immintrin.h>

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

constexpr u32 INVALID_INDEX_U32 = 0xFFFFFFFFu;
constexpr u64 INVALID_INDEX_U64 = 0xFFFFFFFFFFFFFFFFu;

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

inline u32 BitScanForward(u32* ScanResult, u32 Value)
{
#if defined(_MSC_VER) && !defined(__clang__)
    u32 Result = _BitScanForward((unsigned long*)ScanResult, Value);
#elif defined(__clang__)
    u32 Result = 0;
    if (Value != 0)
    {
        *ScanResult = __builtin_ctz(Value);
        Result = 1;
    }
#endif
    return Result;
}


inline u32 BitScanReverse(u32* ScanResult, u32 Value)
{
#if defined(_MSC_VER) && !defined(__clang__)
    u32 Result = _BitScanReverse((unsigned long*)ScanResult, Value);
#elif defined(__clang__)
    u32 Result = 0;
    if (Value != 0)
    {
        *ScanResult = __builtin_clz(Value);
        Result = 1;
    }
#endif
    return Result;
}

class CBuffer
{
public:
    u64 Size;
    u8* Data;
public:
    CBuffer();
    ~CBuffer();
    
    CBuffer(const CBuffer& Other) = delete;
    CBuffer(CBuffer&& Other);
    
    CBuffer& operator=(const CBuffer& Other) = delete;
    CBuffer& operator=(CBuffer&& Other) = delete;
};
