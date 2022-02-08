#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <Renderer.hpp>
#include <Camera.hpp>

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

struct debug_visualizer
{
    static constexpr u64 BufferSize = 4*1024*1024;

    VkDeviceMemory Memory;
    VkBuffer Buffer;
    void* Data;

    u32 MaxVertexCount;
    vertex* VertexData;

    u32 CurrentFrameIndex;
    struct
    {
        u32 Begin;
        u32 End;
    } PerFrame[2];
};

struct game_input
{
    vec2 MouseDelta;
    
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

    camera Camera;

    perlin2 Perlin;

    static constexpr u32 ChunkSqrtDim = 256;
    static constexpr u32 MaxChunkCount = ChunkSqrtDim*ChunkSqrtDim;

    u32 ChunkCount;
    chunk* Chunks;
    chunk_data* ChunkData;

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

    debug_visualizer DebugVisualizer;
};

bool Game_Initialize(game_state* GameState);
void Game_UpdateAndRender(
    game_state* GameState,
    game_input* Input, 
    f32 DeltaTime);
