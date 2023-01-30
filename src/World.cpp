#include "World.hpp"

#include <Game.hpp>
#include <Platform.hpp>
#include <Renderer/Renderer.hpp>

#include <Profiler.hpp>

//
// Internal functions
//
static u32 HashChunkP(const world* World, vec2i P, vec2i* Coords = nullptr);
static void LoadChunksAroundPlayer(world* World, memory_arena* TransientArena);
static chunk* ReserveChunk(world* World, vec2i P);
static chunk* FindPlayerChunk(world* World);

//
// Implementations
//

// TODO(boti): rename, I don't understand this anymore without looking at the implementation
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

void ResetPlayer(world* World)
{
    player* Player = &World->Player;
    Player->Velocity = {};
    vec3i PlayerP = (vec3i)Floor(Player->P);
    Player->P.x = PlayerP.x + 0.5f;
    Player->P.y = PlayerP.y + 0.5f;
    
    vec3i RelP;
    chunk* Chunk = GetChunkFromP(World, PlayerP, &RelP);
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

static u32 HashChunkP(const world* World, vec2i P, vec2i* Coords /*= nullptr*/)
{
    P.x = FloorDiv(P.x, CHUNK_DIM_XY);
    P.y = FloorDiv(P.y, CHUNK_DIM_XY);
    P.x += World->MaxChunkCountSqrt / 2;
    P.y += World->MaxChunkCountSqrt / 2;
    s32 ix = Modulo(P.x, World->MaxChunkCountSqrt);
    s32 iy = Modulo(P.y, World->MaxChunkCountSqrt);

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

chunk* GetChunkFromP(world* World, vec2i P)
{
    chunk* Result = nullptr;

    u32 Index = HashChunkP(World, P);
    chunk* Chunk = World->Chunks + Index;
    if (Chunk->P == P)
    {
        Result = Chunk;
    }

    return Result;
}

chunk* GetChunkFromP(world* World, vec3i P, vec3i* RelP)
{
    chunk* Result = nullptr;

    vec2i ChunkP = { FloorDiv(P.x, CHUNK_DIM_XY) * CHUNK_DIM_XY, FloorDiv(P.y, CHUNK_DIM_XY) * CHUNK_DIM_XY };
    chunk* Chunk = GetChunkFromP(World, ChunkP);
    if (Chunk)
    {
        Result = Chunk;
        if (RelP)
        {
            *RelP = P - vec3i{ ChunkP.x, ChunkP.y, 0 };
        }
    }

    return Result;
}

u16 GetVoxelTypeAt(world* World, vec3i P)
{
    u16 Result = VOXEL_AIR;

    if (0 <= P.z && P.z < CHUNK_DIM_Z)
    {
        vec3i RelP = {};
        chunk* Chunk = GetChunkFromP(World, P, &RelP);

        if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
        {
            assert(Chunk->Data);
            Result = Chunk->Data->Voxels[RelP.z][RelP.y][RelP.x];
        }
        else
        {
            //DebugPrint("WARNING: Invalid voxel read\n");
        }
    }
    return Result;
}

bool SetVoxelTypeAt(world* World, vec3i P, u16 Type)
{
    bool Result = false;

    vec3i RelP = {};
    chunk* Chunk = GetChunkFromP(World, P, &RelP);

    if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
    {
        assert(Chunk->Data);
        if ((0 <= RelP.z) && (RelP.z < CHUNK_DIM_Z))
        {
            Chunk->Data->Voxels[RelP.z][RelP.y][RelP.x] = Type;
            Chunk->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;

            if (RelP.x == 0)
            {
                chunk* Neighbor = GetChunkFromP(World, Chunk->P + CardinalDirections[West] * CHUNK_DIM_XY);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.x == CHUNK_DIM_XY - 1)
            {
                chunk* Neighbor = GetChunkFromP(World, Chunk->P + CardinalDirections[East] * CHUNK_DIM_XY);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            if (RelP.y == 0)
            {
                chunk* Neighbor = GetChunkFromP(World, Chunk->P + CardinalDirections[South] * CHUNK_DIM_XY);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.y == CHUNK_DIM_XY - 1)
            {
                chunk* Neighbor = GetChunkFromP(World, Chunk->P + CardinalDirections[North] * CHUNK_DIM_XY);
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

voxel_neighborhood GetVoxelNeighborhood(world* World, vec3i P)
{
    voxel_neighborhood Result = {};

    for (s32 z = -1; z <= 1; z++)
    {
        for (s32 y = -1; y <= 1; y++)
        {
            for (s32 x = -1; x <= 1; x++)
            {
                Result.GetVoxel(vec3i{ x, y, z }) = GetVoxelTypeAt(World, P + vec3i{ x, y, z });
            }
        }
    }

    return Result;
}


static chunk* ReserveChunk(world* World, vec2i P)
{
    chunk* Result = nullptr;
    u32 Index = HashChunkP(World, P);
    assert(Index < World->MaxChunkCount);

    Result = World->Chunks + Index;
    if (Result->P != P)
    {
        if (Result->Flags & CHUNK_STATE_GENERATED_BIT)
        {
            Platform.DebugPrint("WARNING: Evicting chunk { %d, %d }\n", Result->P.x, Result->P.y);

            if (Result->OldVertexBlock)
            {
                if (Result->OldAllocationLastRenderedInFrameIndex < World->FrameIndex - 1)
                {
                    VB_Free(&World->Renderer->VB, Result->OldVertexBlock);
                    Result->OldVertexBlock = nullptr;
                }
                else
                {
                    // TODO(boti): the vertex memory will need to be kept alive until the GPU is no longer using it
                    assert(!"Unimplemented code path");
                }
            }
            if (Result->VertexBlock)
            {
                if (Result->LastRenderedInFrameIndex < World->FrameIndex - 1)
                {
                    VB_Free(&World->Renderer->VB, Result->VertexBlock);
                }
                else
                {
                    // Preserve the current allocation, it will get freed once the GPU is no longer using it
                    Result->OldVertexBlock = Result->VertexBlock;
                    Result->OldAllocationLastRenderedInFrameIndex = Result->LastRenderedInFrameIndex;
                }

                Result->VertexBlock = nullptr;
                Result->LastRenderedInFrameIndex = 0;
            }
        }

        Result->P = P;
        Result->Flags = 0;
    }

    return Result;
}

static chunk* FindPlayerChunk(world* World)
{
    TIMED_FUNCTION();
    chunk* Result = nullptr;

    vec2i PlayerChunkP = ((vec2i)Floor(vec2{ World->Player.P.x / CHUNK_DIM_XY, World->Player.P.y / CHUNK_DIM_XY })) * vec2i{ CHUNK_DIM_XY, CHUNK_DIM_XY };
    u32 Index = HashChunkP(World, PlayerChunkP);
    chunk* Chunk = World->Chunks + Index;
    if (Chunk->P == PlayerChunkP)
    {
        Result = Chunk;
    }
    return Result;
}

bool RayCast(
    world* World, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, direction* OutDir)
{
    TIMED_FUNCTION();

    bool Result = false;

    //vec3 RelP = P - (vec3)(vec3i{Chunk->P.x, Chunk->P.y, 0 } * vec3i{ CHUNK_DIM_X, CHUNK_DIM_Y, 1 });

    V = NOZ(V);
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
                u16 Voxel = GetVoxelTypeAt(World, vec3i{x, y, z});
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

vec3 MoveEntityBy(world* World, entity* Entity, aabb AABB, vec3 dP)
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
                        u16 VoxelType = GetVoxelTypeAt(World, vec3i{x, y, z});
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
                if (Intersect(AABB, AABBStack[i], Overlap, MinCoord))
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
void LoadChunksAroundPlayer(world* World, memory_arena* TransientArena)
{
    TIMED_FUNCTION();

    vec2 PlayerP = (vec2)World->Player.P;
    vec2i PlayerChunkP = ((vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_XY, (f32)CHUNK_DIM_XY })) * vec2i{ CHUNK_DIM_XY, CHUNK_DIM_XY };

    constexpr s32 ImmediateMeshDistance = 1;
    constexpr s32 ImmediateGenerationDistance = ImmediateMeshDistance + 1;
#if BLOKKER_TINY_RENDER_DISTANCE
    constexpr s32 MeshDistance = 3;
#else
    constexpr s32 MeshDistance = 20;
#endif
    constexpr s32 GenerationDistance = MeshDistance + 1;

    // Create a stack that'll hold the chunks that haven't been meshed/generated around the player.
    constexpr u32 StackSize = (2*GenerationDistance + 1)*(2*GenerationDistance + 1);
    u32 StackAt = 0;
    chunk* Stack[StackSize];

    chunk* PlayerChunk = FindPlayerChunk(World);
    if (!PlayerChunk)
    {
        PlayerChunk = ReserveChunk(World, PlayerChunkP);
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
    s32 ClosestNotGeneratedDistance = GenerationDistance + 1;
    s32 ClosestNotMeshedDistance = MeshDistance + 1;

    for (s32 Ring = 0; Ring <= GenerationDistance; Ring++)
    {
        u32 Diameter = 2*Ring + 1;
        vec2i CurrentP = PlayerChunk->P - vec2i{ Ring * CHUNK_DIM_XY, Ring * CHUNK_DIM_XY};

        u32 CurrentCardinal = South; // last
        for (u32 CardinalIndex = 0; CardinalIndex < 4; CardinalIndex++)
        {
            CurrentCardinal = CardinalNext(CurrentCardinal);
            for (u32 EdgeIndex = 0; EdgeIndex < Diameter - 1; EdgeIndex++)
            {
                CurrentP = CurrentP + CardinalDirections[CurrentCardinal] * CHUNK_DIM_XY;
                chunk* Chunk = GetChunkFromP(World, CurrentP);
                if (!Chunk)
                {
                    Chunk = ReserveChunk(World, CurrentP);
                }

                if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT) ||
                    !(Chunk->Flags & CHUNK_STATE_MESHED_BIT) ||
                    !(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT) ||
                    (Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT))
                {
                    Stack[StackAt++] = Chunk;

                    if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
                    {
                        ClosestNotGeneratedDistance = Min(Ring, ClosestNotGeneratedDistance);
                    }
                    if (!(Chunk->Flags & CHUNK_STATE_MESHED_BIT))
                    {
                        ClosestNotMeshedDistance = Min(Ring, ClosestNotMeshedDistance);
                    }
                }
            }
        }
    }

    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];
        s32 Distance = ChebyshevDistance(Chunk->P, PlayerChunkP) / CHUNK_DIM_XY;

        bool ShouldGenerate = Distance <= ClosestNotGeneratedDistance/* || Distance < ImmediateGenerationDistance*/;

        if (ShouldGenerate && !Chunk->InGenerationQueue && 
            ((Chunk->Flags & CHUNK_STATE_GENERATED_BIT) == 0))
        {
            Chunk->InGenerationQueue = true;
            Platform.AddWork(Platform.LowPriorityQueue,
                [Chunk, World](memory_arena* Arena)
                {
                    Generate(Chunk, World);

                    chunk_work* Work = nullptr;
                    while (!Work)
                    {
                        u32 WriteIndex = World->ChunkWorkWriteIndex;
                        while (WriteIndex - World->ChunkWorkReadIndex == World->ChunkWorkQueueCount)
                        {
                            SpinWait;
                        }
                        if (AtomicCompareExchange(&World->ChunkWorkWriteIndex, WriteIndex + 1, WriteIndex) == WriteIndex)
                        {
                            Work = World->ChunkWorkResults + (WriteIndex % World->ChunkWorkQueueCount);
                        }
                    }

                    Work->Type = ChunkWork_Generate;
                    Work->Chunk = Chunk;
                    AtomicExchange(&Work->IsReady, true);
                });
        }
    }

    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];

        s32 Distance = ChebyshevDistance(Chunk->P, PlayerChunkP) / CHUNK_DIM_XY;
        bool ShouldMesh = 
            Distance < ClosestNotGeneratedDistance && 
            Distance <= ClosestNotMeshedDistance && 
            Distance <= MeshDistance;
        
        if (ShouldMesh && !Chunk->InMeshQueue &&
            (((Chunk->Flags & CHUNK_STATE_MESHED_BIT) == 0) ||
            ((Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT) != 0)))
        {
            platform_work_queue* Queue = ((Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT) != 0) ?
                Platform.HighPriorityQueue : Platform.LowPriorityQueue;

            Chunk->InMeshQueue = true;
            Platform.AddWork(Queue,
                [Chunk, World](memory_arena* Arena)
                {
                    chunk_mesh Mesh = BuildMesh(Chunk, World, Arena);
                    assert(Mesh.VertexCount <= World->VertexBufferCount);

                    chunk_work* Work = nullptr;
                    while (!Work)
                    {
                        u32 WriteIndex = World->ChunkWorkWriteIndex;
                        while (WriteIndex - World->ChunkWorkReadIndex == World->ChunkWorkQueueCount)
                        {
                            SpinWait;
                        }
                        if (AtomicCompareExchange(&World->ChunkWorkWriteIndex, WriteIndex + 1, WriteIndex) == WriteIndex)
                        {
                            Work = World->ChunkWorkResults + (WriteIndex % World->ChunkWorkQueueCount);
                        }
                    }
                    Work->Type = ChunkWork_BuildMesh;
                    Work->Chunk = Chunk;

                    u64 FirstIndex = AtomicAdd(&World->VertexBufferWriteIndex, Mesh.VertexCount);
                    u64 OnePastLastIndex = FirstIndex + Mesh.VertexCount;
                    while (OnePastLastIndex - AtomicLoad(&World->VertexBufferReadIndex) >= World->VertexBufferCount)
                    {
                        SpinWait;
                    }
                    for (u64 i = 0; i < Mesh.VertexCount; i++)
                    {
                        World->VertexBuffer[(FirstIndex + i) % World->VertexBufferCount] = Mesh.VertexData[i];
                    }
                    Work->Mesh.FirstIndex = FirstIndex;
                    Work->Mesh.OnePastLastIndex = OnePastLastIndex;
                    AtomicExchange(&Work->IsReady, true);
                });
        }
    }
}

bool Initialize(world* World)
{
    // Allocate chunk memory
    World->Chunks = PushArray<chunk>(World->Arena, world::MaxChunkCount);
    World->ChunkData = PushArray<chunk_data>(World->Arena, world::MaxChunkCount);
    if (!World->Chunks || !World->ChunkData)
    {
        return false;
    }

    World->VertexBuffer = PushArray<terrain_vertex>(World->Arena, World->VertexBufferCount);
    if (!World->VertexBuffer)
    {
        return false;
    }

    // Init chunks
    for (u32 i = 0; i < World->MaxChunkCount; i++)
    {
        chunk* Chunk = World->Chunks + i;
        chunk_data* ChunkData = World->ChunkData + i;

        Chunk->VertexBlock = nullptr;
        Chunk->OldVertexBlock = nullptr;
        Chunk->Data = ChunkData;
    }

    // Place the player in the middle of the starting chunk
    World->Player.P = { (0.5f * CHUNK_DIM_XY + 0.5f), 0.5f * CHUNK_DIM_XY + 0.5f, 100.0f };
    World->Player.CurrentFov = World->Player.DefaultFov;
    World->Player.TargetFov = World->Player.TargetFov;

    World->Debug.DebugCamera.FieldOfView = ToRadians(90.0f);

    Perlin2_Init(&World->Perlin2, 1);
    Perlin3_Init(&World->Perlin3, 1);

    return true;
}

void HandleInput(world* World, game_io* IO)
{
    if (World->Debug.IsDebugCameraEnabled)
    {
        // Debug camera update
        if (!IO->IsCursorEnabled)
        {
            const f32 dt = IO->DeltaTime;
            constexpr f32 CameraSpeed = 2.5e-3f;

            camera* Camera = &World->Debug.DebugCamera;
            Camera->Yaw -= IO->MouseDelta.x * CameraSpeed;
            Camera->Pitch -= IO->MouseDelta.y * CameraSpeed;

            constexpr f32 PitchClamp = 0.5f * PI - 1e-3f;
            Camera->Pitch = Clamp(Camera->Pitch, -PitchClamp, +PitchClamp);

            mat4 CameraTransform = Camera->GetGlobalTransform();

            vec3 Forward = { 0.0f, 1.0f, 0.0f };
            vec3 Right = { 1.0f, 0.0f, 0.0f };
            Forward = TransformDirection(CameraTransform, Forward);
            Right = TransformDirection(CameraTransform, Right);

            vec3 Up = { 0.0f, 0.0f, 1.0f };

            f32 MoveSpeed = 3.0f;
            if (IO->LeftShift) MoveSpeed = 10.0f;
            if (IO->LeftAlt) MoveSpeed = 50.0f;

            if (IO->Forward) Camera->P += Forward * MoveSpeed * dt;
            if (IO->Back) Camera->P -= Forward * MoveSpeed * dt;
            if (IO->Right) Camera->P += Right * MoveSpeed * dt;
            if (IO->Left) Camera->P -= Right * MoveSpeed * dt;

            if (IO->Space) Camera->P += Up * MoveSpeed * dt;
            if (IO->LeftControl) Camera->P -= Up * MoveSpeed * dt;
        }
    }
    else
    {
        // Toggle map view
        if (IO->MPressed)
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
            World->MapView.ZoomTarget += 0.1f * IO->WheelDelta * World->MapView.ZoomTarget; // Diff equation hack?

            if (IO->MouseButtons[MOUSE_LEFT])
            {
                constexpr f32 MouseSpeed = 3.5e-3f;
                World->MapView.TargetYaw -= MouseSpeed * IO->MouseDelta.x;
                World->MapView.TargetPitch -= MouseSpeed * IO->MouseDelta.y;
                World->MapView.TargetPitch = Clamp(World->MapView.TargetPitch, World->MapView.PitchMin, World->MapView.PitchMax);
            }
            else if (IO->MouseButtons[MOUSE_RIGHT])
            {
                mat2 Axes = World->MapView.GetAxesXY();

                constexpr f32 MoveSpeed = 2.5e-1f;
                World->MapView.TargetP += (Axes * vec2{ -IO->MouseDelta.x, IO->MouseDelta.y }) * (MoveSpeed / World->MapView.ZoomTarget);
            }
            else if (IO->MouseButtons[MOUSE_MIDDLE])
            {
                World->MapView.Reset(World);
            }
        }
        else
        {
            // Player update
            HandleInput(&World->Player, IO);
        }
    }
}

void Update(world* World, game_io* IO, memory_arena* TransientArena)
{
    TIMED_FUNCTION();

    if (World->MapView.IsEnabled)
    {
        World->MapView.CurrentP = Lerp(World->MapView.CurrentP, World->MapView.TargetP, 1.0f - Exp(-50.0f * IO->DeltaTime));
        World->MapView.CurrentYaw = Lerp(World->MapView.CurrentYaw, World->MapView.TargetYaw, 1.0f - Exp(-30.0f * IO->DeltaTime));
        World->MapView.CurrentPitch = Lerp(World->MapView.CurrentPitch, World->MapView.TargetPitch, 1.0f - Exp(-30.0f * IO->DeltaTime));
        World->MapView.ZoomCurrent = Lerp(World->MapView.ZoomCurrent, World->MapView.ZoomTarget, 1.0f - Exp(-20.0f * IO->DeltaTime));
    }

    LoadChunksAroundPlayer(World, TransientArena);

    bool WaitForPlayerChunk = false;
    chunk* PlayerChunk = FindPlayerChunk(World);
    if ((PlayerChunk->Flags & CHUNK_STATE_GENERATED_BIT) == 0)
    {
        WaitForPlayerChunk = true;
        assert(PlayerChunk->InGenerationQueue);
    }

    do
    {
        for (; World->ChunkWorkReadIndex < World->ChunkWorkWriteIndex; AtomicIncrement(&World->ChunkWorkReadIndex))
        {
            chunk_work* Work = World->ChunkWorkResults + (World->ChunkWorkReadIndex % World->ChunkWorkQueueCount);
            while (AtomicLoad(&Work->IsReady) == false)
            {
                SpinWait;
            }

            chunk* Chunk = Work->Chunk;
            if (Work->Type == ChunkWork_Generate)
            {
                Chunk->Flags |= CHUNK_STATE_GENERATED_BIT;
                Chunk->InGenerationQueue = false;
                if (Chunk == PlayerChunk)
                {
                    WaitForPlayerChunk = false;
                }
            }
            else if (Work->Type == ChunkWork_BuildMesh)
            {
                u64 Count = Work->Mesh.OnePastLastIndex - Work->Mesh.FirstIndex;
                Chunk->OldVertexBlock = Chunk->VertexBlock;
                Chunk->VertexBlock = VB_Allocate(&World->Renderer->VB, (u32)Count);
                if (Chunk->VertexBlock)
                {
                    u64 Size = Count * sizeof(terrain_vertex);
                    u64 BaseOffset = VB_GetAllocationMemoryOffset(Chunk->VertexBlock);

                    u64 HeadCount = Count;
                    u64 TailCount = 0;
                    u64 FirstIndexModCount = Work->Mesh.FirstIndex % World->VertexBufferCount;
                    u64 OnePastLastIndexModCount = Work->Mesh.OnePastLastIndex % World->VertexBufferCount;
                    if (OnePastLastIndexModCount < FirstIndexModCount)
                    {
                        HeadCount = World->VertexBufferCount - FirstIndexModCount;
                        TailCount = OnePastLastIndexModCount;
                    }
                    u64 HeadSize = HeadCount * sizeof(terrain_vertex);
                    u64 TailSize = TailCount * sizeof(terrain_vertex);


                    auto CopyVertexData = [World](u64 Offset, u64 Size, void* Data) -> bool
                    {
                        return StagingHeap_Copy(
                            &World->Renderer->StagingHeap,
                            World->Renderer->RenderDevice.TransferQueue,
                            World->Renderer->TransferCmdBuffer,
                            Offset, World->Renderer->VB.Buffer,
                            Size, Data);
                    };

                    bool CopyResult = CopyVertexData(BaseOffset, HeadSize, 
                                                     World->VertexBuffer + FirstIndexModCount);
                    if (TailCount != 0)
                    {
                        CopyResult &= CopyVertexData(BaseOffset + HeadSize, TailSize,
                                                     World->VertexBuffer);
                    }

                    if (CopyResult)
                    {
                        Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT | CHUNK_STATE_MESHED_BIT;
                        Chunk->Flags &= ~CHUNK_STATE_MESH_DIRTY_BIT;
                    }
                    else
                    {
                        Assert(!"Upload failed");
                        VB_Free(&World->Renderer->VB, Chunk->VertexBlock);
                        Chunk->VertexBlock = nullptr;
                    }

                    // NOTE(boti): In rare cases it's possible for the meshes to be written into the vertex ring buffer
                    //             in a different order than the one they arrive in in the ChunkWorkResults queue.
                    //             We maintain the information of the last mesh for this reason, which seems sufficient for the 99.9%
                    //             case because this race condition only happens when two (or more) threads finish meshing at the exact same
                    //             time, but if it does become an issue later on, we could maintain a small buffer of the last meshes.
                    if (World->VertexBufferReadIndex == Work->Mesh.FirstIndex)
                    {
                        AtomicExchange(&World->VertexBufferReadIndex, Work->Mesh.OnePastLastIndex);
                        if (World->IsLastMeshValid)
                        {
                            if (World->LastMeshFirstIndex == World->VertexBufferReadIndex)
                            {
                                AtomicExchange(&World->VertexBufferReadIndex, World->LastMeshOnePastLastIndex);
                                World->IsLastMeshValid = false;
                            }
                            else
                            {
                                Assert(!"Too many out of order mesh chunk works");
                            }
                        }
                    }
                    else
                    {
                        Assert(!World->IsLastMeshValid);
                        World->IsLastMeshValid = true;
                        World->LastMeshFirstIndex = Work->Mesh.FirstIndex;
                        World->LastMeshOnePastLastIndex = Work->Mesh.OnePastLastIndex;
                    }
                }
                else
                {
                    assert(!"Allocation failed");
                }

                Chunk->InMeshQueue = false;
            }

            AtomicExchange(&Work->IsReady, false);
        }
    } while (WaitForPlayerChunk);

    constexpr f32 MinPhysicsResolution = 16.6667e-3f;

    f32 RemainingTime = IO->DeltaTime;
    while (RemainingTime > 0.0f)
    {
        f32 dt = Min(RemainingTime, MinPhysicsResolution);
        Update(&World->Player, World, dt);
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
            if (((Chunk->Flags & CHUNK_STATE_GENERATED_BIT)) == 0 ||
                ((Chunk->Flags & CHUNK_STATE_MESHED_BIT) == 0))
            {
                continue;
            }

            if (Chunk->OldVertexBlock)
            {
                if (Chunk->OldAllocationLastRenderedInFrameIndex < World->FrameIndex - 1)
                {
                    VB_Free(&World->Renderer->VB, Chunk->OldVertexBlock);
                    Chunk->OldVertexBlock = nullptr;
                }
            }

            if (Chunk->Flags & CHUNK_STATE_UPLOADED_BIT)
            {
                assert(World->ChunkRenderDataCount < World->MaxChunkCount);

                World->ChunkRenderData[World->ChunkRenderDataCount++] = 
                {
                    .P = Chunk->P,
                    .VertexBlock = Chunk->VertexBlock,
                    .LastRenderedInFrameIndex = &Chunk->LastRenderedInFrameIndex,
                };
            }
        }
    }

    if (World->FrameIndex % 500 == 0)
    {
        u64 ChunkCount = 0;
        chunk** Chunks = PushArray<chunk*>(TransientArena, World->MaxChunkCount);
        for (u64 i = 0; i < World->MaxChunkCount; i++)
        {
            chunk* Chunk = World->Chunks + i;
            if (Chunk->VertexBlock || Chunk->OldVertexBlock)
            {
                Chunks[ChunkCount++] = Chunk;
            }
        }

        vulkan_vertex_buffer_block* Sentinel = &World->Renderer->VB.UsedBlockSentinel;
        for (vulkan_vertex_buffer_block* It = Sentinel->Next; It != Sentinel; It = It->Next)
        {
            bool BlockFound = false;
            for (u64 i = 0; i < ChunkCount; i++)
            {
                chunk* Chunk = Chunks[i];
                if (Chunk->VertexBlock == It ||
                    Chunk->OldVertexBlock == It)
                {
                    BlockFound = true;
                    break;
                }
            }
            Assert(BlockFound);
        }
    }
}

void World_Render(world* World, renderer_frame_params* FrameParams)
{
    TIMED_FUNCTION();

    FrameParams->Camera = 
        World->Debug.IsDebugCameraEnabled ? 
        World->Debug.DebugCamera : 
        GetCamera(&World->Player);
    FrameParams->ViewTransform = FrameParams->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)FrameParams->Renderer->SwapchainSize.width / (f32)FrameParams->Renderer->SwapchainSize.height;
    FrameParams->ProjectionTransform = PerspectiveMat4(FrameParams->Camera.FieldOfView, AspectRatio, FrameParams->Camera.Near, FrameParams->Camera.Far);

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
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, GetAABB(&World->Player), PackColor(0xFF, 0x00, 0x00));
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, GetVerticalAABB(&World->Player), PackColor(0xFF, 0xFF, 0x00));
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