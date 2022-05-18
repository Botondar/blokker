#include "Math.hpp"

f32& vec2::operator[](int idx)
{
    return (&x)[idx];
}
const f32& vec2::operator[](int idx) const
{
    return (&x)[idx];
}

vec2::operator vec2i() const
{
    vec2i Result = { (s32)x, (s32)y };
    return Result;
}

vec2::operator vec3() const
{
    vec3 Result = { x, y, 0.0f };
    return Result;
}

vec2 operator-(const vec2& v)
{
    vec2 Result = { -v.x, -v.y };
    return Result;
}
vec2& operator*=(vec2& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    return v;
}
vec2& operator+=(vec2& v, const vec2& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    return v;
}
vec2& operator-=(vec2& v, const vec2& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    return v;
}
vec2& operator*=(vec2& v, const vec2& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    return v;
}
vec2& operator/=(vec2& v, const vec2& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    return v;
}

vec2 operator*(const vec2& v, f32 s)
{
    vec2 Result = { v.x*s, v.y*s };
    return Result;
}
vec2 operator*(f32 s, const vec2& v)
{
    vec2 Result = { s*v.x, s*v.y };
    return Result;
}

vec2 operator+(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x + b.x, a.y + b.y };
    return Result;
}
vec2 operator-(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x - b.x, a.y - b.y };
    return Result;
}
vec2 operator*(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x * b.x, a.y * b.y };
    return Result;
}
vec2 operator/(const vec2& a, const vec2& b)
{
    vec2 Result = { a.x / b.x, a.y / b.y };
    return Result;
}

vec2 Floor(const vec2& v)
{
    vec2 Result = { Floor(v.x), Floor(v.y) };
    return Result;
}

f32 Dot(const vec2& a, const vec2& b)
{
    f32 Result = a.x*b.x + a.y*b.y;
    return Result;
}
f32 Length(const vec2& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
vec2 Normalize(const vec2& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec2 Result = v * InvLength;
    return Result;
}

vec2 SafeNormalize(const vec2& v)
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

s32& vec2i::operator[](int idx)
{
    return (&x)[idx];
}
const s32& vec2i::operator[](int idx) const
{
    return (&x)[idx];
}

vec2i::operator vec2() const
{
    vec2 Result = { (f32)x, (f32)y };
    return Result;
}

vec2i::operator vec3i() const
{
    vec3i Result = { x, y, 0 };
    return Result;
}

vec2i operator-(const vec2i& v)
{
    vec2i Result = { -v.x, -v.y };
    return Result;
}
vec2i& operator*=(vec2i& v, s32 s)
{
    v.x *= s;
    v.y *= s;
    return v;
}
vec2i& operator+=(vec2i& v, const vec2i& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    return v;
}
vec2i& operator-=(vec2i& v, const vec2i& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    return v;
}
vec2i& operator*=(vec2i& v, const vec2i& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    return v;
}
vec2i& operator/=(vec2i& v, const vec2i& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    return v;
}

vec2i operator*(const vec2i& v, s32 s)
{
    vec2i Result = { v.x * s, v.y * s };
    return Result;
}
vec2i operator*(s32 s, const vec2i& v)
{
    vec2i Result = { s * v.x, s * v.y };
    return Result;
}

vec2i operator+(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x + b.x, a.y + b.y };
    return Result;
}
vec2i operator-(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x - b.x, a.y - b.y };
    return Result;
}
vec2i operator*(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x * b.x, a.y * b.y };
    return Result;
}
vec2i operator/(const vec2i& a, const vec2i& b)
{
    vec2i Result = { a.x / b.x, a.y / b.y };
    return Result;
}

s32 ManhattanDistance(const vec2i& a, const vec2i& b)
{
    s32 Result = Abs(a.x - b.x) + Abs(a.y - b.y);
    return Result;
}

s32 ChebyshevDistance(const vec2i& a, const vec2i& b)
{
    s32 Result = Max(Abs(a.x - b.x), Abs(a.y - b.y));
    return Result;
}

f32& vec3::operator[](int idx)
{
    return (&x)[idx];
}
const f32& vec3::operator[](int idx) const
{
    return (&x)[idx];
}

vec3::operator vec3i() const
{
    vec3i Result = { (s32)x, (s32)y, (s32)z };
    return Result;
}

