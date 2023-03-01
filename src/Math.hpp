#pragma once

#include <Common.hpp>
#include <Intrinsics.hpp>
#include <cmath>

//
// Constants
//
constexpr f32 PI = 3.14159265358979323846f;

//
// Types
//
struct vec2;
struct vec2i;
struct vec3;
struct vec3i;
struct vec4;
struct mat2;
struct mat3;
struct mat4;

//
// Type definitions
//

struct vec2 
{
    f32 x, y;

    inline f32& operator[](int idx);
    inline const f32& operator[](int idx) const;

    inline explicit operator vec2i() const;
    inline explicit operator vec3() const;
};

struct vec2i 
{
    s32 x, y;

    inline s32& operator[](int idx);
    inline const s32& operator[](int idx) const;

    inline explicit operator vec2() const;
    inline explicit operator vec3i() const;

    inline bool operator==(const vec2i& Other) const = default;
};

struct vec3 
{
    f32 x, y, z;

    inline f32& operator[](int idx);
    inline const f32& operator[](int idx) const;

    inline explicit operator vec3i() const;
    inline explicit operator vec2() const;
};

struct vec3i
{
    s32 x, y, z;

    inline bool operator==(const vec3i& Other) const = default;

    inline s32& operator[](int idx);
    inline const s32& operator[](int idx) const;

    inline explicit operator vec3() const;
    inline explicit operator vec2i() const;
};

struct vec4 
{
    f32 x, y, z, w;

    inline f32& operator[](int idx);
    inline const f32& operator[](int idx) const;
};

struct mat2
{
    union
    {
        f32 m[2][2];
        f32 mm[4];
    };

    inline f32& operator()(int i, int j);
    inline const f32& operator()(int i, int j) const;
};

struct mat3
{
    union
    {
        f32 m[3][3];
        f32 mm[9];
    };

    inline f32& operator()(int i, int j);
    inline const f32& operator()(int i, int j) const;
};

struct mat4
{
    union
    {
        f32 m[4][4];
        f32 mm[16];
    };

    inline f32& operator()(int i, int j);
    inline const f32& operator()(int i, int j) const;
};

// 
// Common functions
// 

inline s32 FloorDiv(s32 a, s32 b)
{
    assert(b > 0); // TODO(boti)?

    s32 Result = a/b;
    if ((a < 0) && ((a % b) != 0))
    {
        Result -= 1;
    }
    return Result;
}

inline s32 CeilDiv(s32 a, s32 b)
{
    assert(b > 0); // TODO(boti)?

    s32 Result = a/b;
    if ((a > 0) && ((a % b) != 0))
    {
        Result += 1;
    }
    return Result;
}

// Positive modulo
inline s32 Modulo(s32 a, s32 b)
{
    assert(b > 0);
    s32 Result = a % b;
    if (Result < 0)
    {
        Result += b;
    }
    return Result;
}

constexpr f32 ToRadians(f32 Degrees) { return PI * Degrees / 180.0f; }
constexpr f32 ToDegrees(f32 Radians) { return 180.0f * Radians / PI; }

inline s32 Abs(s32 x) { return abs(x); }

template<typename T>
inline T Min(T a, T b) { return (a < b) ? a : b; }

template<typename T>
inline T Max(T a, T b) { return (a < b) ? b : a; }

inline f32 Signum(f32 x) 
{  
    if (x < 0.0f) return -1.0f;
    else if (x > 0.0f) return 1.0f;
    return 0.0f;
}

inline f32 Abs(f32 x) { return fabsf(x); }
inline f32 Floor(f32 x) { return floorf(x); }
inline f32 Ceil (f32 x) { return ceilf(x); }
inline f32 Round(f32 x) { return roundf(x); }
inline f32 Trunc(f32 x) { return truncf(x); }

inline f32 Max(f32 a, f32 b) { return fmaxf(a, b); }
inline f32 Min(f32 a, f32 b) { return fminf(a, b); }
inline f32 Clamp(f32 v, f32 e0, f32 e1) { return Min(e1, Max(e0, v)); }

