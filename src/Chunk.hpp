#pragma once
#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <RendererCommon.hpp>

// TODO: remove stl
#include <vector>

constexpr s32 CHUNK_DIM_X = 16;
constexpr s32 CHUNK_DIM_Y = 16;
constexpr s32 CHUNK_DIM_Z = 256;

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
    { VOXEL_FLAGS_NONE,   { 0, 0, 0, 0, 1, 2 } },
};
static constexpr u32 VoxelDescCount = CountOf(VoxelDescs);

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

inline constexpr u32 CardinalOpposite(u32 Cardinal);
inline constexpr u32 CardinalNext(u32 Cardinal);

static const vec2i CardinalDirections[Cardinal_Count] =
{
    { +1,  0 },
    {  0, +1 },
    { -1,  0 },
    {  0, -1 },
};

enum chunk_state_flags : u32
{
    CHUNK_STATE_NONE          = 0,
    CHUNK_STATE_GENERATED_BIT = (1 << 0),
    CHUNK_STATE_MESHED_BIT    = (1 << 1),
    CHUNK_STATE_UPLOADED_BIT  = (1 << 2),
};

struct chunk_data
{
    u16 Voxels[CHUNK_DIM_Z][CHUNK_DIM_Y][CHUNK_DIM_X];
};

struct chunk 
{
    vec2i P;
    u32 Flags;

    chunk* Neighbors[Cardinal_Count];
    chunk_data* Data;

    u32 AllocationIndex; // in VB
};

static u16 Chunk_GetVoxelType(const chunk* Chunk, s32 x, s32 y, s32 z);
static bool Chunk_SetVoxelType(chunk* Chunk, u16 Type, s32 x, s32 y, s32 z);

static bool Chunk_RayCast(
    const chunk* Chunk, 
    vec3 P, vec3 V, 
    f32 Max, 
    vec3i* OutP, int* OutDir);

static void Chunk_Generate(const perlin2* Perlin, chunk* Chunk);
static std::vector<vertex> Chunk_Mesh(const chunk* Chunk);

/* Implentations */
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
