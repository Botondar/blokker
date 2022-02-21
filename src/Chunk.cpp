#include "Chunk.hpp"

static u16 Chunk_GetVoxelType(const chunk* Chunk, s32 x, s32 y, s32 z)
{
    u16 Result = VOXEL_AIR;

    if (0 <= z && z <= CHUNK_DIM_Z)
    {
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
    
        if (Chunk->Flags & CHUNK_STATE_GENERATED_BIT)
        {
            assert(Chunk->Data);
            Result = Chunk->Data->Voxels[z][y][x];
        }
    }
    return Result;
}

static bool Chunk_SetVoxelType(chunk* Chunk, u16 Type, s32 x, s32 y, s32 z)
{
    bool Result = false;

    if (0 <= z && z <= CHUNK_DIM_Z)
    {
        while (x < 0 && Chunk)
        {
            Chunk = Chunk->Neighbors[West];
            x += CHUNK_DIM_X;
        }
        while (x >= CHUNK_DIM_X && Chunk)
        {
            Chunk = Chunk->Neighbors[East];
            x -= CHUNK_DIM_X;
        }
        while (y < 0 && Chunk)
        {
            Chunk = Chunk->Neighbors[South];
            y += CHUNK_DIM_Y;
        }
        while (y >= CHUNK_DIM_Y && Chunk)
        {
            Chunk = Chunk->Neighbors[North];
            y -= CHUNK_DIM_Y;
        }

        if (Chunk)
        {
            Chunk->Data->Voxels[z][y][x] = Type;
            if (Chunk->Flags & CHUNK_STATE_MESHED_BIT)
            {
                Chunk->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
            }
            // Remesh neighbors if we've modified a block on the edge of the chunk
            if(x == 0)
            {
                if (Chunk->Neighbors[West] && (Chunk->Neighbors[West]->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Chunk->Neighbors[West]->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (x == CHUNK_DIM_X - 1)
            {
                if (Chunk->Neighbors[East] && (Chunk->Neighbors[East]->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Chunk->Neighbors[East]->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            if(y == 0)
            {
                if (Chunk->Neighbors[South] && (Chunk->Neighbors[South]->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Chunk->Neighbors[South]->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (y == CHUNK_DIM_Y - 1)
            {
                if (Chunk->Neighbors[North] && (Chunk->Neighbors[North]->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Chunk->Neighbors[North]->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }


            Result = true;
        }
    }

    return Result;
}

static bool Chunk_RayCast(
    const chunk* Chunk, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, int* OutDir)
{
    TIMED_FUNCTION();

    bool Result = false;

    auto RayPlaneIntersection = [](vec3 P, vec3 v, vec4 Plane, f32 tMin, f32 tMax, f32* tOut) -> bool
    {
        bool Result = false;

        vec3 N = { Plane.x, Plane.y, Plane.z };
        f32 NdotV = Dot(N, v);

        if (NdotV != 0.0f)
        {
            f32 t = (-Dot(P, N) - Plane.w) / NdotV;
            if ((tMin <= t) && (t < tMax))
            {
                *tOut = t;
                Result = true;
            }
        }

        return Result;
    };

    auto RayAABBIntersection = [RayPlaneIntersection](vec3 P, vec3 v, aabb Box, f32 tMin, f32 tMax, f32* tOut, int* OutDir) -> bool
    {
        bool Result = false;

        bool AnyIntersection = false;
        f32 tIntersection = -1.0f;
        int IntersectionDir = -1;
        for (int i = 0; i < 6; i++)
        {
            switch (i)
            {
                case 0: 
                {
                    vec4 Plane = { +1.0f, 0.0f, 0.0f, -Box.Max.x };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.y <= P0.y) && (P0.y <= Box.Max.y) &&
                            (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
                case 1: 
                {
                    vec4 Plane = { -1.0f, 0.0f, 0.0f, +Box.Min.x };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.y <= P0.y) && (P0.y <= Box.Max.y) &&
                            (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
                case 2: 
                {
                    vec4 Plane = { 0.0f, +1.0f, 0.0f, -Box.Max.y };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                            (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
                case 3: 
                {
                    vec4 Plane = { 0.0f, -1.0f, 0.0f, +Box.Min.y };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                            (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
                case 4:
                {
                    vec4 Plane = { 0.0f, 0.0f, +1.0f, -Box.Max.z };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                            (Box.Min.y <= P0.y) && (P0.y <= Box.Max.y))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
                case 5:
                {
                    vec4 Plane = { 0.0f, 0.0f, -1.0f, +Box.Min.z };
                    f32 t;
                    if (RayPlaneIntersection(P, v, Plane, tMin, tMax, &t))
                    {
                        vec3 P0 = P + t*v;
                        if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                            (Box.Min.y <= P0.y) && (P0.y <= Box.Max.y))
                        {
                            AnyIntersection = true;
                            tIntersection = t;
                            IntersectionDir = i;
                            tMax = Min(tMax, t);
                        }
                    }
                } break;
            }
        }

        if (AnyIntersection)
        {
            *tOut = tIntersection;
            *OutDir = IntersectionDir;
            Result = true;
        }

        return Result;
    };

    vec3 RelP = P - (vec3)(vec3i{Chunk->P.x, Chunk->P.y, 0 } * vec3i{ CHUNK_DIM_X, CHUNK_DIM_Y, 1 });

    V = SafeNormalize(V);
    aabb SearchBox = MakeAABB(Floor(RelP), Floor(RelP + tMax * V));

    vec3i StartP = (vec3i)SearchBox.Min;
    vec3i EndP = (vec3i)SearchBox.Max;

    bool AnyHit = false;
    vec3i HitP = {};
    int HitDirection = -1;
    for (s32 z = StartP.z; z <= EndP.z; z++)
    {
        for (s32 y = StartP.y; y <= EndP.y; y++)
        {
            for (s32 x = StartP.x; x <= EndP.x; x++)
            {
                u16 Voxel = Chunk_GetVoxelType(Chunk, x, y, z);
                if (Voxel == VOXEL_GROUND)
                {
                    aabb Box = MakeAABB(vec3{ (f32)x, (f32)y, (f32)z }, vec3{ (f32)(x + 1), (f32)(y + 1), (f32)(z + 1) });

                    f32 tCurrent;
                    int CurrentDir;
                    if (RayAABBIntersection(RelP, V, Box, 0.0f, tMax, &tCurrent, &CurrentDir))
                    {
                        tMax = Min(tMax, tCurrent);
                        HitP = vec3i { x, y, z };
                        HitDirection = CurrentDir;
                        AnyHit = true;
                    }
                }
            }
        }
    }

    if (AnyHit)
    {
        *OutP = HitP;
        *OutDir = HitDirection;
        Result = true;
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