inline f32 Sqrt(f32 x) { return sqrtf(x); }
inline f32 Sin(f32 x) { return sinf(x); }
inline f32 Cos(f32 x) { return cosf(x); }
inline f32 Tan(f32 x) { return tanf(x); }
inline f32 ATan2(f32 y, f32 x) { return atan2f(y, x); }

inline f32 Modulo(f32 x, f32 y) { return fmodf(x, y); }

inline f32 Exp(f32 x) { return expf(x); }
inline f32 Pow(f32 x, f32 y) { return powf(x, y); }


template<typename T>
inline T Lerp(T a, T b, f32 t) 
{ 
    return a*(1.0f - t) + b*t; 
}

template<typename T>
inline T Blerp(const T& v00, const T& v10, const T& v01, const T& v11, vec2 uv)
{
    T x0 = Lerp(v00, v10, uv.x);
    T x1 = Lerp(v01, v11, uv.x);

    T Result = Lerp(x0, x1, uv.y);
    return Result;
}

template<typename T>
inline T Trilerp(
    const T& c000, const T& c100, const T& c010, const T& c110,
    const T& c001, const T& c101, const T& c011, const T& c111,
    vec3 uvw)
{
    T x0 = Blerp(c000, c100, c010, c110, { uvw.x, uvw.y });
    T x1 = Blerp(c001, c101, c011, c111, { uvw.x, uvw.y });

    T Result = Lerp(x0, x1, uvw.z);

    return Result;
}

inline constexpr f32 Fade3(f32 t) { return (3.0f - 2.0f*t)*t*t; }
inline constexpr f32 Fade5(f32 t) { return ((6.0f*t - 15.0f) * t + 10.0f)*t*t*t; }

inline __m128 Lerp(__m128 a, __m128 b, __m128 t)
{
    __m128 Result = 
        _mm_fmadd_ps(b, t, _mm_mul_ps(a, _mm_sub_ps(_mm_set1_ps(1.0f), t)));
    return Result;
}

// 
// Vector and matrix functions
// 
inline vec2 operator-(const vec2& v);
inline vec2& operator*=(vec2& v, f32 s);
inline vec2& operator+=(vec2& v, const vec2& Other);
inline vec2& operator-=(vec2& v, const vec2& Other);
inline vec2& operator*=(vec2& v, const vec2& Other);
inline vec2& operator/=(vec2& v, const vec2& Other);

inline vec2 operator*(const vec2& v, f32 s);
inline vec2 operator*(f32 s, const vec2& v);

inline vec2 operator+(const vec2& a, const vec2& b);
inline vec2 operator-(const vec2& a, const vec2& b);
inline vec2 operator*(const vec2& a, const vec2& b);
inline vec2 operator/(const vec2& a, const vec2& b);

inline vec2 Floor(const vec2& v);

inline f32 Dot(const vec2& a, const vec2& b);
inline f32 Length(const vec2& v);
inline vec2 Normalize(const vec2& v);
inline vec2 NOZ(const vec2& v);

inline vec2i operator-(const vec2i& v);
inline vec2i& operator*=(vec2i& v, s32 s);
inline vec2i& operator+=(vec2i& v, const vec2i& Other);
inline vec2i& operator-=(vec2i& v, const vec2i& Other);
inline vec2i& operator*=(vec2i& v, const vec2i& Other);
inline vec2i& operator/=(vec2i& v, const vec2i& Other);

inline vec2i operator*(const vec2i& v, s32 s);
inline vec2i operator*(s32 s, const vec2i& v);

inline vec2i operator+(const vec2i& a, const vec2i& b);
inline vec2i operator-(const vec2i& a, const vec2i& b);
inline vec2i operator*(const vec2i& a, const vec2i& b);
inline vec2i operator/(const vec2i& a, const vec2i& b);

inline s32 ManhattanDistance(const vec2i& a, const vec2i& b);
inline s32 ChebyshevDistance(const vec2i& a, const vec2i& b);

