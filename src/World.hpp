#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>

#include <Chunk.hpp>
#include <Camera.hpp>
#include <Shapes.hpp>

// Forward declare
struct game_input;
struct renderer;
struct renderer_frame_params;

//
// Player
//

struct player_control
{
    vec2 DesiredMoveDirection; // Local space, unnormalized
    bool IsJumping;
    bool IsRunning;
    bool PrimaryAction;
    bool SecondaryAction;
};

struct player
{
    vec3 P;
    vec3 Velocity;
    f32 Yaw, Pitch;
    bool WasGroundedLastFrame;

    static constexpr f32 Height = 1.8f;
    static constexpr f32 EyeHeight = 1.75f;
    static constexpr f32 LegHeight = 0.51f;
    static constexpr f32 Width = 0.6f;

    f32 HeadBob;
    f32 BobAmplitude = 0.05f;
    f32 BobFrequency = 2.0f;

    static constexpr f32 WalkBobAmplitude = 0.025f;
    static constexpr f32 WalkBobFrequency = 2.0f;

    static constexpr f32 RunBobAmplitude = 0.075f;
    static constexpr f32 RunBobFrequency = 2.5f;

    static constexpr f32 DefaultFov = ToRadians(80.0f);

    f32 FieldOfView;
    f32 CurrentFov;
    f32 TargetFov;

    static constexpr f32 BlockBreakTime = 0.5f;
    bool HasTargetBlock;
    vec3i TargetBlock;
    direction TargetDirection;
    f32 BreakTime;

    static constexpr f32 MaxBlockPlacementFrequency = 0.2f;
    f32 TimeSinceLastBlockPlacement;

    player_control Control;
};

static camera Player_GetCamera(const player* Player);
static aabb Player_GetAABB(const player* Player);
static void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right);
static vec3 Player_GetForward(const player* Player);

//
// World
//

struct world
{
    renderer* Renderer;
    u64 FrameIndex;

    perlin2 Perlin2;
    perlin3 Perlin3;

#if BLOKKER_TINY_RENDER_DISTANCE
    static constexpr u32 MaxChunkCount = 36;
    static constexpr u32 MaxChunkCountSqrt = 6;
#else
    static constexpr u32 MaxChunkCount = 16384;
    static constexpr u32 MaxChunkCountSqrt = 128;
#endif

    u32 ChunkCount;
    chunk* Chunks;
    chunk_data* ChunkData;

    u32 ChunkRenderDataCount;
    chunk_render_data ChunkRenderData[MaxChunkCount];

    player Player;

    // Debug
    struct 
    {
        bool IsDebuggingEnabled;
        bool IsHitboxEnabled;
        bool IsDebugCameraEnabled;
        camera DebugCamera;
    } Debug;
};

// From chunk position
static chunk* World_GetChunkFromP(world* World, vec2i P);
// From voxel position
static chunk* World_GetChunkFromP(world* World, vec3i P, vec3i* RelP);
static u16 World_GetVoxelType(world* World, vec3i P);
static bool World_SetVoxelType(world* World, vec3i P, u16 Type);
static voxel_neighborhood World_GetVoxelNeighborhood(world* World, vec3i P);


static bool World_RayCast(
    world* World, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, direction* OutDir);

static chunk* World_ReserveChunk(world* World, vec2i P);
static chunk* World_FindPlayerChunk(world* World);

static void World_ResetPlayer(world* World);

static bool World_Initialize(world* World);
static void World_Update(world* World, game_input* Input, f32 DeltaTime);
static void World_Render(world* World, renderer_frame_params* Frame);