#pragma once
#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <RendererCommon.hpp>

// TODO: remove stl
#include <vector>

struct game_state;

constexpr s32 CHUNK_DIM_X = 16;
constexpr s32 CHUNK_DIM_Y = 16;
constexpr s32 CHUNK_DIM_Z = 256;

enum axis : u32
{
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2,
    AXIS_Count,
    AXIS_First = AXIS_X,
};

enum direction : u32
{
    DIRECTION_POS_X = 0,
    DIRECTION_NEG_X = 1,
    DIRECTION_POS_Y = 2,
    DIRECTION_NEG_Y = 3,
    DIRECTION_POS_Z = 4,
    DIRECTION_NEG_Z = 5,

    DIRECTION_Count,
    DIRECTION_First = DIRECTION_POS_X,
};

static const vec3i GlobalDirections[DIRECTION_Count] = 
{
    { +1, 0, 0 },
    { -1, 0, 0 },
    { 0, +1, 0 },
    { 0, -1, 0 },
    { 0, 0, +1 },
    { 0, 0, -1 },
};

// TODO: namespace/enum class + operator overloads
enum cardinal : u32
{
    East = 0,
    North,
    West,
    South,

    Cardinal_Count,
    Cardinal_First = East,
};

static const vec2i CardinalDirections[Cardinal_Count] =
{
    { +1,  0 },
    {  0, +1 },
    { -1,  0 },
    {  0, -1 },
};

inline constexpr u32 CardinalOpposite(u32 Cardinal);
inline constexpr u32 CardinalNext(u32 Cardinal);

constexpr u16 VOXEL_AIR = 0;
constexpr u16 VOXEL_GROUND = 1;

enum voxel_flags : u32
{
    VOXEL_FLAGS_NONE = 0,
    VOXEL_FLAGS_NO_MESH = 1,
};

struct voxel_desc
{
    voxel_flags Flags;
    u32 FaceTextureIndices[6];
};

static voxel_desc VoxelDescs[] = 
{
    { VOXEL_FLAGS_NO_MESH, { } },
    { VOXEL_FLAGS_NONE,    { 0, 0, 0, 0, 1, 2 } },
};
static constexpr u32 VoxelDescCount = CountOf(VoxelDescs);

enum chunk_state_flags : u32
{
    CHUNK_STATE_NONE          = 0,
    CHUNK_STATE_GENERATED_BIT = (1 << 0),
    CHUNK_STATE_MESHED_BIT    = (1 << 1),
    CHUNK_STATE_UPLOADED_BIT  = (1 << 2),
    CHUNK_STATE_MESH_DIRTY_BIT = (1 << 3),
};

struct chunk_data
{
    u16 Voxels[CHUNK_DIM_Z][CHUNK_DIM_Y][CHUNK_DIM_X];
};

struct chunk 
{
    vec2i P;
    u32 Flags;

    chunk_data* Data;

    u32 AllocationIndex; // in VB
    u64 LastRenderedInFrameIndex;

    u32 OldAllocationIndex;
    u64 OldAllocationLastRenderedInFrameIndex;
};

static void Chunk_Generate(chunk* Chunk, game_state* GameState);
static std::vector<vertex> Chunk_Mesh(const chunk* Chunk, game_state* GameState);

/* Implementations */
inline constexpr u32 CardinalOpposite(u32 Cardinal)
{
    u32 Result = (u32)((Cardinal + 2) % Cardinal_Count);
    return Result;
}

inline constexpr u32 CardinalNext(u32 Cardinal)
{
    u32 Result = (Cardinal + 1) % Cardinal_Count;
    return Result;
}
