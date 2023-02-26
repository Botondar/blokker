#include "World.hpp"

//
// Internal functions
//
static u32 HashChunkP(const world* World, vec2i P, vec2i* Coords = nullptr);
static void LoadChunksAroundPlayer(world* World, memory_arena* TransientArena);
static chunk* ReserveChunk(world* World, vec2i P);
static chunk* FindPlayerChunk(world* World);
static void FreeChunkMesh(world* World, chunk* Chunk);

static void FlushChunkWorks(world* World, render_frame* Frame, bool WaitForPlayerChunk, chunk* PlayerChunk);
static chunk_work* GetNextChunkWorkToWrite(chunk_work_queue* Queue);

//
// Implementations
//

static chunk_work* GetNextChunkWorkToWrite(chunk_work_queue* Queue)
{
    chunk_work* Result = nullptr;
    while (!Result)
    {
        u32 WriteIndex = Queue->WriteIndex;
        while (WriteIndex - Queue->ReadIndex == Queue->MaxWorkCount)
        {
            SpinWait;
        }
        if (AtomicCompareExchange(&Queue->WriteIndex, WriteIndex + 1, WriteIndex) == WriteIndex)
        {
            Result = Queue->WorkResults + (WriteIndex % Queue->MaxWorkCount);
        }
    }
    return(Result);
}

static void FreeChunkMesh(world* World, chunk* Chunk)
{
    if (Chunk->VertexBlock)
    {
        Assert(World->ChunkDeletionWriteIndex - World->ChunkDeletionReadIndex <= World->MaxChunkDeletionQueueCount);

        u32 DeletionIndex = World->ChunkDeletionWriteIndex++;
        World->ChunkDeletionQueue[DeletionIndex % World->MaxChunkDeletionQueueCount] = Chunk->VertexBlock;
        Chunk->VertexBlock = nullptr;
        Chunk->Flags &= ~CHUNK_STATE_MESHED_BIT;
    }
}

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
        FreeChunkMesh(World, Result);
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

                    chunk_work* Work = GetNextChunkWorkToWrite(&World->ChunkWorkQueue);
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
                    chunk_work_queue* Queue = &World->ChunkWorkQueue;

                    chunk_mesh Mesh = BuildMesh(Chunk, World, Arena);
                    assert(Mesh.VertexCount <= Queue->VertexBufferCount);

                    chunk_work* Work = GetNextChunkWorkToWrite(Queue);
                    Work->Type = ChunkWork_BuildMesh;
                    Work->Chunk = Chunk;

                    u32 FirstIndex = AtomicAdd(&Queue->VertexWriteIndex, Mesh.VertexCount);
                    u32 OnePastLastIndex = FirstIndex + Mesh.VertexCount;
                    while (OnePastLastIndex - AtomicLoad(&Queue->VertexReadIndex) >= Queue->VertexBufferCount)
                    {
                        SpinWait;
                    }
                    for (u64 i = 0; i < Mesh.VertexCount; i++)
                    {
                        Queue->VertexBuffer[(FirstIndex + i) % Queue->VertexBufferCount] = Mesh.VertexData[i];
                    }
                    Work->Mesh.FirstIndex = FirstIndex;
                    Work->Mesh.OnePastLastIndex = OnePastLastIndex;
                    AtomicExchange(&Work->IsReady, true);
                });
        }
    }
}

