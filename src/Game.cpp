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
#include "Profiler.cpp"

static chunk* Game_ReserveChunk(game_state* GameState);
static void Game_FindChunkNeighbors(game_state* GameState, chunk* Chunk);
static chunk* Game_FindPlayerChunk(game_state* GameState);

static void Game_LoadChunks(game_state* GameState);

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
    vec2i PlayerChunkP = (vec2i)Floor(vec2{ GameState->Player.Camera.P.x / CHUNK_DIM_X, GameState->Player.Camera.P.y / CHUNK_DIM_Y });

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

    vec2 PlayerP = (vec2)GameState->Player.Camera.P;
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
        }
        else
        {
            // TODO: free up some memory
            return;
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

    Game_LoadChunks(GameState);

    if (Input->EscapePressed)
    {
        Input->IsCursorEnabled = ToggleCursor();
    }

    Game_UpdatePlayer(GameState, Input, DeltaTime);
}

static void Game_UpdatePlayer(game_state* GameState, game_input* Input, f32 dt)
{
    TIMED_FUNCTION();
    constexpr f32 MouseTurnSpeed = 2.5e-3f;

    player* Player = &GameState->Player;

    if (!Input->IsCursorEnabled)
    {
        Player->Camera.Yaw -= Input->MouseDelta.x * MouseTurnSpeed;
        Player->Camera.Pitch -= Input->MouseDelta.y * MouseTurnSpeed;
    }
    constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
    Player->Camera.Pitch = Clamp(Player->Camera.Pitch, -CameraClamp, CameraClamp);

    // Camera axes in world space
    mat4 Transform = Player->Camera.GetTransform();
    vec3 Forward = TransformDirection(Transform, { 0.0f, 0.0f, 1.0f });
    vec3 Right   = TransformDirection(Transform, { 1.0f, 0.0f, 0.0f });
    vec3 Up      = TransformDirection(Transform, { 0.0f, -1.0f, 0.0f });

    // Project directions to the XY plane for movement
    Forward = Normalize(vec3{ Forward.x, Forward.y, 0.0f });
    Right = Normalize(vec3{ Right.x, Right.y, 0.0f });
    Up = { 0.0f, 0.0f, 1.0f };

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

        // Clear xy for now, we always move at a fixed speed
        Player->Velocity.x = 0.0f;
        Player->Velocity.y = 0.0f;
        
        f32 Speed = Input->LeftShift ? RunSpeed : WalkSpeed;
        vec3 dVelocity = DesiredMoveDirection * Speed;
        
        Player->Velocity += dVelocity;

        if (Input->Space)
        {
            constexpr f32 DesiredJumpHeight = 1.2f; // This is here just for reference
            constexpr f32 JumpVelocity = 7.7459667f; // sqrt(2 * Gravity * DesiredJumpHeight)
            Player->Velocity.z += JumpVelocity;
        }
    }
    else
    {
        // Duplicate walk code for now, but we'll want drag probably
        Player->Velocity.x = 0.0f;
        Player->Velocity.y = 0.0f;
        
        f32 Speed = Input->LeftShift ? RunSpeed : WalkSpeed;
        vec3 dVelocity = DesiredMoveDirection * Speed;
        
        Player->Velocity += dVelocity;
    }

    constexpr f32 Gravity = 25.0f;
    
    Acceleration += vec3{ 0.0f, 0.0f, -Gravity };
    Player->Velocity += Acceleration * dt;

    Player->Camera.P += (Player->Velocity + 0.5f * Acceleration * dt) * dt;
    Player->WasGroundedLastFrame = false;

    DebugPrint("Pv: { %.2f, %.2f, %.2f }\n", Player->Velocity.x, Player->Velocity.y, Player->Velocity.z);

    // Collision
    {
        TIMED_BLOCK("Collision");

        chunk* PlayerChunk = Game_FindPlayerChunk(GameState);
        assert(PlayerChunk);
        assert(
            PlayerChunk->Neighbors[East] &&
            PlayerChunk->Neighbors[North] &&
            PlayerChunk->Neighbors[West] &&
            PlayerChunk->Neighbors[South]);

        constexpr f32 PlayerHeight = 1.8f;
        constexpr f32 PlayerEyeHeight = 1.75f;
        constexpr f32 PlayerLegHeight = 0.51f;
        constexpr f32 PlayerWidth = 0.6f;

        vec3 PlayerAABBMin = 
        { 
            Player->Camera.P.x - 0.5f * PlayerWidth, 
            Player->Camera.P.y - 0.5f * PlayerWidth, 
            Player->Camera.P.z - PlayerEyeHeight 
        };
        vec3 PlayerAABBMax = 
        { 
            Player->Camera.P.x + 0.5f * PlayerWidth, 
            Player->Camera.P.y + 0.5f * PlayerWidth, 
            Player->Camera.P.z + (PlayerHeight - PlayerEyeHeight) 
        };

        vec3i ChunkP = (vec3i)(PlayerChunk->P * vec2i{ CHUNK_DIM_X, CHUNK_DIM_Y });

        // Relative coordinates 
        vec3 MinP = PlayerAABBMin - (vec3)ChunkP;
        vec3 MaxP = PlayerAABBMax - (vec3)ChunkP;

        vec3i MinPi = (vec3i)Floor(MinP);
        vec3i MaxPi = (vec3i)Ceil(MaxP);

        vec3 Displacement = {};
        vec3 PenetrationSigns = {};
        bool SkipCoords[3] = { false, false, false };

        bool IsCollision = false;
        for (s32 z = MinPi.z; z <= MaxPi.z; z++)
        {
            for (s32 y = MinPi.y; y <= MaxPi.y; y++)
            {
                for (s32 x = MinPi.x; x <= MaxPi.x; x++)
                {
                    u16 VoxelType = Chunk_GetVoxelType(PlayerChunk, x, y, z);
                    if (VoxelType == VOXEL_GROUND)
                    {
                        vec3 VoxelMinP = { (f32)x, (f32)y, (f32)z };
                        vec3 VoxelMaxP = { (f32)(x + 1), (f32)(y + 1), (f32)(z + 1) };

                        if ((MinP.x <= VoxelMaxP.x) && (VoxelMinP.x < MaxP.x) &&
                            (MinP.y <= VoxelMaxP.y) && (VoxelMinP.y < MaxP.y) &&
                            (MinP.z <= VoxelMaxP.z) && (VoxelMinP.z < MaxP.z))
                        {
                            IsCollision = true;
                            vec3 Penetration = {};

                            f32 MinPenetration = F32_MAX_NORMAL;
                            s32 MinCoord = -1;
                            for (s32 i = 0; i < 3; i++)
                            {
                                f32 AxisPenetration = (MaxP[i] < VoxelMaxP[i]) ? VoxelMinP[i] - MaxP[i] : VoxelMaxP[i] - MinP[i];

                                if (Abs(AxisPenetration) < Abs(MinPenetration))
                                {
                                    MinPenetration = AxisPenetration;
                                    MinCoord = i;
                                }
                                Penetration[i] = AxisPenetration;
                            }
                            assert(MinCoord != -1);

                            if ((Displacement[MinCoord] != 0.0f) && (ExtractSign(Displacement[MinCoord]) != ExtractSign(Penetration[MinCoord])))
                            {
                                SkipCoords[MinCoord] = true;
                            }
                            else
                            {
                                //vec3 Displacement = {};
                                //Displacement[MinCoord] = Penetration[MinCoord];
                                if (Abs(Displacement[MinCoord]) < Abs(Penetration[MinCoord]))
                                {
                                    Displacement[MinCoord] = Penetration[MinCoord];
                                }
                            }
                        }
                    }
                }
            }
        }

        if (IsCollision)
        {
            Player->Camera.P += Displacement;
#if 0
            constexpr f32 Epsilon = 1e-7f;
            vec3 Adjustment = { Signum(Displacement.x) * Epsilon, Signum(Displacement.y) * Epsilon, Signum(Displacement.z) * Epsilon };
            Player->Camera.P += Adjustment;
#endif
            
            // Clear velocities in the directions we collided
            for (s32 i = 0; i < 3; i++)
            {
                if (!SkipCoords[i])
                {
                    if (Displacement[i] != 0.0f)
                    {
                        Player->Velocity[i] = 0.0f;
                    }
                }
            }

            if (Displacement.z > 0.0f)
            {
                Player->WasGroundedLastFrame = true;
            }
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

        DebugPrint("RenderTarget MEM: %lluMB / %lluMB (%.1f%%)\n",
            Renderer->RTHeap.HeapOffset >> 20,
            Renderer->RTHeap.HeapSize >> 20,
            100.0 * ((f64)Renderer->RTHeap.HeapOffset / (f64)Renderer->RTHeap.HeapSize));
    }


#if 0
    DebugPrint("Chunks: %u/%u\n", GameState->ChunkCount, GameState->MaxChunkCount);
    DebugPrint("VB mem: %lluMB / %lluMB (%.1f%%)\n",
        GameState->VB.MemoryUsage >> 20,
        GameState->VB.MemorySize >> 20,
        100.0 * GameState->VB.MemoryUsage / GameState->VB.MemorySize);
#endif
    
    renderer_frame_params* FrameParams = Renderer_NewFrame(Renderer);
    FrameParams->Camera = GameState->Player.Camera;

    Renderer_BeginRendering(FrameParams);
    Renderer_RenderChunks(FrameParams, GameState->ChunkCount, GameState->Chunks);
    Renderer_EndRendering(FrameParams);

    Renderer_SubmitFrame(Renderer, FrameParams);
}

bool Game_Initialize(game_state* GameState)
{
    if (!Renderer_Initialize(GameState->Renderer))
    {
        return false;
    }
    
    GameState->Player.Camera = {};
    // Place the player in the middle of the starting chunk
    GameState->Player.Camera.P = { 0.5f * CHUNK_DIM_X + 0.5f, 0.5f * CHUNK_DIM_Y + 0.5f, 100.0f };

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