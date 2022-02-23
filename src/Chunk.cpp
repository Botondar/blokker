#include "Chunk.hpp"

#include <Game.hpp>

static void Chunk_Generate(chunk* Chunk, game_state* GameState)
{
    TIMED_FUNCTION();

    assert(Chunk);
    assert(Chunk->Data);

    for (u32 y = 0; y < CHUNK_DIM_Y; y++)
    {
        for (u32 x = 0; x < CHUNK_DIM_X; x++)
        {
            constexpr f32 Scale = 1.0f / 32.0f;

            vec2 ChunkP = { (f32)Chunk->P.x * CHUNK_DIM_X, (f32)Chunk->P.y * CHUNK_DIM_Y };
            vec2 P = Scale * (vec2{ (f32)x, (f32)y } + ChunkP);

            f32 Sample = 16.0f * Perlin2_Octave(&GameState->Perlin, P, 2, 0.5f, 1.5f);
            s32 Height = (s32)Round(Sample) + 80;

            for (u32 z = 0; z < CHUNK_DIM_Z; z++)
            {
                if ((s32)z > Height)
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }
                else if ((s32)z > Height - 3)
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_GROUND;
                }
                else
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_STONE;
                }
            }
        }
    }
}

static std::vector<vertex> Chunk_BuildMesh(const chunk* Chunk, game_state* GameState)
{
    TIMED_FUNCTION();

    assert(Chunk);
    assert(Chunk->Data);

    std::vector<vertex> VertexList;

    for (u32 z = 0; z < CHUNK_DIM_Z; z++)
    {
        for (u32 y = 0; y < CHUNK_DIM_Y; y++)
        {
            for (u32 x = 0; x < CHUNK_DIM_X; x++)
            {
                vec3 VoxelP = vec3{ (f32)x, (f32)y, (f32)z };

                u16 VoxelType = Chunk->Data->Voxels[z][y][x];
                assert(VoxelType < VoxelDescCount);

                const voxel_desc* Desc = VoxelDescs + VoxelType;
                if (Desc->Flags & VOXEL_FLAGS_NO_MESH)
                {
                    continue;
                }
                else
                {
                    static const vertex Cube[] = 
                    {
                        // EAST
                        { { 1.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },

                        // WEST
                        { { 0.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },

                        // NORTH
                        { { 0.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 0.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },

                        // SOUTH
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },

                        // TOP
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },

                        // BOTTOM
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 0.0f, 1.0f, 0.0f, }, { 0.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                    };
                    constexpr u32 CubeVertexCount = CountOf(Cube);

                    for (u32 Direction = DIRECTION_First; Direction < DIRECTION_Count; Direction++)
                    {
                        bool IsOccluded = false;
                        vec3i NeighborP = vec3i{(s32)x, (s32)y, (s32)z} + vec3i{Chunk->P.x * CHUNK_DIM_X, Chunk->P.y * CHUNK_DIM_Y, 0 } + GlobalDirections[Direction];
                        u16 NeighborType = Game_GetVoxelType(GameState, NeighborP);
                        const voxel_desc* NeighborDesc = &VoxelDescs[NeighborType];

                        if ((NeighborDesc->Flags & VOXEL_FLAGS_NO_MESH) || (NeighborDesc->Flags & VOXEL_FLAGS_TRANSPARENT))
                        {
                            for (u32 i = 0; i < 6; i++)
                            {
                                vertex Vertex = Cube[Direction*DIRECTION_Count + i];
                                Vertex.P += VoxelP;
                                Vertex.UVW.z += (f32)Desc->FaceTextureIndices[Direction];
                                VertexList.push_back(Vertex);
                            }
                        }
                    }
                }
            }
        }
    }

    return VertexList;
}