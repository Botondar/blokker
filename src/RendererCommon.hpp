#pragma once

inline constexpr u32 PackColor(u32 R, u32 G, u32 B, u32 A = 0xFF)
{
    R &= 0xFF;
    G &= 0xFF;
    B &= 0xFF;
    A &= 0xFF;

    u32 Result = 
        (A << 24) |
        (B << 16) |
        (G << 8) |
        (R << 0);
    return Result;
}

inline u32 PackColor(const vec3& v)
{
    u32 R = (u32)Round(255.0f * Clamp(v.x, 0.0f, 1.0f));
    u32 G = (u32)Round(255.0f * Clamp(v.y, 0.0f, 1.0f));
    u32 B = (u32)Round(255.0f * Clamp(v.z, 0.0f, 1.0f));

    u32 Result = PackColor(R, G, B);
    return Result;
}

inline vec3 UnpackColor3(u32 c)
{
    f32 R = ((c >> 0) & 0xFF) / 255.0f;
    f32 G = ((c >> 8) & 0xFF) / 255.0f;
    f32 B = ((c >> 16) & 0xFF) / 255.0f;

    vec3 Result = { R, G, B };
    return Result;
}

struct vertex 
{
    vec3 P;
    vec3 UVW;
    u32 Color;
};

enum attrib_location : u32
{
    ATTRIB_POS = 0,
    ATTRIB_TEXCOORD = 1,
    ATTRIB_COLOR = 2,
};