static void FlushChunkWorks(world* World, render_frame* Frame, bool WaitForPlayerChunk, chunk* PlayerChunk)
{
    do
    {
        chunk_work_queue* Queue = &World->ChunkWorkQueue;
        for (; Queue->ReadIndex < Queue->WriteIndex; AtomicIncrement(&Queue->ReadIndex))
        {
            chunk_work* Work = Queue->WorkResults + (Queue->ReadIndex % Queue->MaxWorkCount);
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
                FreeChunkMesh(World, Work->Chunk);

                u64 Count = Work->Mesh.OnePastLastIndex - Work->Mesh.FirstIndex;
                u64 Size = Count * sizeof(terrain_vertex);

                u64 HeadCount = Count;
                u64 TailCount = 0;
                u64 FirstIndexModCount = Work->Mesh.FirstIndex % Queue->VertexBufferCount;
                u64 OnePastLastIndexModCount = Work->Mesh.OnePastLastIndex % Queue->VertexBufferCount;
                if (OnePastLastIndexModCount < FirstIndexModCount)
                {
                    HeadCount = Queue->VertexBufferCount - FirstIndexModCount;
                    TailCount = OnePastLastIndexModCount;
                }
                u64 HeadSize = HeadCount * sizeof(terrain_vertex);
                u64 TailSize = TailCount * sizeof(terrain_vertex);

                Chunk->VertexBlock = AllocateAndUploadVertexBlock(Frame, 
                                                                 HeadSize, Queue->VertexBuffer + FirstIndexModCount,
                                                                 TailSize, Queue->VertexBuffer);
                if (Chunk->VertexBlock)
                {
                    Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT | CHUNK_STATE_MESHED_BIT;
                    Chunk->Flags &= ~CHUNK_STATE_MESH_DIRTY_BIT;
                }
                else
                {
                    Chunk->Flags &= ~(CHUNK_STATE_UPLOADED_BIT|CHUNK_STATE_MESHED_BIT|CHUNK_STATE_MESH_DIRTY_BIT);
                }
                
                if (Queue->VertexReadIndex == Work->Mesh.FirstIndex)
                {
                    AtomicExchange(&Queue->VertexReadIndex, Work->Mesh.OnePastLastIndex);
                    if (Queue->IsLastMeshValid)
                    {
                        if (Queue->LastMeshFirstIndex == Queue->VertexReadIndex)
                        {
                            AtomicExchange(&Queue->VertexReadIndex, Queue->LastMeshOnePastLastIndex);
                            Queue->IsLastMeshValid = false;
                        }
                        else
                        {
                            FatalError("Too many out of order mesh chunk works");
                        }
                    }
                }
                else
                {
                    Assert(!Queue->IsLastMeshValid);
                    Queue->IsLastMeshValid = true;
                    Queue->LastMeshFirstIndex = Work->Mesh.FirstIndex;
                    Queue->LastMeshOnePastLastIndex = Work->Mesh.OnePastLastIndex;
                }

                Chunk->InMeshQueue = false;
            }

            AtomicExchange(&Work->IsReady, false);
        }
    } while (WaitForPlayerChunk);
}

