#include "Chunk.hpp"

#include <Game.hpp>

static void Chunk_Generate(chunk* Chunk, world* World)
{
    TIMED_FUNCTION();

    assert(Chunk);
    assert(Chunk->Data);

    vec2 ChunkP = { (f32)Chunk->P.x, (f32)Chunk->P.y };
    for (u32 y = 0; y < CHUNK_DIM_Y; y++)
    {
        for (u32 x = 0; x < CHUNK_DIM_X; x++)
        {
            constexpr f32 TerrainBaseFrequency = 1.0f / 64.0f;
            constexpr f32 TerrainBaseScale = 32.0f;
            constexpr u32 TerrainBaseHeight = 80;

            vec2 TerrainP = TerrainBaseFrequency * (vec2{ (f32)x, (f32)y } + ChunkP);

            f32 TerrainSample = Perlin2_Octave(&World->Perlin2, TerrainP, 8, 0.5f, 2.0f);
            TerrainSample = 0.5f * (TerrainSample + 1.0f);
            TerrainSample = Fade3(TerrainSample*TerrainSample);
            s32 Height = (s32)Round(TerrainBaseScale * TerrainSample) + TerrainBaseHeight;

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
                f32 OreSample = Perlin3_Octave(&World->Perlin3, OreScale*P, 3, 0.5f, 2.0f);
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
                f32 CaveSample = Perlin3_Octave(&World->Perlin3, CaveScale*P, 1, 0.5f, 2.0f);
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
                if (CaveSample < -0.5f && ((z < (TerrainBaseHeight + (s32)TerrainBaseScale)) || ((s32)z < Height)))
                {
                    Chunk->Data->Voxels[z][y][x] = VOXEL_AIR;
                }
#endif
            }
        }
    }
}

static chunk_mesh Chunk_BuildMesh(const chunk* Chunk, world* World, memory_arena* Arena)
{
    TIMED_FUNCTION();

    chunk_mesh Mesh = {};

    assert(Chunk);
    assert(Chunk->Data);

    constexpr u32 VoxelFaceCount = 6*2;
    constexpr u32 VertexCountPerVoxel = VoxelFaceCount * 3;
    constexpr u32 MaxVertexCount = VertexCountPerVoxel * CHUNK_DIM_X * CHUNK_DIM_Y * CHUNK_DIM_Z;
    // Allocate the theoretical maximum
    Mesh.VertexData = PushArray<terrain_vertex>(Arena, MaxVertexCount);

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

                    vec3i WorldVoxelP = vec3i{(s32)x, (s32)y, (s32)z} + vec3i{Chunk->P.x, Chunk->P.y, 0 };
                    voxel_neighborhood Neighborhood = World_GetVoxelNeighborhood(World, WorldVoxelP);

                    for (u32 Direction = DIRECTION_First; Direction < DIRECTION_Count; Direction++)
                    {
                        // Delta in the direction of the surface normal
                        vec3i NormalDelta = GlobalDirections[Direction];

                        bool IsOccluded = false;
                        u16 NeighborType = Neighborhood.GetVoxel(NormalDelta);
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
                                u32 AO = 0;
                                {
                                    u32 bSideAO[2];
                                    for (u32 j = 0; j < 2; j++)
                                    {
                                        vec3i DeltaP = PlaneDeltaP[j] + NormalDelta;
                                        u16 AONeighborType = Neighborhood.GetVoxel(DeltaP);
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
                                        u16 AONeighborType = Neighborhood.GetVoxel(DeltaP);
                                        const voxel_desc* AONeighborDesc = &VoxelDescs[AONeighborType];
                                        if (!(AONeighborDesc->Flags & VOXEL_FLAGS_NO_MESH) && !(AONeighborDesc->Flags & VOXEL_FLAGS_TRANSPARENT))
                                        {
                                            bCornerAO = 1;
                                        }
                                    }

                                    if (bSideAO[0] && bSideAO[1])
                                    {
                                        AO = 3;
                                    }
                                    else
                                    {
                                        AO = bSideAO[0] + bSideAO[1] + bCornerAO;
                                    }
                                }

                                Mesh.VertexData[Mesh.VertexCount++] = 
                                {
                                    .P = PackPosition(CubeVertex.P + VoxelP),
                                    .TexCoord = PackTexCoord((u32)CubeVertex.UVW.x, (u32)CubeVertex.UVW.y, (u32)Desc->FaceTextureIndices[Direction], AO),
                                };
                            }
                        }
                    }
                }
            }
        }
    }

    return(Mesh);
}