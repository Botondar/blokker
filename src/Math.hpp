#pragma once

#include <Common.hpp>
#include <cmath>

constexpr f32 PI = 3.14159265358979323846f;

constexpr f32 ToRadians(f32 Degrees) { return PI * Degrees / 180.0f; }
constexpr f32 ToDegrees(f32 Radians) { return 180.0f * Radians / PI; }

inline s32 Abs(s32 x) { return abs(x); }
inline s32 Min(s32 a, s32 b) { return (a < b) ? a : b; }
inline s32 Max(s32 a, s32 b) { return (a < b) ? b : a; }

inline u64 Max(u64 a, u64 b) { return (a < b) ? a : b; }

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

inline f32 Lerp(f32 a, f32 b, f32 t) { return a*(1.0f - t) + b*t; }
inline constexpr f32 Fade3(f32 t) { return (3.0f - 2.0f*t)*t*t; }
inline constexpr f32 Fade5(f32 t) { return ((6.0f*t - 15.0f) * t + 10.0f)*t*t*t; }

struct vec2;
struct vec2i;
struct vec3;
struct vec3i;
struct vec4;
struct mat4;

struct vec2 
{
    f32 x, y;

    f32& operator[](int idx);
    const f32& operator[](int idx) const;

    explicit operator vec2i() const;
    explicit operator vec3() const;
};

vec2 operator-(const vec2& v);
vec2& operator*=(vec2& v, f32 s);
vec2& operator+=(vec2& v, const vec2& Other);
vec2& operator-=(vec2& v, const vec2& Other);
vec2& operator*=(vec2& v, const vec2& Other);
vec2& operator/=(vec2& v, const vec2& Other);

vec2 operator*(const vec2& v, f32 s);
vec2 operator*(f32 s, const vec2& v);

vec2 operator+(const vec2& a, const vec2& b);
vec2 operator-(const vec2& a, const vec2& b);
vec2 operator*(const vec2& a, const vec2& b);
vec2 operator/(const vec2& a, const vec2& b);

vec2 Floor(const vec2& v);

f32 Dot(const vec2& a, const vec2& b);
f32 Length(const vec2& v);
vec2 Normalize(const vec2& v);

struct vec2i 
{
    s32 x, y;

    s32& operator[](int idx);
    const s32& operator[](int idx) const;

    explicit operator vec2() const;
    explicit operator vec3i() const;

    bool operator==(const vec2i& Other) const = default;
};

vec2i operator-(const vec2i& v);
vec2i& operator*=(vec2i& v, s32 s);
vec2i& operator+=(vec2i& v, const vec2i& Other);
vec2i& operator-=(vec2i& v, const vec2i& Other);
vec2i& operator*=(vec2i& v, const vec2i& Other);
vec2i& operator/=(vec2i& v, const vec2i& Other);

vec2i operator*(const vec2i& v, s32 s);
vec2i operator*(s32 s, const vec2i& v);

vec2i operator+(const vec2i& a, const vec2i& b);
vec2i operator-(const vec2i& a, const vec2i& b);
vec2i operator*(const vec2i& a, const vec2i& b);
vec2i operator/(const vec2i& a, const vec2i& b);

s32 ManhattanDistance(const vec2i& a, const vec2i& b);
s32 ChebyshevDistance(const vec2i& a, const vec2i& b);

struct vec3 
{
    f32 x, y, z;

    f32& operator[](int idx);
    const f32& operator[](int idx) const;

    explicit operator vec3i() const;
    explicit operator vec2() const;
};

vec3 operator-(const vec3& v);
vec3& operator*=(vec3& v, f32 s);
vec3& operator+=(vec3& v, const vec3& Other);
vec3& operator-=(vec3& v, const vec3& Other);
vec3& operator*=(vec3& v, const vec3& Other);
vec3& operator/=(vec3& v, const vec3& Other);

vec3 operator*(const vec3& v, f32 s);
vec3 operator*(f32 s, const vec3& v);
vec3 operator/(const vec3& v, f32 s);

vec3 operator+(const vec3& a, const vec3& b);
vec3 operator-(const vec3& a, const vec3& b);
vec3 operator*(const vec3& a, const vec3& b);
vec3 operator/(const vec3& a, const vec3& b);

vec3 Floor(const vec3& v);
vec3 Ceil(const vec3& v);

f32 Dot(const vec3& a, const vec3& b);
f32 Length(const vec3& v);
vec3 Normalize(const vec3& v);
vec3 SafeNormalize(const vec3& v);

// NOTE(boti): b should be normalized
vec3 Projection(const vec3& a, const vec3& b);
// NOTE(boti): b should be normalized
vec3 Rejection(const vec3& a, const vec3& b);

vec3 Cross(const vec3& a, const vec3& b);

struct vec3i
{
    s32 x, y, z;

    bool operator==(const vec3i& Other) const = default;

    s32& operator[](int idx);
    const s32& operator[](int idx) const;

    explicit operator vec3() const;
    explicit operator vec2i() const;
};

vec3i operator-(const vec3i& v);

vec3i operator*(const vec3i& v, s32 s);
vec3i operator*(s32 s, const vec3i& v);
vec3i operator/(const vec3i& v, s32 s);

vec3i operator+(const vec3i& a, const vec3i& b);
vec3i operator-(const vec3i& a, const vec3i& b);
vec3i operator*(const vec3i& a, const vec3i& b);
vec3i operator/(const vec3i& a, const vec3i& b);

struct vec4 
{
    f32 x, y, z, w;

    f32& operator[](int idx);
    const f32& operator[](int idx) const;
};

vec4 operator-(const vec4& v);

vec4 operator+(const vec4& a, const vec4& b);
vec4 operator-(const vec4& a, const vec4& b);
vec4 operator*(const vec4& a, const vec4& b);
vec4 operator/(const vec4& a, const vec4& b);

vec4& operator+=(vec4& v, const vec4& Other);
vec4& operator-=(vec4& v, const vec4& Other);
vec4& operator*=(vec4& v, const vec4& Other);
vec4& operator/=(vec4& v, const vec4& Other);

vec4 operator*(const vec4& v, f32 s);
vec4 operator*(f32 s, const vec4& v);
vec4& operator*=(vec4& v, f32 s);

f32 Dot(const vec4& a, const vec4& b);
f32 Length(const vec4& v);
vec4 Normalize(const vec4& v);

struct mat4
{
    union
    {
        f32 m[4][4];
        f32 mm[16];
    };

    f32& operator()(int i, int j);
    const f32& operator()(int i, int j) const;
};

mat4 Mat4(f32 m00, f32 m01, f32 m02, f32 m03,
          f32 m10, f32 m11, f32 m12, f32 m13,
          f32 m20, f32 m21, f32 m22, f32 m23,
          f32 m30, f32 m31, f32 m32, f32 m33);

vec4 operator*(const mat4& M, const vec4& v);
vec4 operator*(const vec4& v, const mat4& M);
mat4 operator*(const mat4& A, const mat4& B);


mat4 Identity4();

mat4 PerspectiveMat4(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far);

vec3 TransformDirection(const mat4& M, const vec3& v);
vec3 TransformPoint(const mat4& M, const vec3& p);