bool InitializeWorld(world* World)
{
    // Allocate chunk memory
    World->Chunks = PushArray<chunk>(World->Arena, world::MaxChunkCount);
    World->ChunkData = PushArray<chunk_data>(World->Arena, world::MaxChunkCount);
    if (!World->Chunks || !World->ChunkData)
    {
        return false;
    }

    World->ChunkWorkQueue.VertexBuffer = PushArray<terrain_vertex>(World->Arena, World->ChunkWorkQueue.VertexBufferCount);
    if (!World->ChunkWorkQueue.VertexBuffer)
    {
        return false;
    }

    // Init chunks
    for (u32 i = 0; i < World->MaxChunkCount; i++)
    {
        chunk* Chunk = World->Chunks + i;
        chunk_data* ChunkData = World->ChunkData + i;

        Chunk->VertexBlock = nullptr;
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

void UpdateAndRenderWorld(game_state* Game, world* World, game_io* IO, render_frame* Frame)
{
    TIMED_FUNCTION();

    HandleInput(World, IO);

    if (Game->IsDebugUIEnabled)
    {
        ImGui::Begin("World");
        {
            ImGui::Checkbox("Hitboxes", &World->Debug.IsHitboxEnabled);
            ImGui::Text("PlayerP: { %.1f, %.1f, %.1f }", 
                        World->Player.P.x, World->Player.P.y, World->Player.P.z);
            if (ImGui::Button("Reset player"))
            {
                ResetPlayer(World);
            }

            ImGui::Checkbox("Debug camera", &World->Debug.IsDebugCameraEnabled);
            ImGui::Text("DebugCameraP: { %.1f, %.1f, %.1f }",
                        World->Debug.DebugCamera.P.x,
                        World->Debug.DebugCamera.P.y,
                        World->Debug.DebugCamera.P.z);
            if (ImGui::Button("Teleport debug camera to player"))
            {
                World->Debug.DebugCamera = GetCamera(&World->Player);
            }
            if (ImGui::Button("Teleport player to debug camera"))
            {
                World->Player.P = Game->World->Debug.DebugCamera.P;
            }
        }
        ImGui::End();
    }

    if (World->MapView.IsEnabled)
    {
        World->MapView.CurrentP = Lerp(World->MapView.CurrentP, World->MapView.TargetP, 1.0f - Exp(-50.0f * IO->DeltaTime));
        World->MapView.CurrentYaw = Lerp(World->MapView.CurrentYaw, World->MapView.TargetYaw, 1.0f - Exp(-30.0f * IO->DeltaTime));
        World->MapView.CurrentPitch = Lerp(World->MapView.CurrentPitch, World->MapView.TargetPitch, 1.0f - Exp(-30.0f * IO->DeltaTime));
        World->MapView.ZoomCurrent = Lerp(World->MapView.ZoomCurrent, World->MapView.ZoomTarget, 1.0f - Exp(-20.0f * IO->DeltaTime));
    }

    Frame->Camera = 
        World->Debug.IsDebugCameraEnabled ? 
        World->Debug.DebugCamera : 
        GetCamera(&World->Player);
    Frame->ViewTransform = Frame->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)Frame->RenderExtent.width / (f32)Frame->RenderExtent.height;
    Frame->ProjectionTransform = PerspectiveMat4(Frame->Camera.FieldOfView, AspectRatio, Frame->Camera.Near, Frame->Camera.Far);

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

        Frame->ViewTransform = ViewTransform;
        Frame->ProjectionTransform = Mat4(
            World->MapView.ZoomCurrent * 2.0f / (AspectRatio*256.0f), 0.0f, 0.0f, 0.0f,
            0.0f, World->MapView.ZoomCurrent * 2.0f / (256.0f), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / 512.0f, +256.0f / 512.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    while (World->ChunkDeletionReadIndex != World->ChunkDeletionWriteIndex)
    {
        u32 Index = (World->ChunkDeletionReadIndex++) % World->MaxChunkDeletionQueueCount;
        FreeVertexBlock(Frame, World->ChunkDeletionQueue[Index]);
    }

    LoadChunksAroundPlayer(World, &Game->TransientArena);

    bool WaitForPlayerChunk = false;
    chunk* PlayerChunk = FindPlayerChunk(World);
    if ((PlayerChunk->Flags & CHUNK_STATE_GENERATED_BIT) == 0)
    {
        WaitForPlayerChunk = true;
        assert(PlayerChunk->InGenerationQueue);
    }

    FlushChunkWorks(World, Frame, WaitForPlayerChunk, PlayerChunk);

#if 1
    UpdatePlayer(Game, World, IO, &World->Player, Frame);
#else
    constexpr f32 MinPhysicsResolution = 1.0f / 60.0f;
    f32 RemainingTime = IO->DeltaTime;
    while (RemainingTime > 0.0f)
    {
        f32 dt = Min(RemainingTime, MinPhysicsResolution);
        UpdatePlayer(Game, World, &World->Player, dt);
        RemainingTime -= dt;
    }
#endif

    // Chunk update
    // TODO: move this
    {
        TIMED_BLOCK("ChunkUpdate");

        frustum CameraFrustum = Frame->Camera.GetFrustum((f32)Frame->RenderExtent.width / Frame->RenderExtent.height);
        for (u32 i = 0; i < World->MaxChunkCount; i++)
        {
            chunk* Chunk = World->Chunks + i;
            if (Chunk->VertexBlock)
            {
                vec3 MinP = vec3{ (f32)Chunk->P.x, (f32)Chunk->P.y, 0.0f };
                vec3 MaxP = MinP + vec3{ CHUNK_DIM_XY, CHUNK_DIM_XY, CHUNK_DIM_Z };
                if (IntersectFrustumAABB(CameraFrustum, MakeAABB(MinP, MaxP)))
                {
                    RenderChunk(Frame, Chunk->VertexBlock, (vec2)Chunk->P);
                }
            }
        }
    }
}