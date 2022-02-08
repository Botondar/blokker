#include "Game.hpp"

#include <Profiler.hpp>
#include <bmp.hpp>

#include <vector>

#include "Renderer.cpp"
#include "Math.cpp"
#include "Random.cpp"
#include "Common.cpp"
#include "Camera.cpp"
#include "Profiler.cpp"

static void Chunk_Generate(const perlin2* Perlin, chunk* Chunk);
static std::vector<vertex> Chunk_Mesh(const chunk* Chunk);

static chunk* Game_ReserveChunk(game_state* GameState);
static void Game_FindChunkNeighbors(game_state* GameState, chunk* Chunk);
static chunk* Game_FindPlayerChunk(game_state* GameState);
static void Game_LoadChunks(game_state* GameState);

static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime);
static void Game_Render(game_state* GameState, f32 DeltaTime);

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

                if (Chunk->Data->Voxels[z][y][x] == 0)
                {
                    continue;
                }
                else if(Chunk->Data->Voxels[z][y][x] == 1)
                {
                    static const vertex Cube[] = 
                    {
                        // EAST
                        { { 1.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0x00, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0xFF, 0x00, 0x00) },

                        // WEST
                        { { 0.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0x00, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0x00, 0xFF, 0xFF) },

                        // NORTH
                        { { 0.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 0.0f, 1.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0x00, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0xFF, 0x00, 0xFF) },

                        // SOUTH
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0x00, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0x00, 0xFF, 0x00) },

                        // TOP
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 0.0f, 1.0f, }, { 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 0.0f, 0.0f, 1.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 1.0f, 1.0f, 1.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0xFF, 0xFF) },
                        { { 0.0f, 1.0f, 1.0f, }, { 0.0f, 1.0f }, PackColor(0xFF, 0xFF, 0xFF) },

                        // BOTTOM
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 0.0f, 0.0f, }, { 1.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 0.0f, 0.0f, 0.0f, }, { 0.0f, 0.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 0.0f, 1.0f, 0.0f, }, { 0.0f, 1.0f }, PackColor(0xFF, 0xFF, 0x00) },
                        { { 1.0f, 1.0f, 0.0f, }, { 1.0f, 1.0f }, PackColor(0xFF, 0xFF, 0x00) },
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
                                VertexList.push_back(Vertex);
                            }
                        }
                    }
                }
                else
                {
                    assert(!"Invalid code path");
                }
            }
        }
    }

    return VertexList;
}

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
    vec2i PlayerChunkP = (vec2i)Floor(vec2{ GameState->Camera.P.x / CHUNK_DIM_X, GameState->Camera.P.y / CHUNK_DIM_Y });

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

    vec2 PlayerP = (vec2)GameState->Camera.P;
    vec2i PlayerChunkP = (vec2i)Floor(PlayerP / vec2{ (f32)CHUNK_DIM_X, (f32)CHUNK_DIM_Y });

    constexpr u32 MeshDistance = 4;
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

    // Generate the chunks in the stack
    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];

        if (!(Chunk->Flags & CHUNK_STATE_GENERATED_BIT))
        {
            Chunk_Generate(&GameState->Perlin, Chunk);
            Chunk->Flags |= CHUNK_STATE_GENERATED_BIT;
        }

        // TODO: this really isn't optimal
        Game_FindChunkNeighbors(GameState, Chunk);
    }

    // Mesh the chunks in the stack and upload them to the GPU
    for (u32 i = 0; i < StackAt; i++)
    {
        chunk* Chunk = Stack[i];
        s32 Distance = ChebyshevDistance(PlayerChunkP, Chunk->P);

        if (!(Chunk->Flags & CHUNK_STATE_MESHED_BIT) && (Distance <= MeshDistance))
        {
            assert(!(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT));

            std::vector<vertex> VertexData = Chunk_Mesh(Chunk);
            Chunk->Flags |= CHUNK_STATE_MESHED_BIT;

            Chunk->AllocationIndex = VB_Allocate(&GameState->VB, (u32)VertexData.size());
            if (Chunk->AllocationIndex != INVALID_INDEX_U32)
            {
                TIMED_BLOCK("Upload");

                u64 Size = VertexData.size() * sizeof(vertex);
                u64 Offset = VB_GetAllocationMemoryOffset(&GameState->VB, Chunk->AllocationIndex);                    

                if (StagingHeap_Copy(
                        &GameState->StagingHeap,
                        GameState->Renderer->TransferQueue,
                        GameState->Renderer->TransferCmdBuffer,
                        Offset, GameState->VB.Buffer,
                        Size, VertexData.data()))
                {
                    Chunk->Flags |= CHUNK_STATE_UPLOADED_BIT;
                }
                else
                {
                    VB_Free(&GameState->VB, Chunk->AllocationIndex);
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

static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    Game_LoadChunks(GameState);

    // Player update
    {
        TIMED_BLOCK("UpdatePlayer");
        constexpr f32 MoveSpeed = 2.5f;
        constexpr f32 MouseTurnSpeed = 5e-3f;

        GameState->Camera.Yaw -= Input->MouseDelta.x * MouseTurnSpeed;
        GameState->Camera.Pitch -= Input->MouseDelta.y * MouseTurnSpeed;

        constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
        GameState->Camera.Pitch = Clamp(GameState->Camera.Pitch, -CameraClamp, CameraClamp);

        mat4 Transform = GameState->Camera.GetTransform();
        vec3 Forward = TransformDirection(Transform, { 0.0f, 0.0f, 1.0f });
        vec3 Right   = TransformDirection(Transform, { 1.0f, 0.0f, 0.0f });
        vec3 Up      = TransformDirection(Transform, { 0.0f, -1.0f, 0.0f });

        vec3 MoveDirection = {};
        if (Input->Forward)
        {
            MoveDirection += Forward;
        }
        if (Input->Back)
        {
            MoveDirection -= Forward;
        }
        if (Input->Right)
        {
            MoveDirection += Right;
        }
        if (Input->Left)
        {
            MoveDirection -= Right;
        }

        if (Input->Space)
        {
            MoveDirection += vec3{ 0.0f, 0.0f, 1.0f };
        }
        if (Input->LeftControl)
        {
            MoveDirection -= vec3{ 0.0f, 0.0f, 1.0f };
        }

        f32 SpeedMul = 1.0f;
        if (Input->LeftShift)
        {
            SpeedMul *= 2.5f;
        }
        if (Input->LeftAlt)
        {
            SpeedMul *= 10.0f;
        }

        MoveDirection = SafeNormalize(MoveDirection);
        GameState->Camera.P += MoveDirection * MoveSpeed * SpeedMul * DeltaTime;
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

    
    // Init frame params
    u64 FrameIndex = Renderer->LatestFrameIndex;
    renderer_frame_params* FrameParams = Renderer->FrameParams + FrameIndex;
    FrameParams->FrameIndex = FrameIndex;
    u32 ImageIndex = INVALID_INDEX_U32;
    {
        TIMED_BLOCK("AcquireImage");
        
        vkWaitForFences(Renderer->Device, 1, &FrameParams->RenderFinishedFence, VK_TRUE, UINT64_MAX);
        vkResetFences(Renderer->Device, 1, &FrameParams->RenderFinishedFence);

        vkAcquireNextImageKHR(
            Renderer->Device, Renderer->Swapchain, 
            0, 
            FrameParams->ImageAcquiredSemaphore, 
            nullptr, 
            &ImageIndex);

        vkResetCommandPool(Renderer->Device, FrameParams->CmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }
    VkCommandBufferBeginInfo BeginInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(FrameParams->CmdBuffer, &BeginInfo);
    {
        TIMED_BLOCK("RecordCmdBuffer");

        VkImageMemoryBarrier BeginBarriers[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Renderer->SwapchainImages[ImageIndex],
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = FrameParams->DepthBuffer,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };
        constexpr u32 BeginBarrierCount = CountOf(BeginBarriers);

        vkCmdPipelineBarrier(FrameParams->CmdBuffer,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            BeginBarrierCount, BeginBarriers);

        VkRenderingAttachmentInfoKHR ColorAttachment = 
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .pNext = nullptr,
            .imageView = Renderer->SwapchainImageViews[ImageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue =  { .color = { 0.0f, 0.0f, 0.0f, 0.0f, }, },
        };
        VkRenderingAttachmentInfoKHR DepthAttachment = 
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .pNext = nullptr,
            .imageView = FrameParams->DepthBufferView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue =  { .depthStencil = { 1.0f, 0 }, },
        };
        VkRenderingAttachmentInfoKHR StencilAttachment = 
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .pNext = nullptr,
            .imageView = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = { .depthStencil = { 1.0f, 0 }, },
        };

        VkRenderingInfoKHR RenderingInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .renderArea = { { 0, 0 }, Renderer->SwapchainSize },
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &ColorAttachment,
            .pDepthAttachment = &DepthAttachment,
            .pStencilAttachment = &StencilAttachment,
        };

        vkCmdBeginRenderingKHR(FrameParams->CmdBuffer, &RenderingInfo);

        VkViewport Viewport = 
        {
            .x = 0.0f,
            .y = 0.0f,
            .width = (f32)Renderer->SwapchainSize.width,
            .height = (f32)Renderer->SwapchainSize.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D Scissor = 
        {
            .offset = { 0, 0 },
            .extent = Renderer->SwapchainSize,
        };

        vkCmdSetViewport(FrameParams->CmdBuffer, 0, 1, &Viewport);
        vkCmdSetScissor(FrameParams->CmdBuffer, 0, 1, &Scissor);

        vkCmdBindPipeline(FrameParams->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GameState->Pipeline);
        vkCmdBindDescriptorSets(FrameParams->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GameState->PipelineLayout, 
            0, 1, &GameState->DescriptorSet, 0, nullptr);

        VkDeviceSize Offset = 0;
        vkCmdBindVertexBuffers(FrameParams->CmdBuffer, 0, 1, &GameState->VB.Buffer, &Offset);

        mat4 ViewTransform = GameState->Camera.GetInverseTransform();

        const f32 AspectRatio = (f32)Renderer->SwapchainSize.width / (f32)Renderer->SwapchainSize.height;
        mat4 Projection = PerspectiveMat4(ToRadians(90.0f), AspectRatio, 0.01f, 8000.0f);
        mat4 VP = Projection * ViewTransform;

        for (u32 i = 0; i < GameState->ChunkCount; i++)
        {
            const chunk* Chunk = GameState->Chunks + i;
            if (!(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT))
            {
                continue;
            }

            mat4 WorldTransform = Mat4(
                1.0f, 0.0f, 0.0f, (f32)Chunk->P.x * CHUNK_DIM_X,
                0.0f, 1.0f, 0.0f, (f32)Chunk->P.y * CHUNK_DIM_Y,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
            mat4 Transform = VP * WorldTransform;
        
            vkCmdPushConstants(FrameParams->CmdBuffer, GameState->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 
                sizeof(mat4), &Transform);
        
            if (Chunk->AllocationIndex == INVALID_INDEX_U32)
            {
                continue;
            }
            vulkan_vertex_buffer_allocation Allocation = GameState->VB.Allocations[Chunk->AllocationIndex];
            if (Allocation.BlockIndex == INVALID_INDEX_U32)
            {
                continue;
            }
        
            vulkan_vertex_buffer_block Block = GameState->VB.Blocks[Allocation.BlockIndex];
            vkCmdDraw(FrameParams->CmdBuffer, Block.VertexCount, 1, Block.VertexOffset, 0);
        }
        vkCmdEndRenderingKHR(FrameParams->CmdBuffer);

        VkImageMemoryBarrier EndBarriers[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Renderer->SwapchainImages[ImageIndex],
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = FrameParams->DepthBuffer,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };
        constexpr u32 EndBarrierCount = CountOf(EndBarriers);

        vkCmdPipelineBarrier(FrameParams->CmdBuffer, 
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            EndBarrierCount, EndBarriers);
    }
    vkEndCommandBuffer(FrameParams->CmdBuffer);

    {
        TIMED_BLOCK("SubmitAndPresent");

        VkPipelineStageFlags WaitStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkSubmitInfo SubmitInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &FrameParams->ImageAcquiredSemaphore,
            .pWaitDstStageMask = &WaitStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &FrameParams->CmdBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &FrameParams->RenderFinishedSemaphore,
        };
        vkQueueSubmit(Renderer->GraphicsQueue, 1, &SubmitInfo, FrameParams->RenderFinishedFence);

        VkPresentInfoKHR PresentInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &FrameParams->RenderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &Renderer->Swapchain,
            .pImageIndices = &ImageIndex,
            .pResults = nullptr,
        };

        vkQueuePresentKHR(Renderer->GraphicsQueue, &PresentInfo);
    }

    Renderer->LatestFrameIndex = (Renderer->LatestFrameIndex + 1) % 2;
}

bool Game_Initialize(game_state* GameState)
{
    if (!Renderer_Initialize(GameState->Renderer))
    {
        return false;
    }

    if (!StagingHeap_Create(&GameState->StagingHeap, 256*1024*1024, GameState->Renderer))
    {
        return false;
    }

    // Vertex buffer
    if (!VB_Create(&GameState->VB, GameState->Renderer->DeviceLocalMemoryTypes, 1024*1024*1024, GameState->Renderer->Device))
    {
        return false;
    }

    // Descriptors
    {
        VkDevice Device = GameState->Renderer->Device;

        // Static sampler
        {
            VkSamplerCreateInfo SamplerInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .mipLodBias = 0.0f,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                .unnormalizedCoordinates = VK_FALSE,
            };
            VkSampler Sampler = VK_NULL_HANDLE;
            if (vkCreateSampler(Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
            {
                GameState->Sampler = Sampler;
            }
            else
            {
                return false;
            }
        }

        // Set layout
        {
            VkDescriptorSetLayoutBinding Bindings[] = 
            {
                {
                    .binding = 0, // TODO
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = nullptr,
                },
                {
                    .binding = 1, // TODO
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &GameState->Sampler,
                },
            };
            constexpr u32 BindingCount = CountOf(Bindings);

            VkDescriptorSetLayoutCreateInfo CreateInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = BindingCount,
                .pBindings = Bindings,
            };
            VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(Device, &CreateInfo, nullptr, &Layout) == VK_SUCCESS)
            {
                GameState->DescriptorSetLayout = Layout;
            }
            else
            {
                return false;
            }
        }

        // Descriptor pool + set
        {
            VkDescriptorPoolSize PoolSizes[] = 
            {
                { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, },
                { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, },
            };
            constexpr u32 PoolSizeCount = CountOf(PoolSizes);

            VkDescriptorPoolCreateInfo PoolInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = PoolSizeCount,
                .pPoolSizes = PoolSizes,
            };

            VkDescriptorPool Pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo AllocInfo =
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .descriptorPool = Pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &GameState->DescriptorSetLayout,
                };

                VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    GameState->DescriptorPool = Pool;
                    GameState->DescriptorSet = DescriptorSet;
                }
                else
                {
                    vkDestroyDescriptorPool(Device, Pool, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // Textures
    {
        struct image
        {
            u32 Width;
            u32 Height;
            VkFormat Format;
            u32* Pixels;
        };

        auto LoadBMP = [](const char* Path, image* Image) -> bool
        {
            bool Result = false;

            CBuffer Buffer = LoadEntireFile(Path);
            if (Buffer.Data && Buffer.Size)
            {
                bmp_file* Bitmap = (bmp_file*)Buffer.Data;
                if ((Bitmap->File.Tag == BMP_FILE_TAG) && (Bitmap->File.Offset == offsetof(bmp_file, Data)))
                {
                    if ((Bitmap->Info.HeaderSize == sizeof(bmp_info_header)) &&
                        (Bitmap->Info.Planes == 1) &&
                        (Bitmap->Info.BitCount == 24) &&
                        (Bitmap->Info.Compression == BMP_COMPRESSION_NONE))
                    {
                        Image->Width = (u32)Bitmap->Info.Width;
                        Image->Height = (u32)Abs(Bitmap->Info.Height); // TODO: flip on negative height
                        Image->Format = VK_FORMAT_R8G8B8A8_SRGB;

                        u32 PixelCount = Image->Width * Image->Height;
                        Image->Pixels = new u32[PixelCount];

                        if (Image->Pixels)
                        {
                            u8* Src = Bitmap->Data;
                            u32* Dest = Image->Pixels;

                            for (u32 i = 0; i < PixelCount; i++)
                            {
                                u8 B = *Src++;
                                u8 G = *Src++;
                                u8 R = *Src++;

                                *Dest++ = PackColor(R, G, B);
                            }

                            Result = true;
                        }
                    }
                }
            }

            return Result;
        };

        image Top, Side, Bottom;
        if (LoadBMP("texture/ground_top.bmp", &Top) &&
            LoadBMP("texture/ground_side.bmp", &Side) &&
            LoadBMP("texture/oak_log.bmp", &Bottom))
        {
            VkDevice Device = GameState->Renderer->Device;

            image* CurrentImage = &Bottom;
            VkImageCreateInfo CreateInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = CurrentImage->Format,
                .extent = { CurrentImage->Width, CurrentImage->Height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            VkImage Image = VK_NULL_HANDLE;
            if (vkCreateImage(Device, &CreateInfo, nullptr, &Image) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetImageMemoryRequirements(Device, Image, &MemoryRequirements);

                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryRequirements.memoryTypeBits & GameState->Renderer->DeviceLocalMemoryTypes))
                {

                    u64 MemorySize = CurrentImage->Width*CurrentImage->Height * sizeof(u32);
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = MemorySize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory;
                    if (vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindImageMemory(Device, Image, Memory, 0) == VK_SUCCESS)
                        {
                            if (StagingHeap_CopyImage(&GameState->StagingHeap, 
                                    GameState->Renderer->TransferQueue,
                                    GameState->Renderer->TransferCmdBuffer,
                                    Image, 
                                    CurrentImage->Width, CurrentImage->Height,
                                    CurrentImage->Format,
                                    CurrentImage->Pixels))
                            {
                                VkImageViewCreateInfo ViewInfo = 
                                {
                                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                    .pNext = nullptr,
                                    .flags = 0,
                                    .image = Image,
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                    .format = CurrentImage->Format,
                                    .components = {},
                                    .subresourceRange = 
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .baseMipLevel = 0,
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                };

                                VkImageView ImageView = VK_NULL_HANDLE;
                                if (vkCreateImageView(Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
                                {
                                    VkDescriptorImageInfo ImageInfo = 
                                    {
                                        .sampler = VK_NULL_HANDLE,
                                        .imageView = ImageView,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    };
                                    VkWriteDescriptorSet DescriptorWrite = 
                                    {
                                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                        .pNext = nullptr,
                                        .dstSet = GameState->DescriptorSet,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                        .pImageInfo = &ImageInfo,
                                        .pBufferInfo = nullptr,
                                        .pTexelBufferView = nullptr,
                                    };
                                    vkUpdateDescriptorSets(Device, 1, &DescriptorWrite, 0, nullptr);

                                    GameState->Tex = Image;
                                    GameState->TexView = ImageView;
                                    GameState->TexMemory = Memory;
                                }
                                else
                                {
                                    vkFreeMemory(Device, Memory, nullptr);
                                    vkDestroyImage(Device, Image, nullptr);
                                    return false;
                                }

                            }
                            else
                            {
                                vkFreeMemory(Device, Memory, nullptr);
                                vkDestroyImage(Device, Image, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Device, Memory, nullptr);
                            vkDestroyImage(Device, Image, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyImage(Device, Image, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyImage(Device, Image, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    // Debug visualizer
#if 0
    {
        VkBufferCreateInfo BufferInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = debug_visualizer::BufferSize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        VkBuffer Buffer = VK_NULL_HANDLE;
        if (vkCreateBuffer(GameState->Renderer->Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetBufferMemoryRequirements(GameState->Renderer->Device, Buffer, &MemoryRequirements);

            u32 MemoryTypes = GameState->Renderer->HostVisibleCoherentMemoryTypes;
            MemoryTypes &= MemoryRequirements.memoryTypeBits;

            u32 MemoryType = INVALID_INDEX_U32;
            if (BitScanForward(&MemoryType, MemoryTypes) != 0)
            {
                VkMemoryAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .allocationSize = debug_visualizer::BufferSize,
                    .memoryTypeIndex = MemoryType,
                };

                VkDeviceMemory Memory = VK_NULL_HANDLE;
                if (vkAllocateMemory(GameState->Renderer->Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                {
                    if (vkBindBufferMemory(GameState->Renderer->Device, Buffer, Memory, 0) == VK_SUCCESS)
                    {
                        void* Data = nullptr;
                        
                        if (vkMapMemory(GameState->Renderer->Device, Memory, 0, VK_WHOLE_SIZE, 0, &Data) == VK_SUCCESS)
                        {
                            GameState->DebugVisualizer = {};
                            GameState->DebugVisualizer.Memory = Memory;
                            GameState->DebugVisualizer.Buffer = Buffer;

                            GameState->DebugVisualizer.Data = Data;
                            GameState->DebugVisualizer.VertexData = (vertex*)Data;
                            u64 MaxVertexCount = debug_visualizer::BufferSize / sizeof(vertex);
                            assert(MaxVertexCount <= 0x7FFFFFFFF);
                            GameState->DebugVisualizer.MaxVertexCount = (u32)MaxVertexCount;
                            GameState->DebugVisualizer.CurrentFrameIndex = 0;
                        }
                        else
                        {
                            vkFreeMemory(GameState->Renderer->Device, Memory, nullptr);
                            vkDestroyBuffer(GameState->Renderer->Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkFreeMemory(GameState->Renderer->Device, Memory, nullptr);
                        vkDestroyBuffer(GameState->Renderer->Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(GameState->Renderer->Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                vkDestroyBuffer(GameState->Renderer->Device, Buffer, nullptr);
                return false;
            }
        }
        else
        {
            return false;
        }
    }
#endif

    {
        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &GameState->DescriptorSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            VkResult Result = vkCreatePipelineLayout(GameState->Renderer->Device, &Info, nullptr, &GameState->PipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            CBuffer VSBin = LoadEntireFile("shader/shader.vs");
            CBuffer FSBin = LoadEntireFile("shader/shader.fs");
            
            assert((VSBin.Size > 0) && (FSBin.Size > 0));
            
            VkShaderModuleCreateInfo VSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = VSBin.Size,
                .pCode = (u32*)VSBin.Data,
            };
            VkShaderModuleCreateInfo FSInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = FSBin.Size,
                .pCode = (u32*)FSBin.Data,
            };
            
            VkResult Result = VK_SUCCESS;
            Result = vkCreateShaderModule(GameState->Renderer->Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(GameState->Renderer->Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, P),
            },
            // UV
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(vertex, UV),
            },
            // Color
            {
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(vertex, Color),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = VK_STENCIL_OP_KEEP,
            .back = VK_STENCIL_OP_KEEP,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            GameState->Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = GameState->PipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        VkResult Result = vkCreateGraphicsPipelines(
            GameState->Renderer->Device, VK_NULL_HANDLE,
            1, &Info,
            nullptr, &GameState->Pipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(GameState->Renderer->Device, VSModule, nullptr);
        vkDestroyShaderModule(GameState->Renderer->Device, FSModule, nullptr);
    }

    GameState->Camera = {};
    // Place the player in the middle of the starting chunk
    GameState->Camera.P = { 0.5f * CHUNK_DIM_X + 0.5f, 0.5f * CHUNK_DIM_Y + 0.5f, 100.0f };

    Perlin2_Init(&GameState->Perlin, 0);

#if 0
    // NOTE: chunk::Data entries are permanently linked to ChunkData
    for (u32 i = 0; i < game_state::MaxChunkCount; i++)
    {
        GameState->Chunks[i].Data = GameState->ChunkData + i;
    }
#endif

    DebugPrint("Game init done.\n");
    return true;
}

void Game_UpdateAndRender(game_state* GameState, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

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