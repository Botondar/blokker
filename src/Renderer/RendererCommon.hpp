#pragma once

#include <Common.hpp>

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

typedef u32 packed_position;
typedef u32 packed_texcoord;

constexpr packed_position POSITION_X_MASK = 0xF8000000u;
constexpr packed_position POSITION_Y_MASK = 0x07C00000u;
constexpr packed_position POSITION_Z_MASK = 0x003FFFFFu;
constexpr packed_position POSITION_X_SHIFT = 27u;
constexpr packed_position POSITION_Y_SHIFT = 22u;
//constexpr packed_position POSITION_Z_SHIFT = 0;

constexpr packed_texcoord TEXCOORD_LAYER_MASK = 0x07FFu;
constexpr packed_texcoord TEXCOORD_U_MASK = 0x0800u;
constexpr packed_texcoord TEXCOORD_V_MASK = 0x1000u;
constexpr u32 TEXCOORD_U_SHIFT = 11;
constexpr u32 TEXCOORD_V_SHIFT = 12;
//constexpr u32 TEXCOORD_LAYER_SHIFT = 0;

constexpr u32 TEXCOORD_AO_MASK = 0xC000;
constexpr u32 TEXCOORD_AO_SHIFT = 14;

inline constexpr packed_position PackPosition(vec3 P)
{
    // TODO(boti): figure out a way to pack xy coords in 4 bits?
    u32 Result = 
        (((u32)P.x << POSITION_X_SHIFT) & POSITION_X_MASK) |
        (((u32)P.y << POSITION_Y_SHIFT) & POSITION_Y_MASK) |
        ((u32)P.z & POSITION_Z_MASK);
    return Result;
}

inline constexpr packed_texcoord PackTexCoord(u32 u, u32 v, u32 Layer, u32 AO = 0x00)
{
    u32 Result = 
        ((u << TEXCOORD_U_SHIFT) & TEXCOORD_U_MASK) |
        ((v << TEXCOORD_V_SHIFT) & TEXCOORD_V_MASK) |
        (Layer & TEXCOORD_LAYER_MASK) |
        ((AO << TEXCOORD_AO_SHIFT) & TEXCOORD_AO_MASK);
    return Result;
}

#pragma pack(push, 1)
struct terrain_vertex
{
    packed_position P;
    packed_texcoord TexCoord;
};
#pragma pack(pop)

#if 1
struct vertex
{
    vec3 P;
    vec3 UVW;
    u32 Color;
};
#endif

enum attrib_location : u32
{
    ATTRIB_POS = 0,
    ATTRIB_TEXCOORD = 1,
    ATTRIB_COLOR = 2,
    ATTRIB_CHUNK_P = 3,
};

struct per_frame_uniform_data
{
    mat4 ProjectionTransform;
    mat4 ViewTransform;
    mat4 ViewProjectionTransform;

    vec4 SunDirection;
};