inline vec3 operator-(const vec3& v);
inline vec3& operator*=(vec3& v, f32 s);
inline vec3& operator+=(vec3& v, const vec3& Other);
inline vec3& operator-=(vec3& v, const vec3& Other);
inline vec3& operator*=(vec3& v, const vec3& Other);
inline vec3& operator/=(vec3& v, const vec3& Other);

inline vec3 operator*(const vec3& v, f32 s);
inline vec3 operator*(f32 s, const vec3& v);
inline vec3 operator/(const vec3& v, f32 s);

inline vec3 operator+(const vec3& a, const vec3& b);
inline vec3 operator-(const vec3& a, const vec3& b);
inline vec3 operator*(const vec3& a, const vec3& b);
inline vec3 operator/(const vec3& a, const vec3& b);

inline vec3 Floor(const vec3& v);
inline vec3 Ceil(const vec3& v);

inline f32 Dot(const vec3& a, const vec3& b);
inline f32 Length(const vec3& v);
inline vec3 Normalize(const vec3& v);
inline vec3 NOZ(const vec3& v);

// NOTE(boti): b should be normalized
inline vec3 Projection(const vec3& a, const vec3& b);
// NOTE(boti): b should be normalized
inline vec3 Rejection(const vec3& a, const vec3& b);

inline vec3 Cross(const vec3& a, const vec3& b);

inline vec3i operator-(const vec3i& v);

inline vec3i operator*(const vec3i& v, s32 s);
inline vec3i operator*(s32 s, const vec3i& v);
inline vec3i operator/(const vec3i& v, s32 s);

inline vec3i operator+(const vec3i& a, const vec3i& b);
inline vec3i operator-(const vec3i& a, const vec3i& b);
inline vec3i operator*(const vec3i& a, const vec3i& b);
inline vec3i operator/(const vec3i& a, const vec3i& b);

inline vec4 operator-(const vec4& v);

inline vec4 operator+(const vec4& a, const vec4& b);
inline vec4 operator-(const vec4& a, const vec4& b);
inline vec4 operator*(const vec4& a, const vec4& b);
inline vec4 operator/(const vec4& a, const vec4& b);

inline vec4& operator+=(vec4& v, const vec4& Other);
inline vec4& operator-=(vec4& v, const vec4& Other);
inline vec4& operator*=(vec4& v, const vec4& Other);
inline vec4& operator/=(vec4& v, const vec4& Other);

inline vec4 operator*(const vec4& v, f32 s);
inline vec4 operator*(f32 s, const vec4& v);
inline vec4& operator*=(vec4& v, f32 s);

inline f32 Dot(const vec4& a, const vec4& b);
inline f32 Length(const vec4& v);
inline vec4 Normalize(const vec4& v);

inline mat2 Mat2(f32 m00, f32 m01,
                 f32 m10, f32 m11);

inline vec2 operator*(const mat2& M, const vec2& v);

inline mat2 Identity2();

inline mat3 Mat3(f32 m00, f32 m01, f32 m02,
                 f32 m10, f32 m11, f32 m12,
                 f32 m20, f32 m21, f32 m22);

inline vec3 operator*(const mat3& M, const vec3& v);

inline mat3 Identity3();

inline mat4 Mat4(f32 m00, f32 m01, f32 m02, f32 m03,
                 f32 m10, f32 m11, f32 m12, f32 m13,
                 f32 m20, f32 m21, f32 m22, f32 m23,
                 f32 m30, f32 m31, f32 m32, f32 m33);

inline vec4 operator*(const mat4& M, const vec4& v);
inline vec4 operator*(const vec4& v, const mat4& M);
inline mat4 operator*(const mat4& A, const mat4& B);

inline mat4 Identity4();

inline mat4 PerspectiveMat4(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far);
inline mat4 PerspectiveReverse(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far);

inline vec3 TransformDirection(const mat4& M, const vec3& v);
inline vec3 TransformPoint(const mat4& M, const vec3& p);

//
// Implementation
//

inline f32& vec2::operator[](int idx)
{
    return (&x)[idx];
}
inline const f32& vec2::operator[](int idx) const
{
    return (&x)[idx];
}

inline vec2::operator vec2i() const
{
    vec2i Result = { (s32)x, (s32)y };
    return Result;
}

