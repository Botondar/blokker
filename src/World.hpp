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
        FloorDiv(WorldP.x, CHUNK_DIM_XY) * CHUNK_DIM_XY,
        FloorDiv(WorldP.y, CHUNK_DIM_XY) * CHUNK_DIM_XY,
    };
    return(Result);
}

//
// World
//

enum chunk_work_type
{
    ChunkWork_Generate,
    ChunkWork_BuildMesh,
};

struct chunk_work
{
    chunk_work_type Type;
    b32 IsReady;
    chunk* Chunk;
    union
    {
        struct
        {
            u32 FirstIndex;
            u32 OnePastLastIndex;
        } Mesh;
    };
};

struct chunk_work_queue
{
    static constexpr u32 MaxWorkCount = 1024;
    volatile u32 ReadIndex;
    volatile u32 WriteIndex;
    chunk_work WorkResults[MaxWorkCount];

    static constexpr u32 VertexBufferSize = MiB(32);
    static constexpr u32 VertexBufferCount = VertexBufferSize / sizeof(terrain_vertex);
    static_assert((VertexBufferSize % sizeof(terrain_vertex)) == 0);

    u32 VertexReadIndex;
    u32 VertexWriteIndex;
    terrain_vertex* VertexBuffer;

    // NOTE(boti): In rare cases it's possible for the meshes to be written into the vertex ring buffer
    //             in a different order than the one they arrive in in the WorkResults queue.
    //             We maintain the information of the last mesh for this reason, which seems sufficient for the 99.9%
    //             case because this race condition only happens when two (or more) threads finish meshing at the exact same
    //             time, but if it does become an issue later on, we could maintain a small buffer of the last meshes.
    b32 IsLastMeshValid;
    u32 LastMeshFirstIndex;
    u32 LastMeshOnePastLastIndex;
};

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

    chunk* Chunks;
    chunk_data* ChunkData;

    u32 ChunkRenderDataCount;
    chunk_render_data ChunkRenderData[MaxChunkCount];

    chunk_work_queue ChunkWorkQueue;

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

static chunk_work* GetNextChunkWorkToWrite(chunk_work_queue* Queue);

// From chunk position
chunk* GetChunkFromP(world* World, vec2i P);
// From voxel position
chunk* GetChunkFromP(world* World, vec3i P, vec3i* RelP);
u16 GetVoxelTypeAt(world* World, vec3i P);
bool SetVoxelTypeAt(world* World, vec3i P, u16 Type);
voxel_neighborhood GetVoxelNeighborhood(world* World, vec3i P);

void ResetPlayer(world* World);

bool Initialize(world* World);

void HandleInput(world* World, game_io* IO);
void UpdateWorld(game_state* Game, world* World, game_io* IO);
void World_Render(world* World, renderer_frame_params* Frame);

bool RayCast(world* World, vec3 P, vec3 V, f32 tMax, vec3i* OutP, direction* OutDir);

vec3 MoveEntityBy(world* World, entity* Entity, aabb AABB, vec3 dP);