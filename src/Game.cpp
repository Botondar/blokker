#include "Game.hpp"

#include <Profiler.hpp>
#include <bmp.hpp>
#include <Float.hpp>

#include <vector>

#include "Renderer.cpp"
#include "Math.cpp"
#include "Random.cpp"
#include "Common.cpp"
#include "Camera.cpp"
#include "Chunk.cpp"
#include "Shapes.cpp"
#include "Profiler.cpp"

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
    assert(Player);
    
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    Right = { CosYaw, SinYaw, 0.0f };
    Forward = { -SinYaw, CosYaw, 0.0f };
}

static vec3 Player_GetForward(const player* Player)
{
    assert(Player);
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

static bool Game_InitImGui(game_state* GameState);

static chunk* Game_ReserveChunk(game_state* GameState);
static void Game_FindChunkNeighbors(game_state* GameState, chunk* Chunk);
static chunk* Game_FindPlayerChunk(game_state* GameState);

static void Game_LoadChunks(game_state* GameState);

static void Game_PreUpdatePlayer(game_state* GameState, game_input* Input);
static void Game_UpdatePlayer(game_state* GameState, game_input* Input, f32 DeltaTime);
static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime);

static void Game_Render(game_state* GameState, f32 DeltaTime);

static chunk* Game_ReserveChunk(game_state* GameState)
{
    chunk* Result = nullptr;
    if (GameState->ChunkCount < GameState->MaxChunkCount)
    {
        Result = GameState->Chunks + GameState->ChunkCount;
        // For now, chunk headers are implicitly tied to the chunk data by their indices
        // but we'll want to separate them in the future;
        Result->Data = GameState->ChunkData + GameState->ChunkCount;
        GameState->ChunkCount++;
    }
    return Result;
}

static void Game_FindChunkNeighbors(game_state* GameState, chunk* Chunk)
{
    TIMED_FUNCTION();

    if (!Chunk->Neighbors[East] || !Chunk->Neighbors[North] || !Chunk->Neighbors[West] || !Chunk->Neighbors[South])
    {
        for (u32 i = 0; i < GameState->ChunkCount; i++)
        {
            chunk* Other = GameState->Chunks + i;
            for (u32 Cardinal = Cardinal_First; Cardinal < Cardinal_Count; Cardinal++)
            {
                if ((Chunk->P + CardinalDirections[Cardinal]) == Other->P)
                {
                    Chunk->Neighbors[Cardinal] = Other;
                    Other->Neighbors[CardinalOpposite(Cardinal)] = Chunk;
                }
            }
        }
    }
}

static chunk* Game_FindPlayerChunk(game_state* GameState)
{
    TIMED_FUNCTION();
    chunk* Result = nullptr;
    vec2i PlayerChunkP = (vec2i)Floor(vec2{ GameState->Player.P.x / CHUNK_DIM_X, GameState->Player.P.y / CHUNK_DIM_Y });

    for (u32 i = 0; i < GameState->ChunkCount; i++)
    {
        chunk* Chunk = GameState->Chunks + i;
        if (PlayerChunkP == Chunk->P)
        {
            Result = Chunk;
            break;
        }
    }

    return Result;
}