inline vec2::operator vec3() const
{
    vec3 Result = { x, y, 0.0f };
    return Result;
}

inline vec2 operator-(const vec2& v)
{
    vec2 Result = { -v.x, -v.y };
    return Result;
}
inline vec2& operator*=(vec2& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    return v;
}
inline vec2& operator+=(vec2& v, const vec2& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    return v;
}
inline vec2& operator-=(vec2& v, const vec2& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    return v;
}
inline vec2& operator*=(vec2& v, const vec2& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    return v;
}
inline vec2& operator/=(vec2& v, const vec2& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    return v;
}

inline vec2 operator*(const vec2& v, f32 s)
{
    vec2 Result = { v.x*s, v.y*s };
    return Result;
}
inline vec2 operator*(f32 s, const vec2& v)
{
    vec2 Result = { s*v.x, s*v.y };
    return Result;
}

inline vec2 operator+(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x + b.x, a.y + b.y };
    return Result;
}
inline vec2 operator-(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x - b.x, a.y - b.y };
    return Result;
}
inline vec2 operator*(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x * b.x, a.y * b.y };
    return Result;
}
inline vec2 operator/(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x / b.x, a.y / b.y };
    return Result;
}

inline vec2 Floor(const vec2& v)
{
    vec2 Result = { Floor(v.x), Floor(v.y) };
    return Result;
}

inline f32 Dot(const vec2& a, const vec2& b)
{
    f32 Result = a.x*b.x + a.y*b.y;
    return Result;
}
inline f32 Length(const vec2& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
inline vec2 Normalize(const vec2& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec2 Result = v * InvLength;
    return Result;
}

inline vec2 NOZ(const vec2& v)
{
    vec2 Result = { 0.0f, 0.0f };
    f32 Len = Length(v);
    if (Len > 1e-5f)
    {
        f32 InvLength = 1.0f / Len;
        Result = v * InvLength;
    }
    return Result;
}

inline s32& vec2i::operator[](int idx)
{
    return (&x)[idx];
}
inline const s32& vec2i::operator[](int idx) const
{
    return (&x)[idx];
}

inline vec2i::operator vec2() const
{
    vec2 Result = { (f32)x, (f32)y };
    return Result;
}

inline vec2i::operator vec3i() const
{
    vec3i Result = { x, y, 0 };
    return Result;
}

inline vec2i operator-(const vec2i& v)
{
    vec2i Result = { -v.x, -v.y };
    return Result;
}
inline vec2i& operator*=(vec2i& v, s32 s)
{
    v.x *= s;
    v.y *= s;
    return v;
}
inline vec2i& operator+=(vec2i& v, const vec2i& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    return v;
}
inline vec2i& operator-=(vec2i& v, const vec2i& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    return v;
}
inline vec2i& operator*=(vec2i& v, const vec2i& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    return v;
}
inline vec2i& operator/=(vec2i& v, const vec2i& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    return v;
}

inline vec2i operator*(const vec2i& v, s32 s)
{
    vec2i Result = { v.x * s, v.y * s };
    return Result;
}
inline vec2i operator*(s32 s, const vec2i& v)
{
    vec2i Result = { s * v.x, s * v.y };
    return Result;
}

inline vec2i operator+(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x + b.x, a.y + b.y };
    return Result;
}
inline vec2i operator-(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x - b.x, a.y - b.y };
    return Result;
}
inline vec2i operator*(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x * b.x, a.y * b.y };
    return Result;
}
inline vec2i operator/(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x / b.x, a.y / b.y };
    return Result;
}

inline s32 ManhattanDistance(const vec2i& a, const vec2i& b)
{
    s32 Result = Abs(a.x - b.x) + Abs(a.y - b.y);
    return Result;
}

inline s32 ChebyshevDistance(const vec2i& a, const vec2i& b)
{
    s32 Result = Max(Abs(a.x - b.x), Abs(a.y - b.y));
    return Result;
}

inline f32& vec3::operator[](int idx)
{
    return (&x)[idx];
}
inline const f32& vec3::operator[](int idx) const
{
    return (&x)[idx];
}

