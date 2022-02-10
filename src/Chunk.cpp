#include "Chunk.hpp"


static u16 Chunk_GetVoxelType(const chunk* Chunk, s32 x, s32 y, s32 z)
{
    u16 Result = VOXEL_AIR;

    // TODO: we may just want to return some special block if the coordinates point to some chunk that's not loaded/generated
    while (x < 0)
    {
        assert(Chunk->Neighbors[West]);
        Chunk = Chunk->Neighbors[West];
        x += CHUNK_DIM_X;
    }
    while (x >= CHUNK_DIM_X)
    {
        assert(Chunk->Neighbors[East]);
        Chunk = Chunk->Neighbors[East];
        x -= CHUNK_DIM_X;
    }
    while (y < 0)
    {
        assert(Chunk->Neighbors[South]);
        Chunk = Chunk->Neighbors[South];
        y += CHUNK_DIM_Y;
    }
    while (y >= CHUNK_DIM_Y)
    {
        assert(Chunk->Neighbors[North]);
        Chunk = Chunk->Neighbors[North];
        y -= CHUNK_DIM_Y;
    }

    if (0 <= z && z <= CHUNK_DIM_Z)
    {
        if (Chunk->Flags & CHUNK_STATE_GENERATED_BIT)
        {
            assert(Chunk->Data);
            Result = Chunk->Data->Voxels[z][y][x];
        }
    }
    return Result;
}

static void Chunk_Generate(const perlin2* Perlin, chunk* Chunk)
{
    TIMED_FUNCTION();

    assert(Chunk);
    assert(Chunk->Data);

    for (u32 z = 0; z < CHUNK_DIM_Z; z++)
    {
        for (u32 y = 0; y < CHUNK_DIM_Y; y++)
        {
            for (u32 x = 0; x < CHUNK_DIM_X; x++)
            {
                constexpr f32 Scale = 1.0f / 32.0f;
                vec2 ChunkP = { (f32)Chunk->P.x * CHUNK_DIM_X, (f32)Chunk->P.y * CHUNK_DIM_Y };
                vec2 P = Scale * (vec2{ (f32)x, (f32)y } + ChunkP);

                f32 Sample = 32.0f * Perlin2_Octave(Perlin, P, 2);
                s32 Height = (s32)Round(Sample) + 80;

                if ((s32)z > Height)
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }
                else
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_GROUND;
                }
            }
        }
    }
}

static std::vector<vertex> Chunk_Mesh(const chunk* Chunk)
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

                    for (u32 Face = 0; Face < 6; Face++)
                    {
                        bool IsOccluded = false;
                        if (Face == 0)
                        {
                            if (x != CHUNK_DIM_X - 1)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z][y][x + 1] == VOXEL_GROUND);
                            }
                            else 
                            {
                                assert(Chunk->Neighbors[East]);
                                if (Chunk->Neighbors[East])
                                {
                                    IsOccluded = Chunk->Neighbors[East]->Data->Voxels[z][y][0] == VOXEL_GROUND;
                                }
                            }
                        }
                        else if (Face == 1)
                        {
                            if (x != 0)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z][y][x - 1] == VOXEL_GROUND);
                            }
                            else 
                            {
                                assert(Chunk->Neighbors[West]);
                                if (Chunk->Neighbors[West])
                                {
                                    IsOccluded = (Chunk->Neighbors[West]->Data->Voxels[z][y][CHUNK_DIM_X - 1] == VOXEL_GROUND);
                                }
                            }
                        }
                        else if (Face == 2)
                        {
                            if (y != CHUNK_DIM_Y - 1)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z][y + 1][x] == VOXEL_GROUND);
                            }
                            else
                            {
                                assert(Chunk->Neighbors[North]);
                                if (Chunk->Neighbors[North])
                                {
                                    IsOccluded = (Chunk->Neighbors[North]->Data->Voxels[z][0][x] == VOXEL_GROUND);
                                }
                            }
                        }
                        else if (Face == 3)
                        {
                            if (y != 0)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z][y - 1][x] == VOXEL_GROUND);
                            }
                            else
                            {
                                assert(Chunk->Neighbors[South]);
                                if (Chunk->Neighbors[South])
                                {
                                    IsOccluded = (Chunk->Neighbors[South]->Data->Voxels[z][CHUNK_DIM_Y - 1][x] == VOXEL_GROUND);
                                }
                            }
                        }
                        else if (Face == 4)
                        {
                            if (z != CHUNK_DIM_Z - 1)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z + 1][y][x] == VOXEL_GROUND);
                            }
                        }
                        else if (Face == 5)
                        {
                            if (z != 0)
                            {
                                IsOccluded = (Chunk->Data->Voxels[z - 1][y][x] == VOXEL_GROUND);
                            }
                        }

                        if (!IsOccluded)
                        {
                            for (u32 i = 0; i < 6; i++)
                            {
                                vertex Vertex = Cube[Face*6 + i];
                                Vertex.P += VoxelP;
                                Vertex.UVW.z += (f32)Desc->FaceTextureIndices[Face];
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