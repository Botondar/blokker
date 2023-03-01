#pragma once

//
// Opaque types
//
struct renderer;
struct vertex_buffer_block;

//
// Common data types and utilities
//
typedef u32 packed_position;
typedef u32 packed_texcoord;

#pragma pack(push, 1)
struct terrain_vertex
{
    packed_position P;
    packed_texcoord TexCoord;
};
#pragma pack(pop)

struct vertex
{
    vec3 P;
    vec3 UVW;
    u32 Color;
};

inline constexpr u32 PackColor(u32 R, u32 G, u32 B, u32 A = 0xFF);
inline u32 PackColor(vec3 Color);
inline constexpr vec3 UnpackColor3(u32 Color);
inline constexpr packed_position PackPosition(vec3 P);
inline constexpr packed_texcoord PackTexCoord(u32 U, u32 V, u32 Layer, u32 Occlusion = 0x00);

//
// Rendering API
//

// NOTE(boti): draw commands are binary compatible with Vulkan/D3D12
struct draw_cmd
{
    u32 VertexCount;
    u32 InstanceCount;
    u32 VertexOffset;
    u32 InstanceOffset;
};

struct draw_cmd_indexed
{
    u32 IndexCount;
    u32 InstanceCount;
    u32 IndexOffset;
    u32 VertexOffset;
    u32 InstanceOffset;
};

struct render_frame
{
    renderer* Renderer;
    vec2i RenderExtent;

    mat4 ProjectionTransform;
    mat4 ViewTransform;
    mat4 PixelTransform;

    u32 MaxDrawCount;
    u32 DrawCount;
    draw_cmd* DrawList;
    vec2* DrawPositions;
};

struct renderer_init_info
{
    // NOTE(boti): Texture data must be RGBA8 format
    u32 TextureWidth, TextureHeight;
    u32 TextureArrayCount, TextureMipCount;
    void* TextureData;

    u32 ImGuiTextureWidth, ImGuiTextureHeight;
    void* ImGuiTextureData;
};

renderer* CreateRenderer(memory_arena* Arena, memory_arena* TransientArena,
                         const renderer_init_info* RendererInfo);

render_frame* BeginRenderFrame(renderer* Renderer, bool DoResize);
void EndRenderFrame(render_frame* Frame);

void SetCamera(render_frame* Frame, mat4 ViewTransform, mat4 ProjectionTransform);

vertex_buffer_block* AllocateAndUploadVertexBlock(render_frame* Frame, u64 HeadSize, const void* Head, u64 TailSize, const void* Tail);
bool UploadVertexBlock(render_frame* Frame, vertex_buffer_block* Block, u64 HeadSize, const void* Head, u64 TailSize, const void* Tail);
void FreeVertexBlock(render_frame* Frame, vertex_buffer_block* Block);

void RenderVertexBlock(render_frame* Frame, vertex_buffer_block* Block, vec2 P);
void RenderImGui(render_frame* Frame, const ImDrawData* DrawData);

enum class outline_type : u32
{
    Outer = 0,
    Inner,
};

bool ImTriangleList(render_frame* Frame, 
                    mat4 Transform, f32 DepthBias,
                    u32 VertexCount, const vertex* Vertices);

void ImBox(render_frame* Frame, aabb Box, u32 Color, f32 DepthBias /*= 0.0f*/);
void ImBoxOutline(render_frame* Frame, aabb Box, u32 Color, f32 OutlineSize);
void ImRect2D(render_frame* Frame, vec2 P0, vec2 P1, u32 Color);
void ImRectOutline2D(render_frame* Frame, vec2 P0, vec2 P1, u32 Color, f32 OutlineSize, outline_type OutlineType);

//
// Implementation
//

inline constexpr u32 PackColor(u32 R, u32 G, u32 B, u32 A /*= 0xFF*/)
{
    u32 Result = ((A & 0xFF) << 24) | ((B & 0xFF) << 16) | ((G & 0xFF) << 8) | (R & 0xFF);
    return(Result);
}

inline u32 PackColor(vec3 Color)
{
    u32 R = (u32)Round(255.0f * Clamp(Color.x, 0.0f, 1.0f));
    u32 G = (u32)Round(255.0f * Clamp(Color.y, 0.0f, 1.0f));
    u32 B = (u32)Round(255.0f * Clamp(Color.z, 0.0f, 1.0f));

    u32 Result = PackColor(R, G, B);
    return(Result);
}

inline constexpr vec3 UnpackColor3(u32 Color)
{
    f32 R = ((Color >> 0) & 0xFF) / 255.0f;
    f32 G = ((Color >> 8) & 0xFF) / 255.0f;
    f32 B = ((Color >> 16) & 0xFF) / 255.0f;

    vec3 Result = { R, G, B };
    return(Result);
}

inline constexpr packed_position PackPosition(vec3 P)
{
    constexpr packed_position POSITION_X_MASK = 0xF8000000u;
    constexpr packed_position POSITION_Y_MASK = 0x07C00000u;
    constexpr packed_position POSITION_Z_MASK = 0x003FFFFFu;
    constexpr packed_position POSITION_X_SHIFT = 27u;
    constexpr packed_position POSITION_Y_SHIFT = 22u;
    //constexpr packed_position POSITION_Z_SHIFT = 0;

    // TODO(boti): figure out a way to pack xy coords in 4 bits?
    u32 Result = 
        (((u32)P.x << POSITION_X_SHIFT) & POSITION_X_MASK) |
        (((u32)P.y << POSITION_Y_SHIFT) & POSITION_Y_MASK) |
        ((u32)P.z & POSITION_Z_MASK);
    return(Result);
}

inline constexpr packed_texcoord PackTexCoord(u32 U, u32 V, u32 Layer, u32 Occlusion /*= 0x00*/)
{
    constexpr packed_texcoord TEXCOORD_LAYER_MASK = 0x07FFu;
    constexpr packed_texcoord TEXCOORD_U_MASK = 0x0800u;
    constexpr packed_texcoord TEXCOORD_V_MASK = 0x1000u;
    constexpr u32 TEXCOORD_U_SHIFT = 11;
    constexpr u32 TEXCOORD_V_SHIFT = 12;
    //constexpr u32 TEXCOORD_LAYER_SHIFT = 0;
    constexpr u32 TEXCOORD_AO_MASK = 0xC000;
    constexpr u32 TEXCOORD_AO_SHIFT = 14;

    u32 Result = 
        ((U << TEXCOORD_U_SHIFT) & TEXCOORD_U_MASK) |
        ((V << TEXCOORD_V_SHIFT) & TEXCOORD_V_MASK) |
        (Layer & TEXCOORD_LAYER_MASK) |
        ((Occlusion << TEXCOORD_AO_SHIFT) & TEXCOORD_AO_MASK);
    return(Result);
}