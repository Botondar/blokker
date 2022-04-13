#include "World.hpp"

#include <Game.hpp>
#include <Platform.hpp>
#include <Renderer/Renderer.hpp>

#include <Profiler.hpp>

//
// Internal functions
//

static void World_ResetPlayer(world* World);
static void World_PreUpdatePlayer(world* World, game_input* Input);
static void World_UpdatePlayer(world* World, f32 DeltaTime);

static u32 World_HashChunkP(const world* World, vec2i P, vec2i* Coords = nullptr);
static void World_LoadChunks(world* World);

//
// Player
//

static camera Player_GetCamera(const player* Player)
{
    vec3 CamPDelta = { 0.0f, 0.0f, Player->BobAmplitude * Sin(2.0f * PI * Player->HeadBob) };
    camera Camera =
    {
        .P = Player->P + CamPDelta,
        .Yaw = Player->Yaw,
        .Pitch = Player->Pitch,
        .FieldOfView = Player->CurrentFov,
    };

    return Camera;
}

static aabb Player_GetAABB(const player* Player)
{
    aabb Result = 
    {
        .Min = 
        {
            Player->P.x - 0.5f * Player->Width, 
            Player->P.y - 0.5f * Player->Width, 
            Player->P.z - Player->EyeHeight 
        },
        .Max = 
        {
            Player->P.x + 0.5f * Player->Width, 
            Player->P.y + 0.5f * Player->Width, 
            Player->P.z + (Player->Height - Player->EyeHeight) 
        },
    };
    return Result;
}

static aabb Player_GetVerticalAABB(const player* Player)
{
    aabb Result = 
    {
        .Min = 
        {
            Player->P.x - 0.5f * Player->Width, 
            Player->P.y - 0.5f * Player->Width, 
            Player->P.z - Player->EyeHeight + Player->LegHeight,
        },
        .Max = 
        {
            Player->P.x + 0.5f * Player->Width, 
            Player->P.y + 0.5f * Player->Width, 
            Player->P.z + (Player->Height - Player->EyeHeight) 
        },
    };
    return Result;
}

static void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right)
{   
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    Right = { CosYaw, SinYaw, 0.0f };
    Forward = { -SinYaw, CosYaw, 0.0f };
}

static vec3 Player_GetForward(const player* Player)
{
    vec3 Result = {};
    
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    f32 SinPitch = Sin(Player->Pitch);
    f32 CosPitch = Cos(Player->Pitch);

    Result = 
    {
        -SinYaw*CosPitch,
        CosYaw*CosPitch,
        SinPitch,
    };

    return Result;
}

static void World_ResetPlayer(world* World)
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

static void World_PreUpdatePlayer(world* World, game_input* Input)
{
    constexpr f32 MouseTurnSpeed = 2.5e-3f;

    player* Player = &World->Player;

    Player->Control.PrimaryAction = false;
    Player->Control.SecondaryAction = false;
    // Disable mouse input when there's a cursor on the screen
    if (!Input->IsCursorEnabled)
    {
        Player->Yaw -= Input->MouseDelta.x * MouseTurnSpeed;
        Player->Pitch -= Input->MouseDelta.y * MouseTurnSpeed;

        Player->Control.PrimaryAction = Input->MouseButtons[MOUSE_LEFT];
        Player->Control.SecondaryAction = Input->MouseButtons[MOUSE_RIGHT];
    }
    constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
    Player->Pitch = Clamp(Player->Pitch, -CameraClamp, CameraClamp);

    Player->Control.DesiredMoveDirection = { 0.0f, 0.0f };
    if (Input->Forward) Player->Control.DesiredMoveDirection.x += 1.0f;
    if (Input->Back) Player->Control.DesiredMoveDirection.x -= 1.0f;
    if (Input->Right) Player->Control.DesiredMoveDirection.y += 1.0f;
    if (Input->Left) Player->Control.DesiredMoveDirection.y -= 1.0f;

    Player->Control.IsJumping = Input->Space;
    Player->Control.IsRunning = Input->LeftShift;
}

