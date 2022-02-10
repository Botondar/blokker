#pragma once


inline u32 ExtractSign(f32 f)
{
    IEEEBinary32 v = { .f = f };
    u32 Result = (v.u & F32_SIGN_MASK) >> F32_SIGN_SHIFT;
    return Result;
}

inline u32 ExtractExponent(f32 f)
{
    IEEEBinary32 v = { .f = f };
    u32 Result = (v.u & F32_EXPONENT_MASK) >> F32_EXPONENT_SHIFT;
    return Result;
}

inline u32 ExtractMantissa(f32 f)
{
    IEEEBinary32 v = { .f = f };
    u32 Result = (v.u & F32_MANTISSA_MASK) >> F32_MANTISSA_SHIFT;
    return Result;
}

inline s32 GetExponent(f32 f)
{
    IEEEBinary32 v = { .f = f };
    s32 Exponent = (v.u & F32_EXPONENT_MASK) >> F32_EXPONENT_SHIFT;
    Exponent -= F32_EXPONENT_BIAS;
    return Exponent;
}

inline bool IsInf(f32 f)
{
    bool Result =
        (ExtractExponent(f) == F32_EXPONENT_SPECIAL) &&
        (ExtractMantissa(f) == 0);
    return Result;
}

inline bool IsNan(f32 f)
{
    bool Result =
        (ExtractExponent(f) == F32_EXPONENT_SPECIAL) &&
        (ExtractMantissa(f) != 0);
    return Result;
}

inline bool IsFinite(f32 f)
{
    bool Result = ExtractExponent(f) != F32_EXPONENT_SPECIAL;
    return Result;
}

inline bool IsSubnormal(f32 f)
{
    bool Result = (ExtractExponent(f) == F32_EXPONENT_SUBNORMAL);
    return Result;
}

inline bool IsNormal(f32 f)
{
    const u32 Exponent = ExtractExponent(f);
    bool Result = (Exponent != F32_EXPONENT_SUBNORMAL) && (Exponent != F32_EXPONENT_SPECIAL);
    return Result;
}

inline f32 AsF32(u32 Value)
{
    IEEEBinary32 B = { .u = Value };
    return B.f;
}

inline u32 AsU32(f32 Value)
{
    IEEEBinary32 B = { .f = Value };
    return B.u;
}

inline FLOAT_CATEGORY FloatCategory(f32 Value)
{
    u32 Exponent = ExtractExponent(Value);
    u32 Mantissa = ExtractMantissa(Value);

    FLOAT_CATEGORY Category = FLOAT_CATEGORY_NORMAL;
    if (Exponent == F32_EXPONENT_SUBNORMAL)
    {
        Category = (Mantissa == 0) ? FLOAT_CATEGORY_ZERO : FLOAT_CATEGORY_SUBNORMAL;
    }
    else if (Exponent == F32_EXPONENT_SPECIAL)
    {
        Category = (Mantissa == 0) ? FLOAT_CATEGORY_INF : FLOAT_CATEGORY_NAN;
    }

    return Category;
}

inline f32 FrExp(f32 Value, s32& Exponent)
{
    f32 Mantissa = 0.0f;

    switch (FloatCategory(Value))
    {
        case FLOAT_CATEGORY_ZERO:
        case FLOAT_CATEGORY_INF:
        case FLOAT_CATEGORY_NAN:
        {
            Exponent = 0;
            Mantissa = Value;
        } break;
        case FLOAT_CATEGORY_NORMAL:
        {
            IEEEBinary32 B = { .f = Value };

            Exponent = ((B.u & F32_EXPONENT_MASK) >> F32_EXPONENT_SHIFT) - F32_EXPONENT_BIAS;

            B.u &= ~F32_EXPONENT_MASK;
            B.u |= F32_EXPONENT_BIAS << F32_EXPONENT_SHIFT;

            Mantissa = B.f;
        } break;
        case FLOAT_CATEGORY_SUBNORMAL:
        {
            /*
            * Multiplying by 2^25 in FP is a lossless operation, so we use that to normalize the input,
            * and subtract 25 from the exponent
            */
            constexpr f32 TwoTo25 = 33554432.0f;
            IEEEBinary32 B = { .f = Value * TwoTo25 };

            Exponent = ((B.u & F32_EXPONENT_MASK) >> F32_EXPONENT_SHIFT) - F32_EXPONENT_BIAS - 25;

            B.u &= F32_EXPONENT_MASK;
            B.u |= F32_EXPONENT_BIAS << F32_EXPONENT_SHIFT;

            Mantissa = B.f;
        } break;
    }

    return Mantissa;
}

inline __m128 FrExp(__m128 Value, __m128i& Exponent)
{
    // NOTE: See FrExp(f32, s32&) for what this algorithm is doing.
    //       Branches are achieved via masking and blending
    
    // Because there's no epi32 blendv and because we're constantly switching between
    // the float and int representations this function is an ugly cast mess

    __m128i ExponentMask = _mm_set1_epi32(F32_EXPONENT_MASK);
    __m128i ExponentBias = _mm_set1_epi32(F32_EXPONENT_BIAS);
    __m128i ExponentShift = _mm_set1_epi32(F32_EXPONENT_SHIFT);
    __m128i ExponentSpecial = _mm_set1_epi32(F32_EXPONENT_SPECIAL << F32_EXPONENT_SHIFT);
    __m128i Zero = _mm_setzero_si128();
    __m128 One = _mm_set1_ps(1.0f);
    __m128 TwoTo25 = _mm_set1_ps(33554432.0f);

    __m128i Exponents = _mm_and_si128(ExponentMask, _mm_castps_si128(Value));

    // Mask for the zero + non-finite case
    __m128i SpecialMask = _mm_or_si128(
        _mm_cmpeq_epi32(_mm_castps_si128(Value), Zero), 
        _mm_cmpeq_epi32(Exponents, ExponentSpecial));

    // Mask for the subnormal case
    __m128i SubnormalMask = _mm_cmpeq_epi32(Exponents, Zero);

    __m128 Mul = _mm_blendv_ps(One, TwoTo25, _mm_castsi128_ps(SubnormalMask));
    __m128i Bias = _mm_castps_si128(
        _mm_blendv_ps(
            _mm_castsi128_ps(ExponentBias),
            _mm_castsi128_ps(_mm_set1_epi32(25 + F32_EXPONENT_BIAS)),
            _mm_castsi128_ps(SubnormalMask)));

    __m128 Result = _mm_mul_ps(Value, Mul);

    Exponent = _mm_sub_epi32(_mm_srli_epi32(_mm_and_si128(ExponentMask, _mm_castps_si128(Result)), F32_EXPONENT_SHIFT), Bias);
    Result = _mm_andnot_ps(_mm_castsi128_ps(ExponentMask), Result);
    Result = _mm_or_ps(Result, _mm_castsi128_ps(_mm_set1_epi32(F32_EXPONENT_ZERO)));

    // Blend zero + non-finite cases
    Exponent = _mm_castps_si128(
        _mm_blendv_ps(
            _mm_castsi128_ps(Exponent),
            _mm_castsi128_ps(Zero),
            _mm_castsi128_ps(SpecialMask)));
    Result = _mm_blendv_ps(Result, Value, _mm_castsi128_ps(SpecialMask));

    return Result;
}