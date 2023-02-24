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

#define Assert(...) assert(__VA_ARGS__)
#define FatalError(msg) Assert(!msg)
#if DEVELOPER
#define UnhandledError(msg) Assert(!msg)
#else
#define UnhandledError(...) static_assert(false, "Unhandled error in release build!");
#endif

struct buffer
{
    u64 Size;
    u8* Data;
};

template<typename T> inline T* OffsetPtr(T* Base, u64 Offset);

inline u64 AlignTo(u64 Value, u64 Alignment);
inline u64 AlignToPow2(u64 Value, u64 Alignment);
inline u32 SafeU64ToU32(u64 Value);
inline u64 CopyZString(u64 MaxLength, char* Dst, const char* Src);

template<typename T>
inline T* OffsetPtr(T* Base, u64 Offset)
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

inline u64 CopyZString(u64 MaxLength, char* Dst, const char* Src)
{
    u64 Result = 0;
    if (Src && MaxLength > 0)
    {
        while (--MaxLength)
        {
            if (*Src == 0) break;
            Dst[Result++] = *Src++;
        }
        Dst[Result] = 0;
    }
    return(Result);
}

// NOTE(boti): 
//     Work function is a type-erased function pointer that can hold 60 bytes of arguments directly.
//     There are no allocations or real C++ ownership/copy/move/destruction etc. semantics here,
//     so the caller _must_ manage the lifetime of all arguments _not_ passed by value.
template<typename T> struct function {};

template<typename ret_type, typename... args>
struct function<ret_type(args...)>
{
    static constexpr u64 MaxPayloadSize = 60;
    struct impl_base
    {
        virtual ret_type Invoke(args... Args) = 0;
    };

    template<typename function_type>
    struct impl : public impl_base
    {
        function_type Function;

        impl(function_type Function) : Function(Function) { }

        virtual ret_type Invoke(args... Args) { return Function(Args...); }
    };

    function() : Data{}, IsValid{false}
    {
        static_assert(sizeof(function<ret_type(args...)>) == 64);
    }
    template<typename function_type> function(function_type Function)
    {
        static_assert(sizeof(impl<function_type>) <= MaxPayloadSize, "Work function exceeds MaxPayloadSize");
        impl_base* Impl = new(Data) impl<function_type>(Function);
        if (Impl)
        {
            IsValid = true;
        }
    }

    function(const function& Other)
    {
        memcpy(Data, Other.Data, sizeof(Data));
        IsValid = Other.IsValid;
    }

    function& operator=(const function& Other)
    {
        memcpy(Data, Other.Data, sizeof(Data));
        IsValid = Other.IsValid;
        return(*this);
    }

    template<typename function_type> function& operator=(function_type Function)
    {
        static_assert(sizeof(impl<function_type>) <= MaxPayloadSize, "Work function exceeds MaxPayloadSize");
        impl_base* Impl = new(Data) impl<function_type>(Function);
        if (Impl)
        {
            Assert((void*)Impl == (void*)Data);
            IsValid = true;
        }
        return (*this);
    }
    function& operator=(nullptr_t)
    {
        IsValid = false;
        return (*this);
    }

    ret_type Invoke(args... Args)
    {
        if (IsValid)
        {
            return reinterpret_cast<impl_base*>(Data)->Invoke(Args...);
        }
    }

public:
    // NOTE(boti): The data array is put first here so that the impl object is properly aligned
    u8 Data[MaxPayloadSize];
    b32 IsValid;
};
