#pragma once

#include <Common.hpp>
#include <Intrinsics.hpp>

enum FLOAT_CATEGORY
{
    FLOAT_CATEGORY_ZERO = 0,
    FLOAT_CATEGORY_NORMAL,
    FLOAT_CATEGORY_SUBNORMAL,
    FLOAT_CATEGORY_INF,
    FLOAT_CATEGORY_NAN,
};

constexpr u32 F32_SIGN_MASK = 0x80000000u;
constexpr u32 F32_SIGN_SHIFT = 31u;
constexpr u32 F32_EXPONENT_MASK = 0x7F800000u;
constexpr u32 F32_EXPONENT_SHIFT = 23u;
constexpr u32 F32_MANTISSA_MASK = 0x007FFFFFu;
constexpr u32 F32_MANTISSA_SHIFT = 0u;
constexpr u32 F32_EXPONENT_ZERO = 0x3F800000;

constexpr s32 F32_EXPONENT_BIAS = 0x7F;
constexpr s32 F32_EXPONENT_MIN = -126;
constexpr s32 F32_EXPONENT_MAX = 127;
constexpr u32 F32_EXPONENT_SPECIAL = 0xFF;
constexpr u32 F32_EXPONENT_SUBNORMAL = 0x00;

constexpr f32 F32_MAX_NORMAL = 3.40282347e+38f;
constexpr f32 F32_MIN_NORMAL = 1.17549435e-38f;
constexpr f32 F32_MIN_SUBNORMAL = 1.40129846e-45f;
constexpr f32 F32_EPSILON = 1.19209290e-7f;

// Positive infinity
constexpr u32 U32_POS_INF = 0x7F800000;
const f32 F32_POS_INF = reinterpret_cast<const f32&>(U32_POS_INF);

// Negative infinity
constexpr u32 U32_NEG_INF = 0xFF800000u;
const f32 F32_NEG_INF = reinterpret_cast<const f32&>(U32_NEG_INF);

// Quiet NaN (one of several)
constexpr u32 U32_QNAN = 0x7FFFFFFFu;
const f32 F32_QNAN = reinterpret_cast<const f32&>(U32_QNAN);

// Signaling NaN (one of several)
constexpr u32 U32_SNAN = 0x7FBFFFFFu;
const f32 F32_SNAN = reinterpret_cast<const f32&>(U32_SNAN);

union IEEEBinary32
{
    f32 f;
    u32 u;
};

inline u32 ExtractSign(f32 f);
// Extract the biased exponent bits exactly
inline u32 ExtractExponent(f32 f);
inline u32 ExtractMantissa(f32 f);

// Extract the value represented (i.e. unbiased) by the exponent
inline s32 GetExponent(f32 f);

inline bool IsInf(f32 f);
inline bool IsNan(f32 f);
inline bool IsSubnormal(f32 f);

inline f32 AsF32(u32 Value);
inline u32 AsU32(f32 Value);

inline FLOAT_CATEGORY FloatCategory(f32 Value);

/*
 * Break f32 into its exponent (e) and mantissa (m) where
 * Value = m * 2^e
 * m is in the range [1,2) when Value is finite and non-zero
 * Otherwise m = Value, Exponent = 0
 */
inline f32 FrExp(f32 Value, s32& Exponent);

inline __m128 FrExp(__m128 Value, __m128i& Exponent);

#include "Float.inl"