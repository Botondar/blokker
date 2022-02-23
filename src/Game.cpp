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

static u32 Game_HashChunkP(const game_state* GameState, vec2i P, vec2i* Coords = nullptr);

static void Game_LoadChunks(game_state* GameState);

static void Game_PreUpdatePlayer(game_state* GameState, game_input* Input);
static void Game_UpdatePlayer(game_state* GameState, game_input* Input, f32 DeltaTime);
static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime);

static void Game_Render(game_state* GameState, f32 DeltaTime);

static u32 Game_HashChunkP(const game_state* GameState, vec2i P, vec2i* Coords /*= nullptr*/)
{
    s32 ix = Modulo(P.x, GameState->MaxChunkCountSqrt);
    s32 iy = Modulo(P.y, GameState->MaxChunkCountSqrt);
    assert((ix >= 0) && (iy >= 0));

    u32 x = (u32)ix;
    u32 y = (u32)iy;

    u32 Result = x + y * GameState->MaxChunkCountSqrt;
    if (Coords)
    {
        Coords->x = ix;
        Coords->y = iy;
    }

    return Result;
}

static chunk* Game_GetChunkFromP(game_state* GameState, vec2i P)
{
    chunk* Result = nullptr;

    u32 Index = Game_HashChunkP(GameState, P);
    chunk* Chunk = GameState->Chunks + Index;
    if (Chunk->P == P)
    {
        Result = Chunk;
    }

    return Result;
}

