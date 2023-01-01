#pragma once
#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <Renderer/RendererCommon.hpp>

#include <Memory.hpp>

struct world;

constexpr s32 CHUNK_DIM_XY = 16;
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
constexpr u16 VOXEL_STONE = 2;
constexpr u16 VOXEL_COAL = 3;
constexpr u16 VOXEL_IRON = 4;

enum voxel_flags : u32
{
    VOXEL_FLAGS_NONE        = 0,
    VOXEL_FLAGS_NO_MESH     = (1 << 0),
    VOXEL_FLAGS_TRANSPARENT = (1 << 2),
    VOXEL_FLAGS_SOLID       = (1 << 3),
};

struct voxel_desc
{
    u32 Flags;
    u32 FaceTextureIndices[DIRECTION_Count];
};

static const voxel_desc VoxelDescs[] = 
{
    { VOXEL_FLAGS_NO_MESH|VOXEL_FLAGS_TRANSPARENT, { } },
    { VOXEL_FLAGS_SOLID,    { 0, 0, 0, 0, 1, 2 } },
    { VOXEL_FLAGS_SOLID,    { 3, 3, 3, 3, 3, 3 } },
    { VOXEL_FLAGS_SOLID,    { 4, 4, 4, 4, 4, 4 } },
    { VOXEL_FLAGS_SOLID,    { 5, 5, 5, 5, 5, 5 } },
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
    u16 Voxels[CHUNK_DIM_Z][CHUNK_DIM_XY][CHUNK_DIM_XY];
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

struct chunk_render_data
{
    vec2i P;
    u32 AllocationIndex;
    u64* LastRenderedInFrameIndex;
};

struct voxel_neighborhood
{
    u16 VoxelTypes[27];

    u32 IndexFromP(vec3i P) const
    {
        vec3i Index3 = P + vec3i{ 1, 1, 1 };
        u32 Index = 9*Index3.z + 3*Index3.y + Index3.x;
        return Index;
    };

    u16& GetVoxel(vec3i P) { return VoxelTypes[IndexFromP(P)]; };
    const u16& GetVoxel(vec3i P) const { return VoxelTypes[IndexFromP(P)]; };
};

struct chunk_mesh
{
    u32 VertexCount;
    terrain_vertex* VertexData;
};

static void Chunk_Generate(chunk* Chunk, world* World);
static chunk_mesh Chunk_BuildMesh(const chunk* Chunk, world* World, memory_arena* Arena);

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
