#include "World.hpp"

#include <Game.hpp>
#include <Platform.hpp>
#include <Renderer/Renderer.hpp>

#include <Profiler.hpp>

//
// Internal functions
//
static u32 World_HashChunkP(const world* World, vec2i P, vec2i* Coords = nullptr);
static void World_LoadChunks(world* World);
static chunk* World_ReserveChunk(world* World, vec2i P);
static chunk* World_FindPlayerChunk(world* World);

//
// Implementations
//

void map_view::ResetAll(world* World)
{
    CurrentP = { World->Player.P.x, World->Player.P.y };
    CurrentPitch = ToRadians(-60.0f);
    ZoomCurrent = 10.0f;
    CurrentYaw = ToRadians(45.0f);

    Reset(World);
}

void map_view::Reset(world* World) 
{
    ZoomTarget = 1.0f;
    TargetP = { World->Player.P.x, World->Player.P.y };
    TargetPitch = ToRadians(-60.0f);
    TargetYaw = ToRadians(45.0f);
}

mat2 map_view::GetAxesXY() const
{
    f32 SinYaw = Sin(CurrentYaw);
    f32 CosYaw = Cos(CurrentYaw);

    mat2 Result = Mat2(
        CosYaw, -SinYaw,
        SinYaw, CosYaw);
    return Result;
}

void World_ResetPlayer(world* World)
{
    player* Player = &World->Player;
    Player->Velocity = {};
    vec3i PlayerP = (vec3i)Floor(Player->P);
    Player->P.x = PlayerP.x + 0.5f;
    Player->P.y = PlayerP.y + 0.5f;
    
    vec3i RelP;
    chunk* Chunk = World_GetChunkFromP(World, PlayerP, &RelP);
    if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
    {
        for (s32 z = CHUNK_DIM_Z - 1; z >= 0; z--)
        {
            u16 VoxelType = Chunk->Data->Voxels[z][RelP.y][RelP.x];
            const voxel_desc* Desc = &VoxelDescs[VoxelType];
            if (Desc->Flags & VOXEL_FLAGS_SOLID)
            {
                Player->P.z = z + Player->EyeHeight;
                break;
            }
        }
    }
    else
    {
        assert(!"Invalid player chunk");
    }
}

//
// World
//

static u32 World_HashChunkP(const world* World, vec2i P, vec2i* Coords /*= nullptr*/)
{
    s32 ix = Modulo(P.x, World->MaxChunkCountSqrt);
    s32 iy = Modulo(P.y, World->MaxChunkCountSqrt);
    assert((ix >= 0) && (iy >= 0));

    u32 x = (u32)ix;
    u32 y = (u32)iy;

    u32 Result = x + y * World->MaxChunkCountSqrt;
    if (Coords)
    {
        Coords->x = ix;
        Coords->y = iy;
    }

    return Result;
}

chunk* World_GetChunkFromP(world* World, vec2i P)
{
    chunk* Result = nullptr;

    u32 Index = World_HashChunkP(World, P);
    chunk* Chunk = World->Chunks + Index;
    if (Chunk->P == P)
    {
        Result = Chunk;
    }

    return Result;
}

chunk* World_GetChunkFromP(world* World, vec3i P, vec3i* RelP)
{
    chunk* Result = nullptr;

    vec2i ChunkP = { FloorDiv(P.x, CHUNK_DIM_X), FloorDiv(P.y, CHUNK_DIM_Y) };
    chunk* Chunk = World_GetChunkFromP(World, ChunkP);
    if (Chunk)
    {
        if (RelP)
        {
            *RelP = P - vec3i{ ChunkP.x * CHUNK_DIM_X, ChunkP.y * CHUNK_DIM_Y, 0 };
        }
        Result = Chunk;
    }

    return Result;
}

u16 World_GetVoxelType(world* World, vec3i P)
{
    u16 Result = VOXEL_AIR;

    if (0 <= P.z && P.z < CHUNK_DIM_Z)
    {
        vec3i RelP = {};
        chunk* Chunk = World_GetChunkFromP(World, P, &RelP);

        if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
        {
            assert(Chunk->Data);
            Result = Chunk->Data->Voxels[RelP.z][RelP.y][RelP.x];
        }
        else
        {
            DebugPrint("WARNING: Invalid voxel read\n");
        }
    }
    return Result;
}

