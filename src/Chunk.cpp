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

                vec3 P = vec3{ x + ChunkP.x, y + ChunkP.y, (f32)z };

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

                // Generate caves
                constexpr f32 CaveScale = 1.0f / 16.0f;
                f32 CaveSample = Perlin3_Octave(&GameState->Perlin3, CaveScale*P, 1, 0.5f, 2.0f);
#if 0
                CaveSample = Abs(CaveSample);
                if (CaveSample < 0.01f)
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }
                else
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_GROUND;
                }
#else
                if (CaveSample < -0.5f && ((z < 80) || ((s32)z < Height)))
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }
#endif
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
                        // Delta in the direction of the surface normal
                        vec3i NormalDelta = GlobalDirections[Direction];

                        bool IsOccluded = false;
                        vec3i WorldVoxelP = vec3i{(s32)x, (s32)y, (s32)z} + vec3i{Chunk->P.x * CHUNK_DIM_X, Chunk->P.y * CHUNK_DIM_Y, 0 };
                        vec3i NeighborP = WorldVoxelP + NormalDelta;
                        u16 NeighborType = Game_GetVoxelType(GameState, NeighborP);
                        const voxel_desc* NeighborDesc = &VoxelDescs[NeighborType];

                        if ((NeighborDesc->Flags & VOXEL_FLAGS_NO_MESH) || (NeighborDesc->Flags & VOXEL_FLAGS_TRANSPARENT))
                        {
                            for (u32 i = 0; i < 6; i++)
                            {
                                vertex CubeVertex = Cube[Direction*DIRECTION_Count + i];

                                // DeltaP in the plane of the normal to check neighboring voxels for ambient occlusion
                                vec3i PlaneDeltaP[2];
                                switch (Direction)
                                {
                                    case DIRECTION_POS_X:
                                    case DIRECTION_NEG_X:
                                        PlaneDeltaP[0] = { 0, 2 * ((s32)CubeVertex.P.y) - 1, 0 };
                                        PlaneDeltaP[1] = { 0, 0, 2 * ((s32)CubeVertex.P.z) - 1 };
                                        break;
                                    case DIRECTION_POS_Y:
                                    case DIRECTION_NEG_Y:
                                        PlaneDeltaP[0] = { 2 * ((s32)CubeVertex.P.x) - 1, 0, 0 };
                                        PlaneDeltaP[1] = { 0, 0, 2 * ((s32)CubeVertex.P.z) - 1 };
                                        break;
                                    case DIRECTION_POS_Z:
                                    case DIRECTION_NEG_Z:
                                        PlaneDeltaP[0] = { 2 * ((s32)CubeVertex.P.x) - 1, 0, 0, };
                                        PlaneDeltaP[1] = { 0, 2 * ((s32)CubeVertex.P.y) - 1, 0 };
                                        break;
                                }

                                // Calculate ambient occlusion
                                // TODO: there's gotta be a smarter way to do this
                                u32 AO = 0;
                                {
                                    u32 bSideAO[2];
                                    for (u32 j = 0; j < 2; j++)
                                    {
                                        vec3i DeltaP = PlaneDeltaP[j] + NormalDelta;
                                        u16 AONeighborType = Game_GetVoxelType(GameState, WorldVoxelP + DeltaP);
                                        const voxel_desc* AONeighborDesc = &VoxelDescs[AONeighborType];
                                        if (!(AONeighborDesc->Flags & VOXEL_FLAGS_NO_MESH) && !(AONeighborDesc->Flags & VOXEL_FLAGS_TRANSPARENT))
                                        {
                                            bSideAO[j] = 1;
                                        }
                                        else
                                        {
                                            bSideAO[j] = 0;
                                        }
                                    }

                                    u32 bCornerAO = 0;
                                    {
                                        vec3i DeltaP = PlaneDeltaP[0] + PlaneDeltaP[1] + NormalDelta;
                                        u16 AONeighborType = Game_GetVoxelType(GameState, WorldVoxelP + DeltaP);
                                        const voxel_desc* AONeighborDesc = &VoxelDescs[AONeighborType];
                                        if (!(AONeighborDesc->Flags & VOXEL_FLAGS_NO_MESH) && !(AONeighborDesc->Flags & VOXEL_FLAGS_TRANSPARENT))
                                        {
                                            bCornerAO = 1;
                                        }
                                    }

                                    if (bSideAO[0] || bSideAO[1])
                                    {
                                        AO = 1 + bSideAO[0] + bSideAO[1];
                                    }
                                    else if (bCornerAO)
                                    {
                                        AO = 1;
                                    }
                                }
                                terrain_vertex Vertex = 
                                {
                                    .P = CubeVertex.P + VoxelP,
                                    .TexCoord = PackTexCoord((u32)CubeVertex.UVW.x, (u32)CubeVertex.UVW.y, (u32)Desc->FaceTextureIndices[Direction], AO),
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