inline vec3::operator vec3i() const
{
    vec3i Result = { (s32)x, (s32)y, (s32)z };
    return Result;
}

inline vec3::operator vec2() const
{
    vec2 Result = { x, y };
    return Result;
}

inline vec3 operator-(const vec3& v)
{
    vec3 Result = { -v.x, -v.y, -v.z };
    return Result;
}
inline vec3& operator*=(vec3& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
    return v;
}
inline vec3& operator+=(vec3& v, const vec3& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    v.z += Other.z;
    return v;
}
inline vec3& operator-=(vec3& v, const vec3& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    v.z -= Other.z;
    return v;
}
inline vec3& operator*=(vec3& v, const vec3& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    v.z *= Other.z;
    return v;
}
inline vec3& operator/=(vec3& v, const vec3& Other) 
{
    v.x /= Other.x;
    v.y /= Other.y;
    v.z /= Other.z;
    return v;
}

inline vec3 operator*(const vec3& v, f32 s)
{
    vec3 Result = { v.x * s, v.y * s, v.z * s };
    return Result;
}
inline vec3 operator*(f32 s, const vec3& v)
{
    vec3 Result = v * s;
    return Result;
}
inline vec3 operator/(const vec3& v, f32 s)
{
    f32 Inv = 1.0f / s;
    vec3 Result = v * Inv;
    return Result;
}

inline vec3 operator+(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return Result;
}
inline vec3 operator-(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return Result;
}
inline vec3 operator*(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x * b.x, a.y * b.y, a.z * b.z };
    return Result;
}
inline vec3 operator/(const vec3& a, const vec3& b) 
{
    vec3 Result = { a.x / b.x, a.y / b.y, a.z / b.z };
    return Result;
}

inline vec3 Floor(const vec3& v)
{
    vec3 Result = { Floor(v.x), Floor(v.y), Floor(v.z) };
    return Result;
}

inline vec3 Ceil(const vec3& v)
{
    vec3 Result = { Ceil(v.x), Ceil(v.y), Ceil(v.z) };
    return Result;
}

inline f32 Dot(const vec3& a, const vec3& b)
{
    f32 Result = a.x*b.x + a.y*b.y + a.z*b.z;
    return Result;
}
inline vec3 Cross(const vec3& a, const vec3& b)
{
    vec3 Result = 
    {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x,
    };
    return Result;
}

inline f32 Length(const vec3& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
inline vec3 Normalize(const vec3& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec3 Result = v * InvLength;
    return Result;
}

inline vec3 NOZ(const vec3& v)
{
    vec3 Result;
    f32 Len = Length(v);
    if (Len != 0.0f)
    {
        f32 InvLength = 1.0f / Len;
        Result = v * InvLength;
    }
    else
    {
        Result = v;
    }
    return Result;
}

inline vec3 Projection(const vec3& a, const vec3& b)
{
    vec3 Result = Dot(a, b) * b;
    return Result;
}

inline vec3 Rejection(const vec3& a, const vec3& b)
{
    vec3 Result = a - Projection(a, b);
    return Result;
}

inline s32& vec3i::operator[](int idx)
{
    return (&x)[idx];
}
inline const s32& vec3i::operator[](int idx) const
{
    return (&x)[idx];
}

inline vec3i::operator vec3() const
{
    vec3 Result = { (f32)x, (f32)y, (f32)z };
    return Result;
}

inline vec3i::operator vec2i() const
{
    vec2i Result = { x, y };
    return Result;
}

inline vec3i operator-(const vec3i& v)
{
    vec3i Result = { -v.x, -v.y, -v.z };
    return Result;
}

inline vec3i operator*(const vec3i& v, s32 s)
{
    vec3i Result = { v.x * s, v.y * s, v.z * s };
    return Result;
}
inline vec3i operator*(s32 s, const vec3i& v)
{
    vec3i Result = { v.x*s, v.y*s, v.z*s };
    return Result;
}
inline vec3i operator/(const vec3i& v, s32 s)
{
    vec3i Result = { v.x/s, v.y/s, v.z/s };
    return Result;
}

inline vec3i operator+(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return Result;
}
inline vec3i operator-(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return Result;
}
inline vec3i operator*(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x * b.x, a.y * b.y, a.z * b.z };
    return Result;
}
inline vec3i operator/(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x / b.x, a.y / b.y, a.z / b.z };
    return Result;
}