bool World_SetVoxelType(world* World, vec3i P, u16 Type)
{
    bool Result = false;

    vec3i RelP = {};
    chunk* Chunk = World_GetChunkFromP(World, P, &RelP);

    if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
    {
        assert(Chunk->Data);
        if ((0 <= RelP.z) && (RelP.z < CHUNK_DIM_Z))
        {
            Chunk->Data->Voxels[RelP.z][RelP.y][RelP.x] = Type;
            Chunk->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;

            if (RelP.x == 0)
            {
                chunk* Neighbor = World_GetChunkFromP(World, Chunk->P + CardinalDirections[West]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.x == CHUNK_DIM_X - 1)
            {
                chunk* Neighbor = World_GetChunkFromP(World, Chunk->P + CardinalDirections[East]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            if (RelP.y == 0)
            {
                chunk* Neighbor = World_GetChunkFromP(World, Chunk->P + CardinalDirections[South]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.y == CHUNK_DIM_Y - 1)
            {
                chunk* Neighbor = World_GetChunkFromP(World, Chunk->P + CardinalDirections[North]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }

            Result = true;
        }
    }

    return Result;
}

voxel_neighborhood World_GetVoxelNeighborhood(world* World, vec3i P)
{
    voxel_neighborhood Result = {};

    for (s32 z = -1; z <= 1; z++)
    {
        for (s32 y = -1; y <= 1; y++)
        {
            for (s32 x = -1; x <= 1; x++)
            {
                Result.GetVoxel(vec3i{ x, y, z }) = World_GetVoxelType(World, P + vec3i{ x, y, z });
            }
        }
    }

    return Result;
}


static chunk* World_ReserveChunk(world* World, vec2i P)
{
    chunk* Result = nullptr;
    u32 Index = World_HashChunkP(World, P);
    assert(Index < World->MaxChunkCount);

    Result = World->Chunks + Index;
    if (Result->P != P)
    {
        if (Result->Flags & CHUNK_STATE_GENERATED_BIT)
        {
            DebugPrint("WARNING: Evicting chunk { %d, %d }\n", Result->P.x, Result->P.y);

            if (Result->OldAllocationIndex != INVALID_INDEX_U32)
            {
                if (Result->OldAllocationIndex < World->FrameIndex - 1)
                {
                    VB_Free(&World->Renderer->VB, Result->OldAllocationIndex);
                }
                else
                {
                    // TODO(boti): the vertex memory will need to be kept alive until the GPU is no longer using it
                    assert(!"Unimplemented code path");
                }
            }

            // Preserve the current allocation, it will get freed once the GPU is no longer using it
            Result->OldAllocationIndex = Result->AllocationIndex;
            Result->OldAllocationLastRenderedInFrameIndex = Result->LastRenderedInFrameIndex;

            Result->AllocationIndex = INVALID_INDEX_U32;
            Result->LastRenderedInFrameIndex = 0;
        }

        Result->P = P;
        Result->Flags = 0;
    }

    return Result;
}

static chunk* World_FindPlayerChunk(world* World)
{
    TIMED_FUNCTION();
    chunk* Result = nullptr;
    vec2i PlayerChunkP = (vec2i)Floor(vec2{ World->Player.P.x / CHUNK_DIM_X, World->Player.P.y / CHUNK_DIM_Y });

    u32 Index = World_HashChunkP(World, PlayerChunkP);
    chunk* Chunk = World->Chunks + Index;
    if (Chunk->P == PlayerChunkP)
    {
        Result = Chunk;
    }
    return Result;
}

bool World_RayCast(
    world* World, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, direction* OutDir)
{
    TIMED_FUNCTION();

    bool Result = false;

    //vec3 RelP = P - (vec3)(vec3i{Chunk->P.x, Chunk->P.y, 0 } * vec3i{ CHUNK_DIM_X, CHUNK_DIM_Y, 1 });

    V = SafeNormalize(V);
    aabb SearchBox = MakeAABB(Floor(P), Floor(P + tMax * V));

    vec3i StartP = (vec3i)SearchBox.Min;
    vec3i EndP = (vec3i)SearchBox.Max;

    bool AnyHit = false;
    vec3i HitP = {};
    direction HitDirection = DIRECTION_First;
    for (s32 z = StartP.z; z <= EndP.z; z++)
    {
        for (s32 y = StartP.y; y <= EndP.y; y++)
        {
            for (s32 x = StartP.x; x <= EndP.x; x++)
            {
                u16 Voxel = World_GetVoxelType(World, vec3i{x, y, z});
                if (Voxel != VOXEL_AIR)
                {
                    aabb Box = MakeAABB(vec3{ (f32)x, (f32)y, (f32)z }, vec3{ (f32)(x + 1), (f32)(y + 1), (f32)(z + 1) });

                    f32 tCurrent;
                    direction CurrentDir;
                    if (IntersectRayAABB(P, V, Box, 0.0f, tMax, &tCurrent, &CurrentDir))
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

vec3 World_ApplyEntityMovement(world* World, entity* Entity, aabb AABB, vec3 dP)
{
    vec3 Displacement = {};

    // Moves the entity along an axis, checks for collision with the world and resolves those collisions
    auto ApplyMovement = [&World, &Entity, &AABB](vec3 dP, u32 Direction) -> f32
    {
        Entity->P[Direction] += dP[Direction];
        AABB.Min[Direction] += dP[Direction];
        AABB.Max[Direction] += dP[Direction];

        f32 AccumulatedDisplacement = 0.0f;
        bool IsCollision = false;
        do 
        {
            vec3i MinPi = (vec3i)Floor(AABB.Min);
            vec3i MaxPi = (vec3i)Ceil(AABB.Max);

            constexpr u32 AABBStackSize = 64;
            u32 AABBAt = 0;
            aabb AABBStack[AABBStackSize];

            for (s32 z = MinPi.z; z <= MaxPi.z; z++)
            {
                for (s32 y = MinPi.y; y <= MaxPi.y; y++)
                {
                    for (s32 x = MinPi.x; x <= MaxPi.x; x++)
                    {
                        u16 VoxelType = World_GetVoxelType(World, vec3i{x, y, z});
                        const voxel_desc* VoxelDesc = &VoxelDescs[VoxelType];
                        if (VoxelDesc->Flags & VOXEL_FLAGS_SOLID)
                        {
                            assert(AABBAt < AABBStackSize);
                            aabb VoxelAABB = 
                            {
                                .Min = { (f32)x, (f32)y, (f32)z },
                                .Max = { (f32)(x + 1), (f32)(y + 1), (f32)(z + 1) },
                            };
                            AABBStack[AABBAt++] = VoxelAABB;
                        }
                    }
                }
            }

            float Displacement = 0.0f;
            IsCollision = false;
            for (u32 i = 0; i < AABBAt; i++)
            {
                vec3 Overlap;
                int MinCoord;
                if (AABB_Intersect(AABB, AABBStack[i], Overlap, MinCoord))
                {
                    IsCollision = true;
                    if (Abs(Displacement) < Abs(Overlap[Direction]))
                    {
                        Displacement = Overlap[Direction];
                    }
                }
            }

            if (IsCollision)
            {
                constexpr f32 Epsilon = 1e-5f;
                Displacement += Signum(Displacement) * Epsilon;
                Entity->P[Direction] += Displacement;
                AABB.Min[Direction] += Displacement;
                AABB.Max[Direction] += Displacement;

                AccumulatedDisplacement += Displacement;
            }
        } while (IsCollision);
        return AccumulatedDisplacement;
    };

    Displacement.z = ApplyMovement(dP, AXIS_Z);

    if (Abs(dP.x) > Abs(dP.y))
    {
        Displacement.x = ApplyMovement(dP, AXIS_X);
        Displacement.y = ApplyMovement(dP, AXIS_Y);
    }
    else
    {
        Displacement.y = ApplyMovement(dP, AXIS_Y);
        Displacement.x = ApplyMovement(dP, AXIS_X);
    }
    
    return Displacement;
}

// Loads the chunks around the player
void World_LoadChunks(world* World)
{
    TIMED_FUNCTION();

    vec2 PlayerP = (vec2)World->Player.P;
    vec2i PlayerChunkP = (vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_X, (f32)CHUNK_DIM_Y });

    constexpr u32 ImmediateMeshDistance = 1;
    constexpr u32 ImmediateGenerationDistance = ImmediateMeshDistance + 1;
#if BLOKKER_TINY_RENDER_DISTANCE
    constexpr u32 MeshDistance = 3;
#else
    constexpr u32 MeshDistance = 4;
#endif
    constexpr u32 GenerationDistance = MeshDistance + 1;

    // Create a stack that'll hold the chunks that haven't been meshed/generated around the player.
    constexpr u32 StackSize = (2*GenerationDistance + 1)*(2*GenerationDistance + 1);
    u32 StackAt = 0;
    chunk* Stack[StackSize];

    chunk* PlayerChunk = World_FindPlayerChunk(World);
    if (!PlayerChunk)
    {
        PlayerChunk = World_ReserveChunk(World, PlayerChunkP);
        if (PlayerChunk)
        {
            Stack[StackAt++] = PlayerChunk;
            PlayerChunk->P = PlayerChunkP;
        }
    }
    
    if (!(PlayerChunk->Flags & CHUNK_STATE_GENERATED_BIT) ||
        !(PlayerChunk->Flags & CHUNK_STATE_MESHED_BIT) ||
        !(PlayerChunk->Flags & CHUNK_STATE_UPLOADED_BIT) ||
        (PlayerChunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT))
    {
        Stack[StackAt++] = PlayerChunk;
    }

    // Keep track of closest rings around the player that have been fully generated or meshed
    u32 ClosestNotGeneratedDistance = GenerationDistance + 1;
    u32 ClosestNotMeshedDistance = GenerationDistance + 1;

    for (u32 i = 0; i <= GenerationDistance; i++)
    {
        u32 Diameter = 2*i + 1;
        vec2i CurrentP = PlayerChunk->P - vec2i{(s32)i, (s32)i};

        u32 CurrentCardinal = South; // last
        for (u32 j = 0; j < 4; j++)
        {
            CurrentCardinal = CardinalNext(CurrentCardinal);
            for (u32 k = 0; k < Diameter - 1; k++)
            {
                CurrentP = CurrentP + CardinalDirections[CurrentCardinal];
                chunk* Chunk = World_GetChunkFromP(World, CurrentP);
                if (!Chunk)
                {
                    Chunk = World_ReserveChunk(World, CurrentP);
                }

                if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT) ||
                    !(Chunk->Flags & CHUNK_STATE_MESHED_BIT) ||
                    !(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT) ||
                    (Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT))
                {
                    Stack[StackAt++] = Chunk;

                    if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
                    {
                        ClosestNotGeneratedDistance = Min(i, ClosestNotGeneratedDistance);
                    }
                    if (!(Chunk->Flags & CHUNK_STATE_MESHED_BIT))
                    {
                        ClosestNotMeshedDistance = Min(i, ClosestNotMeshedDistance);
                    }
                }
            }
        }
    }

    // Limit the number of chunks that can be processed in a single frame so that we don't hitch
    constexpr u32 ProcessedChunkLimit = 1;
    u32 ProcessedChunkCount = 0;

    
    if ((ClosestNotGeneratedDistance <= ImmediateGenerationDistance) ||
        (ClosestNotMeshedDistance + 1) >= ClosestNotGeneratedDistance)
    {
        // Generate the chunks in the stack
        for (u32 i = 0; i < StackAt; i++)
        {
            chunk* Chunk = Stack[i];

            s32 Distance = ChebyshevDistance(Chunk->P, PlayerChunkP);
            if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
            {
                if ((Distance <= ImmediateGenerationDistance) ||
                    (ProcessedChunkCount < ProcessedChunkLimit))
                {
                    Chunk_Generate(Chunk, World);
                    Chunk->Flags |= CHUNK_STATE_GENERATED_BIT;
                    ProcessedChunkCount++;
                }
            }
        }
    }

    // Mesh the chunks in the stack and upload them to the GPU
    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];
        s32 Distance = ChebyshevDistance(PlayerChunkP, Chunk->P);

        if (!(Chunk->Flags & CHUNK_STATE_MESHED_BIT) && (Distance <= MeshDistance))
        {
            if (((Distance <= ImmediateMeshDistance) || (ProcessedChunkCount < ProcessedChunkLimit)) && 
                ((Distance + 1) < (s32)ClosestNotGeneratedDistance))
            {
                assert(Chunk->Flags & CHUNK_STATE_GENERATED_BIT);
                assert(!(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT));

                ProcessedChunkCount++;

                CBumpArray<terrain_vertex> VertexData = Chunk_BuildMesh(Chunk, World);
                Chunk->Flags |= CHUNK_STATE_MESHED_BIT;

                Chunk->AllocationIndex = VB_Allocate(&World->Renderer->VB, (u32)VertexData.GetCount());
                if (Chunk->AllocationIndex != INVALID_INDEX_U32)
                {
                    TIMED_BLOCK("Upload");

                    u64 Size = VertexData.GetCount() * sizeof(terrain_vertex);
                    World->Debug.MaxRecordedVBSize = Max(Size, World->Debug.MaxRecordedVBSize);
                    u64 Offset = VB_GetAllocationMemoryOffset(&World->Renderer->VB, Chunk->AllocationIndex);

                    if (StagingHeap_Copy(
                        &World->Renderer->StagingHeap,
                        World->Renderer->RenderDevice.TransferQueue,
                        World->Renderer->TransferCmdBuffer,
                        Offset, World->Renderer->VB.Buffer,
                        Size, VertexData.GetData()))
                    {
                        Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT;
                    }
                    else
                    {
                        VB_Free(&World->Renderer->VB, Chunk->AllocationIndex);
                        Chunk->AllocationIndex = INVALID_INDEX_U32;
                        assert(!"Upload failed");
                    }
                }
                else
                {
                    assert(!"Allocation failed");
                }
            }
        }
    }
}

bool World_Initialize(world* World)
{
    // Allocate chunk memory
    {
        u64 ChunkHeadersSize = (u64)world::MaxChunkCount * sizeof(chunk);
        World->Chunks = (chunk*)Platform_VirtualAlloc(nullptr, ChunkHeadersSize);
        if (!World->Chunks)
        {
            return false;
        }

        u64 ChunkDataSize = (u64)world::MaxChunkCount * sizeof(chunk_data);
        World->ChunkData = (chunk_data*)Platform_VirtualAlloc(nullptr, ChunkDataSize);
        if (!World->ChunkData)
        {
            return false;
        }
    }

    // Init chunks
    for (u32 i = 0; i < World->MaxChunkCount; i++)
    {
        chunk* Chunk = World->Chunks + i;
        chunk_data* ChunkData = World->ChunkData + i;

        Chunk->AllocationIndex = INVALID_INDEX_U32;
        Chunk->OldAllocationIndex = INVALID_INDEX_U32;
        Chunk->Data = ChunkData;
    }

    // Place the player in the middle of the starting chunk
    World->Player.P = { (0.5f * CHUNK_DIM_X + 0.5f), 0.5f * CHUNK_DIM_Y + 0.5f, 100.0f };
    World->Player.CurrentFov = World->Player.DefaultFov;
    World->Player.TargetFov = World->Player.TargetFov;

    World->Debug.DebugCamera.FieldOfView = ToRadians(90.0f);

    Perlin2_Init(&World->Perlin2, 0);
    Perlin3_Init(&World->Perlin3, 0);

    return true;
}

void World_HandleInput(world* World, game_input* Input, f32 DeltaTime)
{
    if (World->Debug.IsDebugCameraEnabled)
    {
        // Debug camera update
        if (!Input->IsCursorEnabled)
        {
            const f32 dt = DeltaTime;
            constexpr f32 CameraSpeed = 2.5e-3f;

            camera* Camera = &World->Debug.DebugCamera;
            Camera->Yaw -= Input->MouseDelta.x * CameraSpeed;
            Camera->Pitch -= Input->MouseDelta.y * CameraSpeed;

            constexpr f32 PitchClamp = 0.5f * PI - 1e-3;
            Camera->Pitch = Clamp(Camera->Pitch, -PitchClamp, +PitchClamp);

            mat4 CameraTransform = Camera->GetGlobalTransform();

            vec3 Forward = { 0.0f, 1.0f, 0.0f };
            vec3 Right = { 1.0f, 0.0f, 0.0f };
            Forward = TransformDirection(CameraTransform, Forward);
            Right = TransformDirection(CameraTransform, Right);

            vec3 Up = { 0.0f, 0.0f, 1.0f };

            f32 MoveSpeed = 3.0f;
            if (Input->LeftShift)
            {
                MoveSpeed = 10.0f;
            }
            if (Input->LeftAlt)
            {
                MoveSpeed = 50.0f;
            }

            if (Input->Forward)
            {
                Camera->P += Forward * MoveSpeed * dt;
            }
            if (Input->Back)
            {
                Camera->P -= Forward * MoveSpeed * dt;
            }
            if (Input->Right)
            {
                Camera->P += Right * MoveSpeed * dt;
            }
            if (Input->Left)
            {
                Camera->P -= Right * MoveSpeed * dt;
            }

            if (Input->Space)
            {
                Camera->P += Up * MoveSpeed * dt;
            }
            if (Input->LeftControl)
            {
                Camera->P -= Up * MoveSpeed * dt;
            }
        }
    }
    else
    {
        // Toggle map view
        if (Input->MPressed)
        {
            World->MapView.IsEnabled = !World->MapView.IsEnabled;
            if (World->MapView.IsEnabled)
            {
                World->MapView.ResetAll(World);
            }
            else
            {
                World->Player.CurrentFov = ToRadians(160.0f);
            }
        }

        if (World->MapView.IsEnabled)
        {
            World->MapView.ZoomTarget += 0.1f * Input->WheelDelta * World->MapView.ZoomTarget; // Diff equation hack?

            if (Input->MouseButtons[MOUSE_LEFT])
            {
                constexpr f32 MouseSpeed = 3.5e-3f;
                World->MapView.TargetYaw -= MouseSpeed * Input->MouseDelta.x;
                World->MapView.TargetPitch -= MouseSpeed * Input->MouseDelta.y;
                World->MapView.TargetPitch = Clamp(World->MapView.TargetPitch, World->MapView.PitchMin, World->MapView.PitchMax);
            }
            else if (Input->MouseButtons[MOUSE_RIGHT])
            {
                mat2 Axes = World->MapView.GetAxesXY();

                constexpr f32 MoveSpeed = 2.5e-1f;
                World->MapView.TargetP += (Axes * vec2{ -Input->MouseDelta.x, Input->MouseDelta.y }) * (MoveSpeed / World->MapView.ZoomTarget);
            }
            else if (Input->MouseButtons[MOUSE_MIDDLE])
            {
                World->MapView.Reset(World);
            }
        }
        else
        {
            // Player update
            Player_HandleInput(&World->Player, Input);
        }
    }
}

void World_Update(world* World, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    ImGui::Begin("Memory");
    {
        ImGui::Text("MaxRecordedVBSize = %llu bytes (%.1f MB)", 
            World->Debug.MaxRecordedVBSize,
            World->Debug.MaxRecordedVBSize / (1024.0*1024.0));
    }
    ImGui::End();

    World_LoadChunks(World);

    if (World->MapView.IsEnabled)
    {
        World->MapView.CurrentP = Lerp(World->MapView.CurrentP, World->MapView.TargetP, 1.0f - Exp(-50.0f * DeltaTime));
        World->MapView.CurrentYaw = Lerp(World->MapView.CurrentYaw, World->MapView.TargetYaw, 1.0f - Exp(-30.0f * DeltaTime));
        World->MapView.CurrentPitch = Lerp(World->MapView.CurrentPitch, World->MapView.TargetPitch, 1.0f - Exp(-30.0f * DeltaTime));
        World->MapView.ZoomCurrent = Lerp(World->MapView.ZoomCurrent, World->MapView.ZoomTarget, 1.0f - Exp(-20.0f * DeltaTime));
        
    }

    constexpr f32 MinPhysicsResolution = 16.6667e-3f;

    f32 RemainingTime = DeltaTime;
    while (RemainingTime > 0.0f)
    {
        f32 dt = Min(RemainingTime, MinPhysicsResolution);
        Player_Update(&World->Player, World, dt);
        RemainingTime -= dt;
    }

    // Chunk update
    // TODO: move this
    {
        TIMED_BLOCK("ChunkUpdate");

        World->ChunkRenderDataCount = 0;

        for (u32 i = 0; i < World->MaxChunkCount; i++)
        {
            chunk* Chunk = World->Chunks + i;
            if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
            {
                continue;
            }

            if (Chunk->OldAllocationIndex != INVALID_INDEX_U32)
            {
                if (Chunk->OldAllocationLastRenderedInFrameIndex < World->FrameIndex - 1)
                {
                    VB_Free(&World->Renderer->VB, Chunk->OldAllocationIndex);
                    Chunk->OldAllocationIndex = INVALID_INDEX_U32;
                }
            }

            if ((Chunk->Flags & CHUNK_STATE_MESHED_BIT) &&
                (Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT))
            {
                assert(Chunk->OldAllocationIndex == INVALID_INDEX_U32);

                Chunk->OldAllocationIndex = Chunk->AllocationIndex;
                Chunk->LastRenderedInFrameIndex = Chunk->OldAllocationLastRenderedInFrameIndex;

                CBumpArray<terrain_vertex> VertexData = Chunk_BuildMesh(Chunk, World);

                if (VertexData.GetCount() && (VertexData.GetCount() <= 0xFFFFFFFFu))
                {
                    Chunk->AllocationIndex = VB_Allocate(&World->Renderer->VB, (u32)VertexData.GetCount());
                    u64 Offset = VB_GetAllocationMemoryOffset(&World->Renderer->VB, Chunk->AllocationIndex);
                    Chunk->LastRenderedInFrameIndex = 0;

                    u64 MemorySize = VertexData.GetCount() * sizeof(terrain_vertex);
                    if (StagingHeap_Copy(&World->Renderer->StagingHeap,
                        World->Renderer->RenderDevice.TransferQueue,
                        World->Renderer->TransferCmdBuffer,
                        Offset, World->Renderer->VB.Buffer,
                        MemorySize, VertexData.GetData()))
                    {
                        Chunk->Flags &= ~CHUNK_STATE_MESH_DIRTY_BIT;
                        Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT;
                    }
                    else
                    {
                        assert(!"Chunk upload failed");
                    }
                }
                else
                {
                    assert(!"Invalid VertexData");
                }
            }

            if (Chunk->Flags & CHUNK_STATE_UPLOADED_BIT)
            {
                assert(World->ChunkRenderDataCount < World->MaxChunkCount);

                World->ChunkRenderData[World->ChunkRenderDataCount++] = 
                {
                    .P = Chunk->P,
                    .AllocationIndex = Chunk->AllocationIndex,
                    .LastRenderedInFrameIndex = &Chunk->LastRenderedInFrameIndex,
                };
            }
        }
    }
}

void World_Render(world* World, renderer_frame_params* FrameParams)
{
    TIMED_FUNCTION();

    FrameParams->Camera = 
        World->Debug.IsDebugCameraEnabled ? 
        World->Debug.DebugCamera : 
        Player_GetCamera(&World->Player);
    FrameParams->ViewTransform = FrameParams->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)FrameParams->Renderer->SwapchainSize.width / (f32)FrameParams->Renderer->SwapchainSize.height;
    FrameParams->ProjectionTransform = PerspectiveMat4(FrameParams->Camera.FieldOfView, AspectRatio, 0.01f, 8000.0f);

    if (World->MapView.IsEnabled)
    {
        f32 SinYaw = Sin(World->MapView.CurrentYaw);
        f32 CosYaw = Cos(World->MapView.CurrentYaw);
        f32 SinPitch = Sin(World->MapView.CurrentPitch);
        f32 CosPitch = Cos(World->MapView.CurrentPitch);

        vec3 WorldUp = { 0.0f, 0.0f, 1.0f };
        vec3 Forward = 
        {
            -SinYaw*CosPitch,
            CosYaw*CosPitch,
            SinPitch,
        };
        vec3 Right = Normalize(Cross(Forward, WorldUp));
        vec3 Up = Cross(Right, Forward);

        vec3 P = vec3{ World->MapView.CurrentP.x, World->MapView.CurrentP.y, 80.0f };
        mat4 ViewTransform = Mat4(
            Right.x, Right.y, Right.z, -Dot(Right, P),
            -Up.x, -Up.y, -Up.z, +Dot(Up, P),
            Forward.x, Forward.y, Forward.z, -Dot(Forward, P),
            0.0f, 0.0f, 0.0f, 1.0f);

        FrameParams->ViewTransform = ViewTransform;
        FrameParams->ProjectionTransform = Mat4(
            World->MapView.ZoomCurrent * 2.0f / (AspectRatio*256.0f), 0.0f, 0.0f, 0.0f,
            0.0f, World->MapView.ZoomCurrent * 2.0f / (256.0f), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / 512.0f, +256.0f / 512.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    Renderer_BeginRendering(FrameParams);

    Renderer_RenderChunks(FrameParams, World->ChunkRenderDataCount, World->ChunkRenderData);

    Renderer_BeginImmediate(FrameParams);
    
    // Render selected block
    if (World->Player.HasTargetBlock)
    {
        vec3 P = (vec3)World->Player.TargetBlock;
        aabb Box = MakeAABB(P, P + vec3{ 1, 1, 1 });
        
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Box, PackColor(0x00, 0x00, 0x00));
    }

    if (World->Debug.IsHitboxEnabled)
    {
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Player_GetAABB(&World->Player), PackColor(0xFF, 0x00, 0x00));
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Player_GetVerticalAABB(&World->Player), PackColor(0xFF, 0xFF, 0x00));
    }

    // HUD
    {
        vec2 ScreenExtent = { (f32)FrameParams->Renderer->SwapchainSize.width, (f32)FrameParams->Renderer->SwapchainSize.height };
        vec2 CenterP = 0.5f * ScreenExtent;

        // Crosshair
        {
            constexpr f32 Radius = 20.0f;
            constexpr f32 Width = 1.0f;
            Renderer_ImmediateRect2D(FrameParams, CenterP - vec2{Radius, Width}, CenterP + vec2{Radius, Width}, PackColor(0xFF, 0xFF, 0xFF));
            Renderer_ImmediateRect2D(FrameParams, CenterP - vec2{Width, Radius}, CenterP + vec2{Width, Radius}, PackColor(0xFF, 0xFF, 0xFF));
        }

        // Block break indicator
        if (World->Player.HasTargetBlock && (World->Player.BreakTime > 0.0f))
        {
            constexpr f32 OutlineSize = 1.0f;
            constexpr f32 Height = 20.0f;
            constexpr f32 Width = 100.0f;
            constexpr f32 OffsetY = 200.0f;

            vec2 P0 = { CenterP.x - 0.5f * Width, ScreenExtent.y - OffsetY - 0.5f * Height }; // Upper-left
            vec2 P1 = P0 + vec2{Width, Height}; // Lower-right

            Renderer_ImmediateRectOutline2D(FrameParams, outline_type::Outer, OutlineSize, P0, P1, PackColor(0xFF, 0xFF, 0xFF));

            // Center
            f32 FillRatio = World->Player.BreakTime / World->Player.BlockBreakTime;
            f32 EndX = P0.x + FillRatio * Width;

            Renderer_ImmediateRect2D(FrameParams, P0, vec2{ EndX, P1.y }, PackColor(0xFF, 0x00, 0x00));
        }
    }
    Renderer_RenderImGui(FrameParams);

    Renderer_EndRendering(FrameParams);
}