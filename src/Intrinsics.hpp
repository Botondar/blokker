#pragma once

#if defined(_M_X64) || defined(_M_AMD64_) || defined(__amd64__) || defined(__amd64__) || defined(__x86_64__) || defined(__x86_64)
#else
#error Not supported target architecture
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define COMPILER_MSVC 1
#elif defined(__clang__)
#define COMPILER_CLANG 1
#endif

#include <immintrin.h>
#include <intrin.h>

#include <Common.hpp>

//
// Common
//
inline u32 BitScanForward(u32* ScanResult, u32 Value);
inline u32 BitScanReverse(u32* ScanResult, u32 Value);

//
// Atomics
//

// NOTE(boti): Returns *Dest (original value)
inline u32 AtomicExchange(volatile u32* Dest, u32 Value);
// NOTE(boti): Returns *Dest (original value)
inline u32 AtomicCompareExchange(volatile u32* Dest, u32 Value, u32 Comparand);
// NOTE(boti): Returns *Dest + 1
inline u32 AtomicIncrement(volatile u32* Dest);
// NOTE(boti): Returns *Dest (original value)
inline u32 AtomicAdd(volatile u32* Dest, u32 Value);

inline u32 AtomicLoad(volatile const u32* Value);

inline u64 AtomicExchange(volatile u64* Dest, u64 Value);
inline u64 AtomicCompareExchange(volatile u64* Dest, u64 Value, u64 Comparand);
inline u64 AtomicIncrement(volatile u64* Dest);
inline u64 AtomicAdd(volatile u64* Dest, u64 Value);
inline u64 AtomicLoad(volatile const u64* Value);

inline void* AtomicExchangePointer(void* volatile* Dest, void* Value);
inline void* AtomicCompareExchangePointer(void* volatile* Dest, void* Value, void* Comparand);

struct ticket_mutex
{
    volatile u64 Ticket;
    volatile u64 Serving;
};

inline void BeginTicketMutex(ticket_mutex* Mutex);
inline void EndTicketMutex(ticket_mutex* Mutex);

#ifdef COMPILER_MSVC

#define SpinWait _mm_pause()

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

//
// Atomics
//
inline u32 AtomicExchange(volatile u32* Dest, u32 Value)
{
    return _InterlockedExchange((volatile long*)Dest, Value);
}
inline u32 AtomicCompareExchange(volatile u32* Dest, u32 Value, u32 Comparand)
{
    return _InterlockedCompareExchange((volatile long*)Dest, Value, Comparand);
}
inline u32 AtomicIncrement(volatile u32* Dest)
{
    return _InterlockedIncrement((volatile long*)Dest);
}
inline u32 AtomicAdd(volatile u32* Dest, u32 Addend)
{
    return _InterlockedExchangeAdd((volatile long*)Dest, Addend);
}
inline u32 AtomicLoad(volatile const u32* Value)
{
    return *Value;
}

inline u64 AtomicExchange(volatile u64* Dest, u64 Value)
{
    return _InterlockedExchange64((volatile __int64*)Dest, Value);
}
inline u64 AtomicCompareExchange(volatile u64* Dest, u64 Value, u64 Comparand)
{
    return _InterlockedCompareExchange64((volatile __int64*)Dest, Value, Comparand);
}
inline u64 AtomicIncrement(volatile u64* Dest)
{
    return _InterlockedIncrement64((volatile __int64*)Dest);
}
inline u64 AtomicAdd(volatile u64* Dest, u64 Value)
{
    return _InterlockedExchangeAdd64((volatile __int64*)Dest, Value);
}
inline u64 AtomicLoad(volatile const u64* Value)
{
    return *Value;
}

inline void* AtomicExchangePointer(void* volatile* Dest, void* Value)
{
    return _InterlockedExchangePointer(Dest, Value);
}
inline void* AtomicCompareExchangePointer(void* volatile* Dest, void* Value, void* Comparand)
{
    return _InterlockedCompareExchangePointer(Dest, Value, Comparand);
}

inline void BeginTicketMutex(ticket_mutex* Mutex)
{
    u64 Value = AtomicIncrement(&Mutex->Ticket) - 1;
    while (Value != Mutex->Serving)
    {
        SpinWait;
    }
}
inline void EndTicketMutex(ticket_mutex* Mutex)
{
    AtomicIncrement(&Mutex->Serving);
}

#elif COMPILER_CLANG

#define SpinWait _mm_pause()

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