inline f32& vec4::operator[](int idx)
{
    return (&x)[idx];
}
inline const f32& vec4::operator[](int idx) const
{
    return (&x)[idx];
}

inline vec4 operator-(const vec4& v)
{
    vec4 Result = { -v.x, -v.y, -v.z, -v.w };
    return Result;
}

inline vec4 operator+(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
    return Result;
}
inline vec4 operator-(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
    return Result;
}
inline vec4 operator*(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    return Result;
}
inline vec4 operator/(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.z };
    return Result;
}

inline vec4& operator+=(vec4& v, const vec4& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    v.z += Other.z;
    v.w += Other.w;
    return v;
}
inline vec4& operator-=(vec4& v, const vec4& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    v.z -= Other.z;
    v.w -= Other.w;
    return v;
}
inline vec4& operator*=(vec4& v, const vec4& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    v.z *= Other.z;
    v.w *= Other.w;
    return v;
}
inline vec4& operator/=(vec4& v, const vec4& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    v.z /= Other.z;
    v.w /= Other.w;
    return v;
}

inline vec4 operator*(const vec4& v, f32 s)
{
    vec4 Result = { v.x * s, v.y * s, v.z * s, v.w * s };
    return Result;
}
inline vec4 operator*(f32 s, const vec4& v)
{
    vec4 Result = { v.x * s, v.y * s, v.z * s, v.w * s };
    return Result;
}
inline vec4& operator*=(vec4& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
    v.w *= s;
    return v;
}

inline f32 Dot(const vec4& a, const vec4& b)
{
    f32 Result = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    return Result;
}
inline f32 Length(const vec4& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
inline vec4 Normalize(const vec4& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec4 Result = v * InvLength;
    return Result;
}

//
// mat2
//

inline f32& mat2::operator()(int i, int j)
{
    return m[j][i];
}
inline const f32& mat2::operator()(int i, int j) const 
{
    return m[j][i];
}

inline mat2 Mat2(
    f32 m00, f32 m01,
    f32 m10, f32 m11)
{
    mat2 Result = 
    {
        m00, m10,
        m01, m11,
    };
    return Result;
}

inline vec2 operator*(const mat2& M, const vec2& v)
{
    vec2 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y,
        M(1, 0) * v.x + M(1, 1) * v.y,
    };
    return Result;
}

inline mat2 Identity2()
{
    mat2 Result = 
    {
        1.0f, 0.0f,
        0.0f, 1.0f,
    };
    return Result;
}

//
// mat3
//

inline f32& mat3::operator()(int i, int j)
{
    return m[j][i];
}
inline const f32& mat3::operator()(int i, int j) const 
{
    return m[j][i];
}

inline mat3 Mat3(
    f32 m00, f32 m01, f32 m02,
    f32 m10, f32 m11, f32 m12,
    f32 m20, f32 m21, f32 m22)
{
    mat3 Result = 
    {
        m00, m10, m20,
        m01, m11, m21,
        m02, m12, m22,
    };
    return Result;
}

inline vec3 operator*(const mat3& M, const vec3& v)
{
    vec3 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y + M(0, 2) * v.z,
        M(1, 0) * v.x + M(1, 1) * v.y + M(1, 2) * v.z,
        M(2, 0) * v.x + M(2, 1) * v.y + M(2, 2) * v.z,
    };
    return Result;
}