static chunk* Game_GetChunkFromP(game_state* GameState, vec3i P, vec3i* RelP)
{
    chunk* Result = nullptr;

    vec2i ChunkP = { FloorDiv(P.x, CHUNK_DIM_X), FloorDiv(P.y, CHUNK_DIM_Y) };
    chunk* Chunk = Game_GetChunkFromP(GameState, ChunkP);
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

static u16 Game_GetVoxelType(game_state* GameState, vec3i P)
{
    u16 Result = VOXEL_AIR;

    if (0 <= P.z && P.z < CHUNK_DIM_Z)
    {
        vec3i RelP = {};
        chunk* Chunk = Game_GetChunkFromP(GameState, P, &RelP);

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

static bool Game_SetVoxelType(game_state* GameState, vec3i P, u16 Type)
{
    bool Result = false;

    vec3i RelP = {};
    chunk* Chunk = Game_GetChunkFromP(GameState, P, &RelP);

    if (Chunk && (Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
    {
        assert(Chunk->Data);
        if ((0 <= RelP.z) && (RelP.z < CHUNK_DIM_Z))
        {
            Chunk->Data->Voxels[RelP.z][RelP.y][RelP.x] = Type;
            Chunk->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;

            if (RelP.x == 0)
            {
                chunk* Neighbor = Game_GetChunkFromP(GameState, Chunk->P + CardinalDirections[West]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.x == CHUNK_DIM_X - 1)
            {
                chunk* Neighbor = Game_GetChunkFromP(GameState, Chunk->P + CardinalDirections[East]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            if (RelP.y == 0)
            {
                chunk* Neighbor = Game_GetChunkFromP(GameState, Chunk->P + CardinalDirections[South]);
                if (Neighbor && (Neighbor->Flags & CHUNK_STATE_MESHED_BIT))
                {
                    Neighbor->Flags |= CHUNK_STATE_MESH_DIRTY_BIT;
                }
            }
            else if (RelP.y == CHUNK_DIM_Y - 1)
            {
                chunk* Neighbor = Game_GetChunkFromP(GameState, Chunk->P + CardinalDirections[North]);
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

static chunk* Game_ReserveChunk(game_state* GameState, vec2i P)
{
    chunk* Result = nullptr;
    u32 Index = Game_HashChunkP(GameState, P);
    assert(Index < GameState->MaxChunkCount);

    Result = GameState->Chunks + Index;
    if (Result->P != P)
    {
        if (Result->Flags & CHUNK_STATE_GENERATED_BIT)
        {
            DebugPrint("WARNING: Evicting chunk { %d, %d }\n", Result->P.x, Result->P.y);

            if (Result->OldAllocationIndex != INVALID_INDEX_U32)
            {
                if (Result->OldAllocationIndex < GameState->FrameIndex - 1)
                {
                    VB_Free(&GameState->Renderer->VB, Result->OldAllocationIndex);
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

static chunk* Game_FindPlayerChunk(game_state* GameState)
{
    TIMED_FUNCTION();
    chunk* Result = nullptr;
    vec2i PlayerChunkP = (vec2i)Floor(vec2{ GameState->Player.P.x / CHUNK_DIM_X, GameState->Player.P.y / CHUNK_DIM_Y });

#if 1
    u32 Index = Game_HashChunkP(GameState, PlayerChunkP);
    chunk* Chunk = GameState->Chunks + Index;
    if (Chunk->P == PlayerChunkP)
    {
        Result = Chunk;
    }
#else
    for (u32 i = 0; i < GameState->ChunkCount; i++)
    {
        chunk* Chunk = GameState->Chunks + i;
        if (PlayerChunkP == Chunk->P)
        {
            Result = Chunk;
            break;
        }
    }
#endif
    return Result;
}

static bool Game_RayCast(
    game_state* GameState, 
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
                u16 Voxel = Game_GetVoxelType(GameState, vec3i{x, y, z});
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
static void Game_LoadChunks(game_state* GameState)
{
    TIMED_FUNCTION();

    vec2 PlayerP = (vec2)GameState->Player.P;
    vec2i PlayerChunkP = (vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_X, (f32)CHUNK_DIM_Y });

    constexpr u32 ImmediateMeshDistance = 1;
    constexpr u32 ImmediateGenerationDistance = ImmediateMeshDistance + 1;
#if BLOKKER_TINY_RENDER_DISTANCE
    constexpr u32 MeshDistance = 1;
#else
    constexpr u32 MeshDistance = 16;
#endif
    constexpr u32 GenerationDistance = MeshDistance + 1;

    // Create a stack that'll hold the chunks that haven't been meshed/generated around the player.
    constexpr u32 StackSize = (2*GenerationDistance + 1)*(2*GenerationDistance + 1);
    u32 StackAt = 0;
    chunk* Stack[StackSize];

    chunk* PlayerChunk = Game_FindPlayerChunk(GameState);
    if (!PlayerChunk)
    {
        PlayerChunk = Game_ReserveChunk(GameState, PlayerChunkP);
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
                chunk* Chunk = Game_GetChunkFromP(GameState, CurrentP);
                if (!Chunk)
                {
                    Chunk = Game_ReserveChunk(GameState, CurrentP);
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
                Chunk_Generate(Chunk, GameState);
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

                std::vector<vertex> VertexData = Chunk_BuildMesh(Chunk, GameState);
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
            ImGui::Text("PlayerP: { %.1f, %.1f, %.1f }", GameState->Player.P.x, GameState->Player.P.y, GameState->Player.P.z);
            if (ImGui::Button("Reset player"))
            {
                Game_ResetPlayer(GameState);
            }

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
            ImGui::Text("Chunk header size: %d bytes", sizeof(chunk));
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

    // Chunk update
    // TODO: move this
    {
        TIMED_BLOCK("ChunkUpdate");

        GameState->ChunkRenderDataCount = 0;

        for (u32 i = 0; i < GameState->MaxChunkCount; i++)
        {
            chunk* Chunk = GameState->Chunks + i;
            if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
            {
                continue;
            }

            if (Chunk->OldAllocationIndex != INVALID_INDEX_U32)
            {
                if (Chunk->OldAllocationLastRenderedInFrameIndex < GameState->FrameIndex - 1)
                {
                    VB_Free(&GameState->Renderer->VB, Chunk->OldAllocationIndex);
                    Chunk->OldAllocationIndex = INVALID_INDEX_U32;
                }
            }

            if ((Chunk->Flags & CHUNK_STATE_MESHED_BIT) &&
                (Chunk->Flags & CHUNK_STATE_MESH_DIRTY_BIT))
            {
                assert(Chunk->OldAllocationIndex == INVALID_INDEX_U32);

                Chunk->OldAllocationIndex = Chunk->AllocationIndex;
                Chunk->LastRenderedInFrameIndex = Chunk->OldAllocationLastRenderedInFrameIndex;

                std::vector<vertex> VertexData = Chunk_BuildMesh(Chunk, GameState);

                if (VertexData.size() && (VertexData.size() <= 0xFFFFFFFFu))
                {
                    Chunk->AllocationIndex = VB_Allocate(&GameState->Renderer->VB, (u32)VertexData.size());
                    u64 Offset = VB_GetAllocationMemoryOffset(&GameState->Renderer->VB, Chunk->AllocationIndex);
                    Chunk->LastRenderedInFrameIndex = 0;

                    u64 MemorySize = VertexData.size() * sizeof(vertex);
                    if (StagingHeap_Copy(&GameState->Renderer->StagingHeap,
                        GameState->Renderer->TransferQueue,
                        GameState->Renderer->TransferCmdBuffer,
                        Offset, GameState->Renderer->VB.Buffer,
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
                assert(GameState->ChunkRenderDataCount < GameState->MaxChunkCount);

                GameState->ChunkRenderData[GameState->ChunkRenderDataCount++] = 
                {
                    .P = Chunk->P,
                    .AllocationIndex = Chunk->AllocationIndex,
                    .LastRenderedInFrameIndex = &Chunk->LastRenderedInFrameIndex,
                };
            }
        }
    }
}

static void Game_ResetPlayer(game_state* GameState)
{
    player* Player = &GameState->Player;
    Player->Velocity = {};
    vec3i PlayerP = (vec3i)Floor(Player->P);
    Player->P.x = PlayerP.x + 0.5f;
    Player->P.y = PlayerP.y + 0.5f;
    
    vec3i RelP;
    chunk* Chunk = Game_GetChunkFromP(GameState, PlayerP, &RelP);
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

        vec3i OldTargetBlock = Player->TargetBlock;
        Player->HasTargetBlock = Game_RayCast(GameState, Player->P, Forward, PlayerReach, &Player->TargetBlock, &Player->TargetDirection);

        if (Player->HasTargetBlock)
        {
            // Breaking
            if ((OldTargetBlock == Player->TargetBlock) && Input->MouseButtons[MOUSE_LEFT])
            {
                u16 VoxelType = Game_GetVoxelType(GameState, Player->TargetBlock);
                const voxel_desc* VoxelDesc = &VoxelDescs[VoxelType];
                if (VoxelDesc->Flags & VOXEL_FLAGS_SOLID)
                {
                    Player->BreakTime += dt;
                    if (Player->BreakTime >= Player->BlockBreakTime)
                    {
                        Game_SetVoxelType(GameState, Player->TargetBlock, VOXEL_AIR);
                    }
                }
            }
            else
            {
                Player->BreakTime = 0.0f;
            }

            // Placing
            Player->TimeSinceLastBlockPlacement += dt;
            if (Input->MouseButtons[MOUSE_RIGHT] && (Player->TimeSinceLastBlockPlacement > Player->MaxBlockPlacementFrequency))
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
                u16 VoxelType = Game_GetVoxelType(GameState, PlacementP);
                if (VoxelType == VOXEL_AIR)
                {
                    aabb PlayerBox = Player_GetAABB(Player);
                    vec3i BoxP = PlacementP;
                    aabb BlockBox = MakeAABB((vec3)BoxP, (vec3)(BoxP + vec3i{1, 1, 1}));

                    vec3 Overlap;
                    int MinCoord;
                    if (!AABB_Intersect(PlayerBox, BlockBox, Overlap, MinCoord))
                    {
                        Game_SetVoxelType(GameState, PlacementP, VOXEL_GROUND);
                        Player->TimeSinceLastBlockPlacement = 0.0f;
                    }

                }
            }
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

        auto ApplyMovement = [&GameState, &Player](vec3 dP, u32 Direction) -> f32
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
                        u16 VoxelType = Game_GetVoxelType(GameState, vec3i{x, y, z});
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

    renderer_frame_params* FrameParams = Renderer_NewFrame(Renderer, GameState->FrameIndex);

    FrameParams->Camera = Player_GetCamera(&GameState->Player);
    FrameParams->ViewTransform = FrameParams->Camera.GetInverseTransform();

    const f32 AspectRatio = (f32)FrameParams->Renderer->SwapchainSize.width / (f32)FrameParams->Renderer->SwapchainSize.height;
    FrameParams->ProjectionTransform = PerspectiveMat4(FrameParams->Camera.FieldOfView, AspectRatio, 0.005f, 8000.0f);

    Renderer_BeginRendering(FrameParams);

    Renderer_RenderChunks(FrameParams, GameState->ChunkRenderDataCount, GameState->ChunkRenderData);

    Renderer_BeginImmediate(FrameParams);
    
    // Render selected block
    if (GameState->Player.HasTargetBlock)
    {
        vec3 P = (vec3)GameState->Player.TargetBlock;
        aabb Box = MakeAABB(P, P + vec3{ 1, 1, 1 });
        
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Box, PackColor(0x00, 0x00, 0x00));
    }

    if (GameState->Debug.IsHitboxEnabled)
    {
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Player_GetAABB(&GameState->Player), PackColor(0xFF, 0x00, 0x00));
        Renderer_ImmediateBoxOutline(FrameParams, 0.0025f, Player_GetVerticalAABB(&GameState->Player), PackColor(0xFF, 0xFF, 0x00));
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
        if (GameState->Player.HasTargetBlock && (GameState->Player.BreakTime > 0.0f))
        {
            constexpr f32 OutlineSize = 1.0f;
            constexpr f32 Height = 20.0f;
            constexpr f32 Width = 100.0f;
            constexpr f32 OffsetY = 200.0f;

            vec2 P0 = { CenterP.x - 0.5f * Width, ScreenExtent.y - OffsetY - 0.5f * Height }; // Upper-left
            vec2 P1 = P0 + vec2{Width, Height}; // Lower-right

            Renderer_ImmediateRectOutline2D(FrameParams, outline_type::Outer, OutlineSize, P0, P1, PackColor(0xFF, 0xFF, 0xFF));

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
    // HashChunkP test
    {
        DebugPrint("HashChunkP test: ");
        {
            vec2i StartP = { -10, -10 };
            vec2i EndP = {+10, +10};
            DebugPrint("Count = %u | SqrtCount = %u\n", GameState->MaxChunkCount, GameState->MaxChunkCountSqrt);
            DebugPrint("       ");
            for (s32 x = StartP.x; x < EndP.x; x++)
            {
                DebugPrint("%4d | ", x);
            }
            DebugPrint("\n");

            for (s32 y = StartP.y; y < EndP.y; y++)
            {
                DebugPrint("%4d | ", y);
                for (s32 x = StartP.x; x < EndP.x; x++)
                {
                    u32 Index = Game_HashChunkP(GameState, vec2i{x, y});
                    DebugPrint("%4d | ", Index);
                }
                DebugPrint("\n");
            }
            DebugPrint("===================================\n");

            DebugPrint("      ");
            for (s32 x = StartP.x; x < EndP.x; x++)
            {
                DebugPrint("%9d | ", x);
            }
            DebugPrint("\n");

            for (s32 y = StartP.y; y < EndP.y; y++)
            {
                DebugPrint("%3d | ", y);
                for (s32 x = StartP.x; x < EndP.x; x++)
                {
                    vec2i Coords;
                    u32 Index = Game_HashChunkP(GameState, vec2i{x, y}, &Coords);
                    DebugPrint("(%3d %3d) | ", Coords.x, Coords.y);
                }
                DebugPrint("\n");
            }
        }
    }

    if (!Renderer_Initialize(GameState->Renderer))
    {
        return false;
    }
    
    if (!Game_InitImGui(GameState))
    {
        return false;
    }

    // Init chunks
    for (u32 i = 0; i < GameState->MaxChunkCount; i++)
    {
        chunk* Chunk = GameState->Chunks + i;
        chunk_data* ChunkData = GameState->ChunkData + i;

        Chunk->AllocationIndex = INVALID_INDEX_U32;
        Chunk->OldAllocationIndex = INVALID_INDEX_U32;
        Chunk->Data = ChunkData;
    }

    // Place the player in the middle of the starting chunk
    GameState->Player.P = { (0.5f * CHUNK_DIM_X + 0.5f), 0.5f * CHUNK_DIM_Y + 0.5f, 100.0f };
    GameState->Player.CurrentFov = GameState->Player.DefaultFov;
    GameState->Player.TargetFov = GameState->Player.TargetFov;

    Perlin2_Init(&GameState->Perlin2, 0);
    Perlin3_Init(&GameState->Perlin3, 0);

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
}