vec3::operator vec2() const
{
    vec2 Result = { x, y };
    return Result;
}

vec3 operator-(const vec3& v)
{
    vec3 Result = { -v.x, -v.y, -v.z };
    return Result;
}
vec3& operator*=(vec3& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
    return v;
}
vec3& operator+=(vec3& v, const vec3& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    v.z += Other.z;
    return v;
}
vec3& operator-=(vec3& v, const vec3& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    v.z -= Other.z;
    return v;
}
vec3& operator*=(vec3& v, const vec3& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    v.z *= Other.z;
    return v;
}
vec3& operator/=(vec3& v, const vec3& Other) 
{
    v.x /= Other.x;
    v.y /= Other.y;
    v.z /= Other.z;
    return v;
}

vec3 operator*(const vec3& v, f32 s)
{
    vec3 Result = { v.x * s, v.y * s, v.z * s };
    return Result;
}
vec3 operator*(f32 s, const vec3& v)
{
    vec3 Result = v * s;
    return Result;
}
vec3 operator/(const vec3& v, f32 s)
{
    f32 Inv = 1.0f / s;
    vec3 Result = v * Inv;
    return Result;
}

vec3 operator+(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return Result;
}
vec3 operator-(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return Result;
}
vec3 operator*(const vec3& a, const vec3& b)
{
    vec3 Result = { a.x * b.x, a.y * b.y, a.z * b.z };
    return Result;
}
vec3 operator/(const vec3& a, const vec3& b) 
{
    vec3 Result = { a.x / b.x, a.y / b.y, a.z / b.z };
    return Result;
}

vec3 Floor(const vec3& v)
{
    vec3 Result = { Floor(v.x), Floor(v.y), Floor(v.z) };
    return Result;
}

vec3 Ceil(const vec3& v)
{
    vec3 Result = { Ceil(v.x), Ceil(v.y), Ceil(v.z) };
    return Result;
}

f32 Dot(const vec3& a, const vec3& b)
{
    f32 Result = a.x*b.x + a.y*b.y + a.z*b.z;
    return Result;
}
vec3 Cross(const vec3& a, const vec3& b)
{
    vec3 Result = 
    {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x,
    };
    return Result;
}

f32 Length(const vec3& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
vec3 Normalize(const vec3& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec3 Result = v * InvLength;
    return Result;
}

vec3 SafeNormalize(const vec3& v)
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

vec3 Projection(const vec3& a, const vec3& b)
{
    vec3 Result = Dot(a, b) * b;
    return Result;
}

vec3 Rejection(const vec3& a, const vec3& b)
{
    vec3 Result = a - Projection(a, b);
    return Result;
}

s32& vec3i::operator[](int idx)
{
    return (&x)[idx];
}
const s32& vec3i::operator[](int idx) const
{
    return (&x)[idx];
}

vec3i::operator vec3() const
{
    vec3 Result = { (f32)x, (f32)y, (f32)z };
    return Result;
}

vec3i::operator vec2i() const
{
    vec2i Result = { x, y };
    return Result;
}

vec3i operator-(const vec3i& v)
{
    vec3i Result = { -v.x, -v.y, -v.z };
    return Result;
}

vec3i operator*(const vec3i& v, s32 s)
{
    vec3i Result = { v.x * s, v.y * s, v.z * s };
    return Result;
}
vec3i operator*(s32 s, const vec3i& v)
{
    vec3i Result = { v.x*s, v.y*s, v.z*s };
    return Result;
}
vec3i operator/(const vec3i& v, s32 s)
{
    vec3i Result = { v.x/s, v.y/s, v.z/s };
    return Result;
}

vec3i operator+(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return Result;
}
vec3i operator-(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return Result;
}
vec3i operator*(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x * b.x, a.y * b.y, a.z * b.z };
    return Result;
}
vec3i operator/(const vec3i& a, const vec3i& b)
{
    vec3i Result = { a.x / b.x, a.y / b.y, a.z / b.z };
    return Result;
}

f32& vec4::operator[](int idx)
{
    return (&x)[idx];
}
const f32& vec4::operator[](int idx) const
{
    return (&x)[idx];
}

vec4 operator-(const vec4& v)
{
    vec4 Result = { -v.x, -v.y, -v.z, -v.w };
    return Result;
}