// Loads the chunks around the player
static void Game_LoadChunks(game_state* GameState)
{
    TIMED_FUNCTION();

    vec2 PlayerP = (vec2)GameState->Player.P;
    vec2i PlayerChunkP = (vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_X, (f32)CHUNK_DIM_Y });

    constexpr u32 ImmediateMeshDistance = 1;
    constexpr u32 ImmediateGenerationDistance = ImmediateMeshDistance + 1;
    constexpr u32 MeshDistance = 16;
    constexpr u32 GenerationDistance = MeshDistance + 1;

    // Create a stack that'll hold the chunks that haven't been meshed/generated around the player.
    constexpr u32 StackSize = (2*GenerationDistance + 1)*(2*GenerationDistance + 1);
    u32 StackAt = 0;
    chunk* Stack[StackSize];

    chunk* PlayerChunk = Game_FindPlayerChunk(GameState);
    if (!PlayerChunk)
    {
        PlayerChunk = Game_ReserveChunk(GameState);
        if (PlayerChunk)
        {
            Stack[StackAt++] = PlayerChunk;
            PlayerChunk->P = PlayerChunkP;
        }
        else
        {
            // TODO: free up some memory
            return;
        }
    }
    else
    {
        if (!(PlayerChunk->Flags & CHUNK_STATE_GENERATED_BIT) ||
            !(PlayerChunk->Flags & CHUNK_STATE_MESHED_BIT) ||
            !(PlayerChunk->Flags & CHUNK_STATE_UPLOADED_BIT))
        {
            Stack[StackAt++] = PlayerChunk;
        }
    }

    chunk* CurrentChunk = PlayerChunk;
    for (u32 i = 1; i <= GenerationDistance; i++)
    {
        u32 Diameter = 2*i + 1;

        u32 CurrentCardinal = Cardinal_First;
        chunk* NextChunk = nullptr;

        /*
        * Walk to the next chunk in the current cardinal direction:
        * if it doesn't exist, reserve it and push it onto the stack so that it can get processed later.
        * If it does, check if it has been generated or meshed and push it onto the stack if it hasn't.
        */
        auto WalkChunk = [&]() -> bool
        {
            bool Result = false;

            NextChunk = CurrentChunk->Neighbors[CurrentCardinal];
            if (!NextChunk)
            {
                NextChunk = Game_ReserveChunk(GameState);
                if (NextChunk)
                {
                    NextChunk->P = CurrentChunk->P + CardinalDirections[CurrentCardinal];
                    CurrentChunk->Neighbors[CurrentCardinal] = NextChunk;
                    NextChunk->Neighbors[CardinalOpposite(CurrentCardinal)] = CurrentChunk;
                    
                    // Push chunk onto the stack, it'll need to be generated and meshed later
                    assert(StackAt < StackSize);
                    Stack[StackAt++] = NextChunk;

                    Result = true;
                }
            }
            else
            {
                if (!(NextChunk->Flags & CHUNK_STATE_GENERATED_BIT) ||
                    !(NextChunk->Flags & CHUNK_STATE_MESHED_BIT) ||
                    !(NextChunk->Flags & CHUNK_STATE_UPLOADED_BIT))
                {
                    assert(StackAt < StackSize);
                    Stack[StackAt++] = NextChunk;
                }
                Result = true;
            }

            CurrentChunk = NextChunk;
            return Result;
        };

        /* 
        * # = chunks that will be processed now
        * 0 = chunks that have been already processed
        * 1 = previous chunk
        * - = chunks that will be processed later
        */

        // Walk to the next ring (i)
        /* 
        * --#--
        * -010-
        * -000-
        * -000-
        * -----
        */
        if (!WalkChunk())
        {
            // TODO
            return;
        }
        chunk* RingFirst = CurrentChunk;
        
        /* ##1--
         * -000-
         * -000-
         * -000-
         * -----
         */
        CurrentCardinal = CardinalNext(CurrentCardinal);
        for (u32 j = 0; j < Diameter / 2; j++)
        {
            if (!WalkChunk())
            {
                // TODO
                return;
            }
        }

        /* 100--    000--    000-#
         * #000-    0000-    0000#
         * #000- => 0000- => 0000#
         * #000-    0000-    0000#
         * #----    1####    00001
         */

        for (u32 k = 0; k < 3; k++)
        {
            CurrentCardinal = CardinalNext(CurrentCardinal);
            for (u32 j = 0; j < Diameter - 1; j++)
            {
                if (!WalkChunk())
                {
                    return;
                }
            }
        }

        /* 000#1
         * 00000
         * 00000
         * 00000
         * 00000
         */
        CurrentCardinal = CardinalNext(CurrentCardinal);
        for (u32 j = 0; j < (Diameter / 2) - 1; j++)
        {
            if (!WalkChunk())
            {
                // TODO
                return;
            }
        }

        assert((CurrentChunk->P + CardinalDirections[North]) == RingFirst->P);
        CurrentChunk = RingFirst;
    }

    // Generate the chunk neighborhood.
    // This could be done during the initial walk, but the logic of that is already complicated enough.
    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk= Stack[i];
        Game_FindChunkNeighbors(GameState, Chunk);
    }

    // Limit the number of chunks that can be processed in a single frame so that we don't hitch
    constexpr u32 ProcessedChunkLimit = 4;
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
                Chunk_Generate(&GameState->Perlin, Chunk);
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

                std::vector<vertex> VertexData = Chunk_Mesh(Chunk);
                Chunk->Flags |= CHUNK_STATE_MESHED_BIT;

                Chunk->AllocationIndex = VB_Allocate(&GameState->Renderer->VB, (u32)VertexData.size());
                if (Chunk->AllocationIndex != INVALID_INDEX_U32)
                {
                    TIMED_BLOCK("Upload");

                    u64 Size = VertexData.size() * sizeof(vertex);
                    u64 Offset = VB_GetAllocationMemoryOffset(&GameState->Renderer->VB, Chunk->AllocationIndex);                    

                    if (StagingHeap_Copy(
                        &GameState->Renderer->StagingHeap,
                        GameState->Renderer->TransferQueue,
                        GameState->Renderer->TransferCmdBuffer,
                        Offset, GameState->Renderer->VB.Buffer,
                        Size, VertexData.data()))
                    {
                        Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT;
                    }
                    else
                    {
                        VB_Free(&GameState->Renderer->VB, Chunk->AllocationIndex);
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

static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    if (Input->EscapePressed)
    {
        Input->IsCursorEnabled = ToggleCursor();
    }
    if (Input->BacktickPressed)
    {
        GameState->Debug.IsDebuggingEnabled = !GameState->Debug.IsDebuggingEnabled;
    }

    // ImGui
    {
        // TODO: pass input
        ImGuiIO& IO = ImGui::GetIO();
        IO.DisplaySize = { (f32)GameState->Renderer->SwapchainSize.width, (f32)GameState->Renderer->SwapchainSize.height };
        IO.DeltaTime = (DeltaTime == 0.0f) ? 1000.0f : DeltaTime; // NOTE(boti): ImGui doesn't want 0 dt

        if (Input->IsCursorEnabled)
        {
            IO.MousePos = { Input->MouseP.x, Input->MouseP.y };
            for (u32 i = 0; i < MOUSE_ButtonCount; i++)
            {
                IO.MouseDown[i] = Input->MouseButtons[i];
            }
        }
        else
        {
            IO.MousePos = { -1.0f, -1.0f };
            for (u32 i = 0; i < MOUSE_ButtonCount; i++)
            {
                IO.MouseDown[i] = false;
            }

        }
        ImGui::NewFrame();
    }

    if (GameState->Debug.IsDebuggingEnabled)
    {
        ImGui::Begin("Debug");
        {
            ImGui::Text("FrameTime: %.2fms", 1000.0f*DeltaTime);
            ImGui::Text("FPS: %.1f", 1.0f / DeltaTime);
            ImGui::Checkbox("Hitboxes", &GameState->Debug.IsHitboxEnabled);
        }
        ImGui::End();

        ImGui::Begin("Memory");
        {
            ImGui::Text("RenderTarget: %lluMB / %lluMB (%.1f%%)\n",
                GameState->Renderer->RTHeap.HeapOffset >> 20,
                GameState->Renderer->RTHeap.HeapSize >> 20,
                100.0 * ((f64)GameState->Renderer->RTHeap.HeapOffset / (f64)GameState->Renderer->RTHeap.HeapSize));
            ImGui::Text("VertexBuffer: %lluMB / %lluMB (%.1f%%)\n",
                GameState->Renderer->VB.MemoryUsage >> 20,
                GameState->Renderer->VB.MemorySize >> 20,
                100.0 * GameState->Renderer->VB.MemoryUsage / GameState->Renderer->VB.MemorySize);

            ImGui::Text("Chunks: %u/%u\n", GameState->ChunkCount, GameState->MaxChunkCount);
        }
        ImGui::End();

        GlobalProfiler.DoGUI();
    }

    Game_LoadChunks(GameState);

    Game_PreUpdatePlayer(GameState, Input);

    constexpr f32 MinPhysicsResolution = 16.6667e-3f;

    f32 RemainingTime = DeltaTime;
    while (RemainingTime > 0.0f)
    {
        f32 dt = Min(RemainingTime, MinPhysicsResolution);
        Game_UpdatePlayer(GameState, Input, dt);
        RemainingTime -= dt;
    }
}

static void Game_PreUpdatePlayer(game_state* GameState, game_input* Input)
{
    constexpr f32 MouseTurnSpeed = 2.5e-3f;

    player* Player = &GameState->Player;

    if (!Input->IsCursorEnabled)
    {
        Player->Yaw -= Input->MouseDelta.x * MouseTurnSpeed;
        Player->Pitch -= Input->MouseDelta.y * MouseTurnSpeed;
    }
    constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
    Player->Pitch = Clamp(Player->Pitch, -CameraClamp, CameraClamp);
}

static void Game_UpdatePlayer(game_state* GameState, game_input* Input, f32 dt)
{
    TIMED_FUNCTION();
    
    player* Player = &GameState->Player;

    // Block breaking
    if (!Input->IsCursorEnabled)
    {
        vec3 Forward = Player_GetForward(Player);

        chunk* PlayerChunk = Game_FindPlayerChunk(GameState);
        constexpr f32 PlayerReach = 8.0f;

        vec3i VoxelP;
        int Direction = -1;
        if (Chunk_RayCast(PlayerChunk, Player->P, Forward, PlayerReach, &VoxelP, &Direction))
        {
            if ((VoxelP == Player->TargetBlock) && Input->MouseButtons[MOUSE_LEFT])
            {
                u16 VoxelType = Chunk_GetVoxelType(PlayerChunk, VoxelP.x, VoxelP.y, VoxelP.z);
                if (VoxelType == VOXEL_GROUND)
                {
                    if (Player->BreakTime < 0.0f)
                    {
                        Player->BreakTime = 0.0f;
                    }
                    else
                    {
                        Player->BreakTime += dt;
                    }
                
                    if (Player->BreakTime >= Player->BlockBreakTime)
                    {
                        Chunk_SetVoxelType(PlayerChunk, VOXEL_AIR, VoxelP.x, VoxelP.y, VoxelP.z);
                    }
                }
                
            }
            else
            {
                Player->BreakTime = -1.0f;
            }
            Player->TargetBlock = VoxelP;
        }
        else
        {
            Player->TargetBlock = { 100000, 100000, 100000 };
        }
    }
    

    vec3 Forward, Right;
    Player_GetHorizontalAxes(Player, Forward, Right);
    vec3 Up = { 0.0f, 0.0f, 1.0f };

    vec3 DesiredMoveDirection = {};
    if (Input->Forward)
    {
        DesiredMoveDirection += Forward;
    }
    if (Input->Back)
    {
        DesiredMoveDirection -= Forward;
    }
    if (Input->Right)
    {
        DesiredMoveDirection += Right;
    }
    if (Input->Left)
    {
        DesiredMoveDirection -= Right;
    }
    DesiredMoveDirection = SafeNormalize(DesiredMoveDirection);

    constexpr f32 WalkSpeed = 4.0f;
    constexpr f32 RunSpeed = 7.75f;
    
    vec3 Acceleration = {};
    if (Player->WasGroundedLastFrame)
    {
        // Walk/Run controls
        f32 DesiredSpeed = Input->LeftShift ? RunSpeed : WalkSpeed;

        vec3 DesiredVelocity = DesiredMoveDirection * DesiredSpeed;
        vec3 VelocityXY = { Player->Velocity.x, Player->Velocity.y, 0.0f };
        
        vec3 DiffV = DesiredVelocity - VelocityXY;

        constexpr f32 Accel = 10.0f;
        Acceleration += Accel * DiffV;

        if (Input->Space)
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
            Player->BobAmplitude = Lerp(0, Player->WalkBobAmplitude, t);
            Player->BobFrequency = Lerp(0, Player->WalkBobFrequency, t);
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
            f32 DesiredSpeed = Input->LeftShift ? RunSpeed : WalkSpeed;

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

        chunk* PlayerChunk = Game_FindPlayerChunk(GameState);
        assert(PlayerChunk);
        assert(
            PlayerChunk->Neighbors[East] &&
            PlayerChunk->Neighbors[North] &&
            PlayerChunk->Neighbors[West] &&
            PlayerChunk->Neighbors[South]);

        constexpr u32 AXIS_X = 0;
        constexpr u32 AXIS_Y = 1;
        constexpr u32 AXIS_Z = 2;

        auto ApplyMovement = [&PlayerChunk, &Player](vec3 dP, u32 Direction) -> f32
        {
            TIMED_FUNCTION();

            Player->P[Direction] += dP[Direction];

#if 1
            aabb PlayerAABBAbsolute = Player_GetAABB(Player);
#else
            aabb PlayerAABBAbsolute = (Direction == AXIS_Z) ? Player_GetAABB(Player) : Player_GetVerticalAABB(Player);
#endif
            vec3i ChunkP = (vec3i)(PlayerChunk->P * vec2i{ CHUNK_DIM_X, CHUNK_DIM_Y });

            // Relative coordinates 
            aabb PlayerAABB = 
            {
                PlayerAABBAbsolute.Min - (vec3)ChunkP,
                PlayerAABBAbsolute.Max - (vec3)ChunkP,
            };

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
                        u16 VoxelType = Chunk_GetVoxelType(PlayerChunk, x, y, z);
                        if (VoxelType != VOXEL_AIR)
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

static void Game_Render(game_state* GameState, f32 DeltaTime)
{
    TIMED_FUNCTION();

    vulkan_renderer* Renderer = GameState->Renderer;
    if (GameState->IsMinimized)
    {
        return;
    }
    if (GameState->NeedRendererResize)
    {
        if (!Renderer_ResizeRenderTargets(Renderer))
        {
            assert(!"Fatal error");
        }
        GameState->NeedRendererResize = false;
    }

    renderer_frame_params* FrameParams = Renderer_NewFrame(Renderer);

    FrameParams->Camera = Player_GetCamera(&GameState->Player);
    FrameParams->ViewTransform = FrameParams->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)FrameParams->Renderer->SwapchainSize.width / (f32)FrameParams->Renderer->SwapchainSize.height;
    FrameParams->ProjectionTransform = PerspectiveMat4(FrameParams->Camera.FieldOfView, AspectRatio, 0.005f, 8000.0f);

    Renderer_BeginRendering(FrameParams);

    Renderer_RenderChunks(FrameParams, GameState->ChunkCount, GameState->Chunks);

    Renderer_BeginImmediate(FrameParams);
    if (GameState->Debug.IsHitboxEnabled)
    {
        Renderer_ImmediateBoxOutline(FrameParams, Player_GetAABB(&GameState->Player), PackColor(0xFF, 0x00, 0x00));
        Renderer_ImmediateBoxOutline(FrameParams, Player_GetVerticalAABB(&GameState->Player), PackColor(0xFF, 0xFF, 0x00));
    }

    // Render selected block
    {
        chunk* PlayerChunk = Game_FindPlayerChunk(GameState);

        vec3 P = FrameParams->Camera.P;

        f32 SinYaw = Sin(FrameParams->Camera.Yaw);
        f32 CosYaw = Cos(FrameParams->Camera.Yaw);
        f32 SinPitch = Sin(FrameParams->Camera.Pitch);
        f32 CosPitch = Cos(FrameParams->Camera.Pitch);
        vec3 V = 
        {
            -SinYaw*CosPitch,
            CosYaw*CosPitch,
            SinPitch,
        };

        vec3i BoxP;
        int Dir;
        if (Chunk_RayCast(PlayerChunk, P, V, 8.0f, &BoxP, &Dir))
        {
            BoxP = BoxP + (vec3i)(PlayerChunk->P * vec2i{ CHUNK_DIM_X, CHUNK_DIM_Y });
            aabb Box = MakeAABB((vec3)BoxP, (vec3)(BoxP + vec3i{ 1, 1, 1 }));
            Renderer_ImmediateBoxOutline(FrameParams, Box, PackColor(0x00, 0x00, 0x00));
        }
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
        if (GameState->Player.BreakTime >= 0.0f)
        {
            constexpr f32 OutlineSize = 1.0f;
            constexpr f32 Height = 20.0f;
            constexpr f32 Width = 100.0f;
            constexpr f32 OffsetY = 200.0f;

            vec2 P0 = { CenterP.x - 0.5f * Width, ScreenExtent.y - OffsetY - 0.5f * Height }; // Upper-left
            vec2 P1 = P0 + vec2{Width, Height}; // Lower-right

            // Outline
            // Top
            Renderer_ImmediateRect2D(FrameParams, 
                vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
                vec2{ P1.x + OutlineSize, P0.y               },
                PackColor(0xFF, 0xFF, 0xFF));
            // Left
            Renderer_ImmediateRect2D(FrameParams, 
                vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
                vec2{ P0.x              , P1.y + OutlineSize },
                PackColor(0xFF, 0xFF, 0xFF));
            // Right
            Renderer_ImmediateRect2D(FrameParams, 
                vec2{ P1.x              , P0.y - OutlineSize },
                vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
                PackColor(0xFF, 0xFF, 0xFF));
            // Bottom
            Renderer_ImmediateRect2D(FrameParams, 
                vec2{ P0.x - OutlineSize, P1.y               },
                vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
                PackColor(0xFF, 0xFF, 0xFF));

            // Center
            f32 FillRatio = GameState->Player.BreakTime / GameState->Player.BlockBreakTime;
            f32 EndX = P0.x + FillRatio * Width;

            Renderer_ImmediateRect2D(FrameParams, P0, vec2{ EndX, P1.y }, PackColor(0xFF, 0x00, 0x00));
        }
    }
    Renderer_RenderImGui(FrameParams);

    Renderer_EndRendering(FrameParams);

    Renderer_SubmitFrame(Renderer, FrameParams);
}

static bool Game_InitImGui(game_state* GameState)
{
    bool Result = false;

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& IO = ImGui::GetIO();
    IO.BackendPlatformName = "Blokker";

    IO.ImeWindowHandle = nullptr; // TODO(boti): needed for IME positionion

    // TODO(boti): implement IO.KeyMap here

    s32 TexWidth, TexHeight;
    u8* TexData;
    IO.Fonts->GetTexDataAsAlpha8(&TexData, &TexWidth, &TexHeight);

    if (Renderer_CreateImGuiTexture(GameState->Renderer, (u32)TexWidth, (u32)TexHeight, TexData))
    {
        IO.Fonts->SetTexID((ImTextureID)(u64)GameState->Renderer->ImGuiTextureID);
        Result = true;
    }
    
    return Result;
}

bool Game_Initialize(game_state* GameState)
{
    if (!Renderer_Initialize(GameState->Renderer))
    {
        return false;
    }
    
    if (!Game_InitImGui(GameState))
    {
        return false;
    }

    // Place the player in the middle of the starting chunk
    GameState->Player.P = { (0.5f * CHUNK_DIM_X + 0.5f), 0.5f * CHUNK_DIM_Y + 0.5f, 100.0f };
    GameState->Player.CurrentFov = GameState->Player.DefaultFov;
    GameState->Player.TargetFov = GameState->Player.TargetFov;

    Perlin2_Init(&GameState->Perlin, 0);

    DebugPrint("Game init done.\n");
    return true;
}

void Game_UpdateAndRender(game_state* GameState, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    // Disable stepping if there was giant lag-spike
    // TODO: The physics step should subdivide the frame when dt gets too large
    if (DeltaTime > 0.4f)
    {
        DeltaTime = 0.0f;
    }

    Game_Update(GameState, Input, DeltaTime);
    Game_Render(GameState, DeltaTime);

#if 0
    DebugPrint("Chunks: %u/%u ", GameState->ChunkCount, GameState->MaxChunkCount);
    DebugPrint("[GPU: %lluMB / %lluMB (%.1f%%)]\n",
        GameState->VB.MemoryUsage >> 20,
        GameState->VB.MemorySize >> 20,
        100.0 * GameState->VB.MemoryUsage / GameState->VB.MemorySize);
#endif
}