static void World_UpdatePlayer(world* World, f32 dt)
{
    TIMED_FUNCTION();
    
    player* Player = &World->Player;

    // Block breaking
    {
        vec3 Forward = Player_GetForward(Player);

        chunk* PlayerChunk = World_FindPlayerChunk(World);
        constexpr f32 PlayerReach = 8.0f;

        vec3i OldTargetBlock = Player->TargetBlock;
        Player->HasTargetBlock = World_RayCast(World, Player->P, Forward, PlayerReach, &Player->TargetBlock, &Player->TargetDirection);

        if (Player->HasTargetBlock)
        {
            // Breaking
            if ((OldTargetBlock == Player->TargetBlock) && Player->Control.PrimaryAction)
            {
                u16 VoxelType = World_GetVoxelType(World, Player->TargetBlock);
                const voxel_desc* VoxelDesc = &VoxelDescs[VoxelType];
                if (VoxelDesc->Flags & VOXEL_FLAGS_SOLID)
                {
                    Player->BreakTime += dt;
                    if (Player->BreakTime >= Player->BlockBreakTime)
                    {
                        World_SetVoxelType(World, Player->TargetBlock, VOXEL_AIR);
                    }
                }
            }
            else
            {
                Player->BreakTime = 0.0f;
            }

            // Placing
            Player->TimeSinceLastBlockPlacement += dt;
            if (Player->Control.SecondaryAction && (Player->TimeSinceLastBlockPlacement > Player->MaxBlockPlacementFrequency))
            {
                vec3i DeltaP = {};
                switch (Player->TargetDirection)
                {
                    case 0: DeltaP = { +1, 0, 0 }; break;
                    case 1: DeltaP = { -1, 0, 0 }; break;
                    case 2: DeltaP = { 0, +1, 0 }; break;
                    case 3: DeltaP = { 0, -1, 0 }; break;
                    case 4: DeltaP = { 0, 0, +1 }; break;
                    case 5: DeltaP = { 0, 0, -1 }; break;
                    default: assert(!"Invalid code path");
                }

                vec3i PlacementP = Player->TargetBlock + DeltaP;
                u16 VoxelType = World_GetVoxelType(World, PlacementP);
                if (VoxelType == VOXEL_AIR)
                {
                    aabb PlayerBox = Player_GetAABB(Player);
                    vec3i BoxP = PlacementP;
                    aabb BlockBox = MakeAABB((vec3)BoxP, (vec3)(BoxP + vec3i{1, 1, 1}));

                    vec3 Overlap;
                    int MinCoord;
                    if (!AABB_Intersect(PlayerBox, BlockBox, Overlap, MinCoord))
                    {
                        World_SetVoxelType(World, PlacementP, VOXEL_GROUND);
                        Player->TimeSinceLastBlockPlacement = 0.0f;
                    }

                }
            }
        }
    }
    

    vec3 Forward, Right;
    Player_GetHorizontalAxes(Player, Forward, Right);
    vec3 Up = { 0.0f, 0.0f, 1.0f };

    vec3 DesiredMoveDirection = SafeNormalize(Player->Control.DesiredMoveDirection.x * Forward + Player->Control.DesiredMoveDirection.y * Right);

    constexpr f32 WalkSpeed = 4.0f;
    constexpr f32 RunSpeed = 7.75f;
    
    vec3 Acceleration = {};
    if (Player->WasGroundedLastFrame)
    {
        // Walk/Run controls
        f32 DesiredSpeed = Player->Control.IsRunning ? RunSpeed : WalkSpeed;

        vec3 DesiredVelocity = DesiredMoveDirection * DesiredSpeed;
        vec3 VelocityXY = { Player->Velocity.x, Player->Velocity.y, 0.0f };
        
        vec3 DiffV = DesiredVelocity - VelocityXY;

        constexpr f32 Accel = 10.0f;
        Acceleration += Accel * DiffV;

        if (Player->Control.IsJumping)
        {
            constexpr f32 DesiredJumpHeight = 1.2f; // This is here just for reference
            constexpr f32 JumpVelocity = 7.7459667f; // sqrt(2 * Gravity * DesiredJumpHeight)
            Player->Velocity.z += JumpVelocity;
        }

        // Head bobbing
        f32 Speed = Length(Player->Velocity);
        if (Speed < WalkSpeed)
        {
            f32 t = Fade3(Speed / WalkSpeed);
            Player->BobAmplitude = Lerp(0.0f, Player->WalkBobAmplitude, t);
            Player->BobFrequency = Lerp(0.0f, Player->WalkBobFrequency, t);
        }
        else
        {
            f32 t = Fade3((Speed - WalkSpeed) / (RunSpeed - WalkSpeed));
            Player->BobAmplitude = Lerp(Player->WalkBobAmplitude, Player->RunBobAmplitude, t);
            Player->BobFrequency = Lerp(Player->WalkBobFrequency, Player->RunBobFrequency, t);
        }
        Player->HeadBob += Player->BobFrequency*dt;
        if (Player->HeadBob >= 1.0f)
        {
            Player->HeadBob -= 1.0f;
        }
    }
    else
    {
        // Clear velocity if the user is trying to move
        if (Dot(DesiredMoveDirection, DesiredMoveDirection) != 0.0f)
        {
            f32 DesiredSpeed = Player->Control.IsRunning ? RunSpeed : WalkSpeed;

            vec3 DesiredVelocity = DesiredMoveDirection * DesiredSpeed;
            vec3 VelocityXY = { Player->Velocity.x, Player->Velocity.y, 0.0f };
        
            vec3 DiffV = DesiredVelocity - VelocityXY;

            constexpr f32 Accel = 10.0f;
            Acceleration += Accel * DiffV;
        }

        // Drag force = 0.5 * c * rho * v_r^2 * A
        constexpr f32 AirDensity = 1.2041f;
        constexpr f32 DragCoeff = 1.0f;
        constexpr f32 BottomArea = Player->Width * Player->Width;
        constexpr f32 SideArea = Player->Width * Player->Height;
            
        f32 Speed = Length(Player->Velocity);
        vec3 UnitVelocity = SafeNormalize(Player->Velocity);
            
        f32 Area = Lerp(SideArea, BottomArea, Abs(Dot(UnitVelocity, vec3{0.0f, 0.0f, 1.0f})));
        vec3 DragForce = (-0.5f * AirDensity * DragCoeff * Area) * Player->Velocity * Speed;
            
        constexpr f32 PlayerMass = 60.0f;
        Acceleration += DragForce * (1.0f / PlayerMass);
    }

    constexpr f32 Gravity = 25.0f;
    
    Acceleration += vec3{ 0.0f, 0.0f, -Gravity };
    Player->Velocity += Acceleration * dt;

#if 1
    Player->CurrentFov = Player->DefaultFov;
#else
    // Fov animation
    {
        f32 PlayerSpeed = Length(Player->Velocity);
        vec3 MoveDirection = SafeNormalize(Player->Velocity);
        vec3 FacingDirection = { Forward.x * Cos(Player->Pitch), Forward.y * Cos(Player->Pitch), Sin(Player->Pitch) };
        
        f32 t = (PlayerSpeed - WalkSpeed) / (RunSpeed - WalkSpeed);
        t *= Max(Dot(MoveDirection, FacingDirection), 0.0f);
        t = Fade3(Clamp(t, 0.0f, 1.0f));
        Player->TargetFov = Lerp(Player->DefaultFov, ToRadians(90.0f), t);
        
        constexpr f32 dFov = ToRadians(1000.0f);
        f32 FovDiff = Player->TargetFov - Player->CurrentFov;

        Player->CurrentFov += Signum(FovDiff) * Min(Abs(dFov * FovDiff * dt), Abs(FovDiff));
    }
#endif
    vec3 dP = (Player->Velocity + 0.5f * Acceleration * dt) * dt;

    // Collision
    {
        TIMED_BLOCK("Collision");

        Player->WasGroundedLastFrame = false;

        chunk* PlayerChunk = World_FindPlayerChunk(World);
        assert(PlayerChunk);

        auto ApplyMovement = [&World, &Player](vec3 dP, u32 Direction) -> f32
        {
            TIMED_FUNCTION();

            Player->P[Direction] += dP[Direction];

            aabb PlayerAABB = Player_GetAABB(Player);

            vec3i MinPi = (vec3i)Floor(PlayerAABB.Min);
            vec3i MaxPi = (vec3i)Ceil(PlayerAABB.Max);

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
            
            f32 Displacement = 0.0f;
            bool IsCollision = false;
            for (u32 i = 0; i < AABBAt; i++)
            {
                vec3 Overlap;
                int MinCoord;
                if (AABB_Intersect(PlayerAABB, AABBStack[i], Overlap, MinCoord))
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
                constexpr f32 Epsilon = 1e-6f;
                Displacement += Signum(Displacement) * Epsilon;
                Player->P[Direction] += Displacement;
                if (Displacement != 0.0f)
                {
                    Player->Velocity[Direction] = 0.0f;
                }
            }
            return Displacement;
        };

        // Apply movement and resolve collisions separately on the axes
        vec3 Displacement = {};
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
        
        if (Displacement.z > 0.0f)
        {
            Player->WasGroundedLastFrame = true;
        }
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

static chunk* World_GetChunkFromP(world* World, vec2i P)
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

static chunk* World_GetChunkFromP(world* World, vec3i P, vec3i* RelP)
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

static u16 World_GetVoxelType(world* World, vec3i P)
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

static bool World_SetVoxelType(world* World, vec3i P, u16 Type)
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

static voxel_neighborhood World_GetVoxelNeighborhood(world* World, vec3i P)
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

static bool World_RayCast(
    world* World, 
    vec3 P, vec3 V, 
    f32 tMax, 
    vec3i* OutP, direction* OutDir)
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

    auto RayAABBIntersection = [RayPlaneIntersection](vec3 P, vec3 v, aabb Box, f32 tMin, f32 tMax, f32* tOut, direction* OutDir) -> bool
    {
        bool Result = false;

        bool AnyIntersection = false;
        f32 tIntersection = -1.0f;
        u32 IntersectionDir = (u32)-1;
        for (u32 i = DIRECTION_First; i < DIRECTION_Count; i++)
        {
            switch (i)
            {
                case DIRECTION_POS_X: 
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
                case DIRECTION_NEG_X: 
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
                case DIRECTION_POS_Y: 
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
                case DIRECTION_NEG_Y: 
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
                case DIRECTION_POS_Z:
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
                case DIRECTION_NEG_Z:
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
            *OutDir = (direction)IntersectionDir;
            Result = true;
        }

        return Result;
    };

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
                    if (RayAABBIntersection(P, V, Box, 0.0f, tMax, &tCurrent, &CurrentDir))
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

// Loads the chunks around the player
static void World_LoadChunks(world* World)
{
    TIMED_FUNCTION();

    vec2 PlayerP = (vec2)World->Player.P;
    vec2i PlayerChunkP = (vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_X, (f32)CHUNK_DIM_Y });

    constexpr u32 ImmediateMeshDistance = 1;
    constexpr u32 ImmediateGenerationDistance = ImmediateMeshDistance + 1;
#if BLOKKER_TINY_RENDER_DISTANCE
    constexpr u32 MeshDistance = 1;
#else
    constexpr u32 MeshDistance = 12;
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
                }
            }
        }
    }

    // Limit the number of chunks that can be processed in a single frame so that we don't hitch
    constexpr u32 ProcessedChunkLimit = 1;
    u32 ProcessedChunkCount = 0;

    // Generate the chunks in the stack
    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];

        s32 Distance = ChebyshevDistance(Chunk->P, PlayerChunkP);
        if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
        {
            if ((Distance <= ImmediateGenerationDistance) || (ProcessedChunkCount < ProcessedChunkLimit))
            {
                Chunk_Generate(Chunk, World);
                Chunk->Flags |= CHUNK_STATE_GENERATED_BIT;
                ProcessedChunkCount++;
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
            if ((Distance <= ImmediateMeshDistance) || (ProcessedChunkCount < ProcessedChunkLimit))
            {
                assert(Chunk->Flags & CHUNK_STATE_GENERATED_BIT);
                assert(!(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT));

                ProcessedChunkCount++;

                std::vector<terrain_vertex> VertexData = Chunk_BuildMesh(Chunk, World);
                Chunk->Flags |= CHUNK_STATE_MESHED_BIT;

                Chunk->AllocationIndex = VB_Allocate(&World->Renderer->VB, (u32)VertexData.size());
                if (Chunk->AllocationIndex != INVALID_INDEX_U32)
                {
                    TIMED_BLOCK("Upload");

                    u64 Size = VertexData.size() * sizeof(terrain_vertex);
                    u64 Offset = VB_GetAllocationMemoryOffset(&World->Renderer->VB, Chunk->AllocationIndex);

                    if (StagingHeap_Copy(
                        &World->Renderer->StagingHeap,
                        World->Renderer->RenderDevice.TransferQueue,
                        World->Renderer->TransferCmdBuffer,
                        Offset, World->Renderer->VB.Buffer,
                        Size, VertexData.data()))
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

static bool World_Initialize(world* World)
{
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

static void World_Update(world* World, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    World_LoadChunks(World);

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

            constexpr f32 PitchClamp = PI - 1e-3;
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
        // Player update
        World_PreUpdatePlayer(World, Input);

        constexpr f32 MinPhysicsResolution = 16.6667e-3f;

        f32 RemainingTime = DeltaTime;
        while (RemainingTime > 0.0f)
        {
            f32 dt = Min(RemainingTime, MinPhysicsResolution);
            World_UpdatePlayer(World, dt);
            RemainingTime -= dt;
        }
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

                std::vector<terrain_vertex> VertexData = Chunk_BuildMesh(Chunk, World);

                if (VertexData.size() && (VertexData.size() <= 0xFFFFFFFFu))
                {
                    Chunk->AllocationIndex = VB_Allocate(&World->Renderer->VB, (u32)VertexData.size());
                    u64 Offset = VB_GetAllocationMemoryOffset(&World->Renderer->VB, Chunk->AllocationIndex);
                    Chunk->LastRenderedInFrameIndex = 0;

                    u64 MemorySize = VertexData.size() * sizeof(terrain_vertex);
                    if (StagingHeap_Copy(&World->Renderer->StagingHeap,
                        World->Renderer->RenderDevice.TransferQueue,
                        World->Renderer->TransferCmdBuffer,
                        Offset, World->Renderer->VB.Buffer,
                        MemorySize, VertexData.data()))
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

static void World_Render(world* World, renderer_frame_params* FrameParams)
{
    TIMED_FUNCTION();

    
    FrameParams->Camera = 
        World->Debug.IsDebugCameraEnabled ? 
        World->Debug.DebugCamera : 
        Player_GetCamera(&World->Player);
    FrameParams->ViewTransform = FrameParams->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)FrameParams->Renderer->SwapchainSize.width / (f32)FrameParams->Renderer->SwapchainSize.height;
    FrameParams->ProjectionTransform = PerspectiveMat4(FrameParams->Camera.FieldOfView, AspectRatio, 0.005f, 8000.0f);

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