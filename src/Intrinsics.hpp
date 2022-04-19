#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define COMPILER_MSVC 1
#elif defined(__clang__)
#define COMPILER_CLANG 1
#endif

#include <immintrin.h>
#include <intrin.h>

#include <Common.hpp>

inline u32 BitScanForward(u32* ScanResult, u32 Value);
inline u32 BitScanReverse(u32* ScanResult, u32 Value);

#ifdef COMPILER_MSVC

inline u32 BitScanForward(u32* ScanResult, u32 Value)
{
    u32 Result = _BitScanForward((unsigned long*)ScanResult, Value);
    return Result;
}

inline u32 BitScanReverse(u32* ScanResult, u32 Value)
{
    u32 Result = _BitScanReverse((unsigned long*)ScanResult, Value);
    return Result;
}

inline u32 BitScanForward(u32* ScanResult, u64 Value)
{
    u32 Result = _BitScanForward64((unsigned long*)ScanResult, Value);
    return Result;
}

inline u32 BitScanReverse(u32* ScanResult, u64 Value)
{
    u32 Result = _BitScanReverse64((unsigned long*)ScanResult, Value);
    return Result;
}


#elif COMPILER_CLANG

inline u32 BitScanForward(u32* ScanResult, u32 Value)
{
    u32 Result = 0;
    if (Value != 0)
    {
        *ScanResult = __bultin_ctz(Value);
        Result = 1;
    }
    return Result;
}

inline u32 BitScanReverse(u32* ScanResult, u32 Value)
{
    u32 Result = 0;
    if (Value != 0)
    {
        *ScanResult = __bultin_clz(Value);
        Result = 1;
    }
    return Result;
}


#else
#error Not supported compiler
#endif
