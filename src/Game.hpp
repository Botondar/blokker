#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <Renderer.hpp>
#include <Camera.hpp>
#include <Chunk.hpp>

struct aabb
{
    vec3 Min;
    vec3 Max;
};

// NOTE: Overlap is the amount that A overlaps B. in each direction (B is treated as static)
static bool AABB_Intersect(const aabb& A, const aabb& B, vec3& Overlap, int& MinCoord);

struct player
{
    vec3 P;
    vec3 Velocity;
    f32 Yaw, Pitch;
    bool WasGroundedLastFrame;
};

static aabb Player_GetAABB(const player* Player);
static void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right);

struct game_input
{
    bool IsCursorEnabled;
    vec2 MouseP;
    vec2 MouseDelta;

    bool EscapePressed;

    bool Forward;
    bool Back;
    bool Right;
    bool Left;

    bool Space;
    bool LeftShift;
    bool LeftControl;
    bool LeftAlt;
};

struct game_state
{
    vulkan_renderer* Renderer;

    // TODO: this maybe shouldn't be here
    bool NeedRendererResize;
    bool IsMinimized;

    u64 FrameIndex;
    player Player;

    perlin2 Perlin;

    static constexpr u32 MaxChunkCount = 16384;

    u32 ChunkCount;
    chunk* Chunks;
    chunk_data* ChunkData;

#if 0
    // TODO: these will need to be part of the renderer
    vulkan_staging_heap StagingHeap;

    vulkan_vertex_buffer VB;

    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;

    VkSampler Sampler;

    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorPool DescriptorPool;
    VkDescriptorSet DescriptorSet;

    VkImage Tex;
    VkImageView TexView;
    VkDeviceMemory TexMemory;
#endif
};

bool Game_Initialize(game_state* GameState);
void Game_UpdateAndRender(
    game_state* GameState,
    game_input* Input, 
    f32 DeltaTime);