inline mat3 Identity3()
{
    mat3 Result = 
    {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    return Result;
}

//
// mat4
// 

inline f32& mat4::operator()(int i, int j)
{
    return m[j][i];
}
inline const f32& mat4::operator()(int i, int j) const 
{
    return m[j][i];
}

inline mat4 Mat4(f32 m00, f32 m01, f32 m02, f32 m03,
          f32 m10, f32 m11, f32 m12, f32 m13,
          f32 m20, f32 m21, f32 m22, f32 m23,
          f32 m30, f32 m31, f32 m32, f32 m33)
{
    mat4 Result = 
    {
        m00, m10, m20, m30,
        m01, m11, m21, m31,
        m02, m12, m22, m32,
        m03, m13, m23, m33,
    };
    return Result;
}

inline mat4 Identity4()
{
    mat4 Result = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return Result;
}

inline mat4 PerspectiveMat4(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far) 
{
    const f32 InvTanHalfFov = 1.0f / Tan(0.5f * FieldOfView);
    const f32 ZRange = Far - Near;

    mat4 Result = Mat4(
        InvTanHalfFov / AspectRatio, 0.0f, 0.0f, 0.0f,
        0.0f, InvTanHalfFov, 0.0f, 0.0f,
        0.0f, 0.0f, Far / ZRange, -Near*Far / ZRange,
        0.0f, 0.0f,  1.0f, 0.0f);
    return Result;
}

inline mat4 PerspectiveReverse(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far)
{
    const f32 InvTanHalfFov = 1.0f / Tan(0.5f * FieldOfView);
    const f32 ZRange = Far - Near;

    mat4 Result = Mat4(
        InvTanHalfFov / AspectRatio, 0.0f, 0.0f, 0.0f,
        0.0f, InvTanHalfFov, 0.0f, 0.0f,
        0.0f, 0.0f, -Near / ZRange, Near*Far / ZRange,
        0.0f, 0.0f, 1.0f, 0.0f);
    return Result;
}

inline vec4 operator*(const mat4& M, const vec4& v)
{
    vec4 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y + M(0, 2) * v.z + M(0, 3) * v.w,
        M(1, 0) * v.x + M(1, 1) * v.y + M(1, 2) * v.z + M(1, 3) * v.w,
        M(2, 0) * v.x + M(2, 1) * v.y + M(2, 2) * v.z + M(2, 3) * v.w,
        M(3, 0) * v.x + M(3, 1) * v.y + M(3, 2) * v.z + M(3, 3) * v.w,
    };
    return Result;
}
inline vec4 operator*(const vec4& v, const mat4& M)
{
    vec4 Result = 
    {
        M(0, 0) * v.x + M(1, 0) * v.y + M(2, 0) * v.z + M(3, 0) * v.w,
        M(0, 1) * v.x + M(1, 1) * v.y + M(2, 1) * v.z + M(3, 1) * v.w,
        M(0, 2) * v.x + M(1, 2) * v.y + M(2, 2) * v.z + M(3, 2) * v.w,
        M(0, 3) * v.x + M(1, 3) * v.y + M(2, 3) * v.z + M(3, 3) * v.w,
    };
    return Result;
}

inline mat4 operator*(const mat4& A, const mat4& B) 
{
    mat4 Result = {};
    // Col
    for (u32 j = 0; j < 4; j++)
    {
        // Row
        for (u32 i = 0; i < 4; i++)
        {
            Result(i, j) = 
                A(i, 0) * B(0, j) + 
                A(i, 1) * B(1, j) + 
                A(i, 2) * B(2, j) + 
                A(i, 3) * B(3, j);
        }
    }
    return Result;
}

inline vec3 TransformDirection(const mat4& M, const vec3& v)
{
    vec3 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y + M(0, 2) * v.z,
        M(1, 0) * v.x + M(1, 1) * v.y + M(1, 2) * v.z,
        M(2, 0) * v.x + M(2, 1) * v.y + M(2, 2) * v.z,
    };
    return Result;
}
inline vec3 TransformPoint(const mat4& M, const vec3& p) 
{
    vec3 Result = 
    {
        M(0, 0) * p.x + M(0, 1) * p.y + M(0, 2) * p.z + M(0, 3),
        M(1, 0) * p.x + M(1, 1) * p.y + M(1, 2) * p.z + M(1, 3),
        M(2, 0) * p.x + M(2, 1) * p.y + M(2, 2) * p.z + M(2, 3),
    };
    return Result;
}