vec4 operator+(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
    return Result;
}
vec4 operator-(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
    return Result;
}
vec4 operator*(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    return Result;
}
vec4 operator/(const vec4& a, const vec4& b)
{
    vec4 Result = { a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.z };
    return Result;
}

vec4& operator+=(vec4& v, const vec4& Other)
{
    v.x += Other.x;
    v.y += Other.y;
    v.z += Other.z;
    v.w += Other.w;
    return v;
}
vec4& operator-=(vec4& v, const vec4& Other)
{
    v.x -= Other.x;
    v.y -= Other.y;
    v.z -= Other.z;
    v.w -= Other.w;
    return v;
}
vec4& operator*=(vec4& v, const vec4& Other)
{
    v.x *= Other.x;
    v.y *= Other.y;
    v.z *= Other.z;
    v.w *= Other.w;
    return v;
}
vec4& operator/=(vec4& v, const vec4& Other)
{
    v.x /= Other.x;
    v.y /= Other.y;
    v.z /= Other.z;
    v.w /= Other.w;
    return v;
}

vec4 operator*(const vec4& v, f32 s)
{
    vec4 Result = { v.x * s, v.y * s, v.z * s, v.w * s };
    return Result;
}
vec4 operator*(f32 s, const vec4& v)
{
    vec4 Result = { v.x * s, v.y * s, v.z * s, v.w * s };
    return Result;
}
vec4& operator*=(vec4& v, f32 s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
    v.w *= s;
    return v;
}

f32 Dot(const vec4& a, const vec4& b)
{
    f32 Result = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    return Result;
}
f32 Length(const vec4& v)
{
    f32 Result = Sqrt(Dot(v, v));
    return Result;
}
vec4 Normalize(const vec4& v)
{
    f32 InvLength = 1.0f / Length(v);
    vec4 Result = v * InvLength;
    return Result;
}

//
// mat2
//

f32& mat2::operator()(int i, int j)
{
    return m[j][i];
}
const f32& mat2::operator()(int i, int j) const 
{
    return m[j][i];
}

mat2 Mat2(
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

vec2 operator*(const mat2& M, const vec2& v)
{
    vec2 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y,
        M(1, 0) * v.x + M(1, 1) * v.y,
    };
    return Result;
}

mat2 Identity2()
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

f32& mat3::operator()(int i, int j)
{
    return m[j][i];
}
const f32& mat3::operator()(int i, int j) const 
{
    return m[j][i];
}

mat3 Mat3(
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

vec3 operator*(const mat3& M, const vec3& v)
{
    vec3 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y + M(0, 2) * v.z,
        M(1, 0) * v.x + M(1, 1) * v.y + M(1, 2) * v.z,
        M(2, 0) * v.x + M(2, 1) * v.y + M(2, 2) * v.z,
    };
    return Result;
}

mat3 Identity3()
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

f32& mat4::operator()(int i, int j)
{
    return m[j][i];
}
const f32& mat4::operator()(int i, int j) const 
{
    return m[j][i];
}

mat4 Mat4(f32 m00, f32 m01, f32 m02, f32 m03,
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

mat4 Identity4()
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

mat4 PerspectiveMat4(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far) 
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

mat4 PerspectiveReverse(f32 FieldOfView, f32 AspectRatio, f32 Near, f32 Far)
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

vec4 operator*(const mat4& M, const vec4& v)
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
vec4 operator*(const vec4& v, const mat4& M)
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

mat4 operator*(const mat4& A, const mat4& B) 
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

vec3 TransformDirection(const mat4& M, const vec3& v)
{
    vec3 Result = 
    {
        M(0, 0) * v.x + M(0, 1) * v.y + M(0, 2) * v.z,
        M(1, 0) * v.x + M(1, 1) * v.y + M(1, 2) * v.z,
        M(2, 0) * v.x + M(2, 1) * v.y + M(2, 2) * v.z,
    };
    return Result;
}
vec3 TransformPoint(const mat4& M, const vec3& p) 
{
    vec3 Result = 
    {
        M(0, 0) * p.x + M(0, 1) * p.y + M(0, 2) * p.z + M(0, 3),
        M(1, 0) * p.x + M(1, 1) * p.y + M(1, 2) * p.z + M(1, 3),
        M(2, 0) * p.x + M(2, 1) * p.y + M(2, 2) * p.z + M(2, 3),
    };
    return Result;
}
