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
            constexpr f32 TerrainScale = 1.0f / 32.0f;

            vec2 ChunkP = { (f32)Chunk->P.x * CHUNK_DIM_X, (f32)Chunk->P.y * CHUNK_DIM_Y };
            vec2 TerrainP = TerrainScale * (vec2{ (f32)x, (f32)y } + ChunkP);

            f32 TerrainSample = 16.0f * Perlin2_Octave(&GameState->Perlin2, TerrainP, 2, 0.5f, 1.5f);
            s32 Height = (s32)Round(TerrainSample) + 80;

            for (u32 z = 0; z < CHUNK_DIM_Z; z++)
            {
                // Generate base terrain
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

                // Generate caves
                constexpr f32 CaveScale = 1.0f / 8.0f;
                vec3 P = vec3{ x + ChunkP.x, y + ChunkP.y, (f32)z };
                f32 CaveSample = Perlin3_Octave(&GameState->Perlin3, CaveScale*P, 3, 0.5f, 2.0f);
                if (CaveSample < -0.25f && ((z < 80) || ((s32)z < Height)))
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }

                // Generate ores
                constexpr f32 OreScale = 1.0f / 8.0f;
                f32 OreSample = Perlin3_Octave(&GameState->Perlin3, OreScale*P, 3, 0.5f, 2.0f);
                // Only replace stone with ores
                if (Chunk->Data->Voxels[z][y][x] == VOXEL_STONE)
                {
                    if (OreSample > 0.75f)
                    {
                        Chunk->Data->Voxels[z][y][x] = VOXEL_COAL;
                    }
                    else if (OreSample < -0.75f)
                    {
                        Chunk->Data->Voxels[z][y][x] = VOXEL_IRON;
                    }
                }
            }
        }
    }
}

static std::vector<terrain_vertex> Chunk_BuildMesh(const chunk* Chunk, game_state* GameState)
{
    TIMED_FUNCTION();

    assert(Chunk);
    assert(Chunk->Data);

    std::vector<terrain_vertex> VertexList;

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
                                vertex CubeVertex = Cube[Direction*DIRECTION_Count + i];
                                terrain_vertex Vertex = 
                                {
                                    .P = CubeVertex.P + VoxelP,
                                    .TexCoord = PackTexCoord((u32)CubeVertex.UVW.x, (u32)CubeVertex.UVW.y, (u32)Desc->FaceTextureIndices[Direction]),
                                };
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