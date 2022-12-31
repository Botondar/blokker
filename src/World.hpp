#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>

#include <Chunk.hpp>
#include <Camera.hpp>
#include <Shapes.hpp>

#include <Player.hpp>

#define BLOKKER_TINY_RENDER_DISTANCE 0

// Forward declare
struct renderer;
struct renderer_frame_params;

inline vec2i GetChunkP(vec2i WorldP)
{
    vec2i Result = 
    {
        FloorDiv(WorldP.x, CHUNK_DIM_X) * CHUNK_DIM_X,
        FloorDiv(WorldP.y, CHUNK_DIM_Y) * CHUNK_DIM_Y,
    };
    return(Result);
}

//
// World
//

struct map_view
{
    static constexpr f32 PitchMax = ToRadians(-15.0f);
    static constexpr f32 PitchMin = ToRadians(-75.0f);

    vec2 CurrentP;
    vec2 TargetP;
    f32 CurrentYaw;
    f32 TargetYaw;
    f32 CurrentPitch;
    f32 TargetPitch;

    f32 ZoomTarget;
    f32 ZoomCurrent;
    bool IsEnabled;

    void ResetAll(world* World);
    void Reset(world* World);
    mat2 GetAxesXY() const;
};

struct world
{
    // NOTE(boti): for now the world just piggy-backs off of the game state's memory arena
    memory_arena* Arena;

    renderer* Renderer;
    u64 FrameIndex;

    perlin2 Perlin2;
    perlin3 Perlin3;

#if BLOKKER_TINY_RENDER_DISTANCE
    static constexpr u32 MaxChunkCount = 81;
    static constexpr u32 MaxChunkCountSqrt = 9;
#else
    static constexpr u32 MaxChunkCount = 16384;
    static constexpr u32 MaxChunkCountSqrt = 128;
    static_assert(MaxChunkCountSqrt*MaxChunkCountSqrt == MaxChunkCount);
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

    map_view MapView;
};

// From chunk position
chunk* World_GetChunkFromP(world* World, vec2i P);
// From voxel position
chunk* World_GetChunkFromP(world* World, vec3i P, vec3i* RelP);
u16 World_GetVoxelType(world* World, vec3i P);
bool World_SetVoxelType(world* World, vec3i P, u16 Type);
voxel_neighborhood World_GetVoxelNeighborhood(world* World, vec3i P);


static bool World_RayCast(
    world* World, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, direction* OutDir);

void World_ResetPlayer(world* World);

bool World_Initialize(world* World);

// TODO(boti): remove dt from HandleInput
void World_HandleInput(world* World, game_io* IO);
void World_Update(world* World, game_io* IO, memory_arena* TransientArena);
void World_Render(world* World, renderer_frame_params* Frame);

bool World_RayCast(world* World, vec3 P, vec3 V, f32 tMax, vec3i* OutP, direction* OutDir);

vec3 World_ApplyEntityMovement(world* World, entity* Entity, aabb AABB, vec3 dP);