#include "Game.hpp"

#include <Profiler.hpp>
#include <bmp.hpp>
#include <Float.hpp>

#include "Renderer/Renderer.cpp"

#include "Common.cpp"
#include "Random.cpp"
#include "Audio.cpp"
#include "Camera.cpp"
#include "Chunk.cpp"
#include "Shapes.cpp"
#include "Profiler.cpp"

#include "World.cpp"
#include "Player.cpp"

static bool Game_InitImGui(game_state* Game);

static void Game_Render(game_state* Game, game_io* IO);

static void Game_Update(game_state* Game, game_io* IO)
{
    TIMED_FUNCTION();

    if (IO->EscapePressed)
    {
        IO->IsCursorEnabled = Platform.ToggleCursor();
    }
    if (IO->BacktickPressed)
    {
        Game->World->Debug.IsDebuggingEnabled = !Game->World->Debug.IsDebuggingEnabled;
    }

#if 1
    // ImGui
    {
        // TODO: pass input
        ImGuiIO& ImIO = ImGui::GetIO();
        ImIO.DisplaySize = { (f32)Game->Renderer->SwapchainSize.width, (f32)Game->Renderer->SwapchainSize.height };
        ImIO.DeltaTime = (IO->DeltaTime == 0.0f) ? 1000.0f : IO->DeltaTime; // NOTE(boti): ImGui doesn't want 0 dt

        if (IO->IsCursorEnabled)
        {
            ImIO.MousePos = { IO->MouseP.x, IO->MouseP.y };
            for (u32 i = 0; i < MOUSE_ButtonCount; i++)
            {
                ImIO.MouseDown[i] = IO->MouseButtons[i];
            }
        }
        else
        {
            ImIO.MousePos = { -1.0f, -1.0f };
            for (u32 i = 0; i < MOUSE_ButtonCount; i++)
            {
                ImIO.MouseDown[i] = false;
            }

        }
        ImGui::NewFrame();
    }

    if (Game->World->Debug.IsDebuggingEnabled)
    {
        ImGui::Begin("Debug");
        {
            ImGui::Text("FrameTime: %.2fms", 1000.0f*IO->DeltaTime);
            ImGui::Text("FPS: %.1f", 1.0f / IO->DeltaTime);
            ImGui::Checkbox("Hitboxes", &Game->World->Debug.IsHitboxEnabled);
            ImGui::Text("PlayerP: { %.1f, %.1f, %.1f }", 
                        Game->World->Player.P.x, Game->World->Player.P.y, Game->World->Player.P.z);
            if (ImGui::Button("Reset player"))
            {
                ResetPlayer(Game->World);
            }

            ImGui::Checkbox("Debug camera", &Game->World->Debug.IsDebugCameraEnabled);
            ImGui::Text("DebugCameraP: { %.1f, %.1f, %.1f }",
                Game->World->Debug.DebugCamera.P.x,
                Game->World->Debug.DebugCamera.P.y,
                Game->World->Debug.DebugCamera.P.z);
            if (ImGui::Button("Teleport debug camera to player"))
            {
                Game->World->Debug.DebugCamera = GetCamera(&Game->World->Player);
            }
            if (ImGui::Button("Teleport player to debug camera"))
            {
                Game->World->Player.P = Game->World->Debug.DebugCamera.P;
            }

        }
        ImGui::End();

        ImGui::Begin("Memory");
        {
            ImGui::Text("Game: %lluMB / %lluMB (%.1f%%)\n",
                Game->PrimaryArena.Used >> 20,
                Game->PrimaryArena.Size >> 20,
                100.0 * ((f64)Game->PrimaryArena.Used / (f64)Game->PrimaryArena.Size));

            ImGui::Text("Temporary: %lluMB / %lluMB (%.1f%%)\n",
                Game->TransientArenaLastUsed >> 20,
                Game->TransientArena.Size >> 20,
                100.0 * ((f64)Game->TransientArenaLastUsed / (f64)Game->TransientArena.Size));
            ImGui::Text("Temporary (max): %lluMB / %lluMB (%.1f%%)\n",
                Game->TransientArenaMaxUsed >> 20,
                Game->TransientArena.Size >> 20,
                100.0 * ((f64)Game->TransientArenaMaxUsed / (f64)Game->TransientArena.Size));
            ImGui::Text("RenderTarget: %lluMB / %lluMB (%.1f%%)\n",
                Game->Renderer->RTHeap.HeapOffset >> 20,
                Game->Renderer->RTHeap.HeapSize >> 20,
                100.0 * ((f64)Game->Renderer->RTHeap.HeapOffset / (f64)Game->Renderer->RTHeap.HeapSize));
            ImGui::Text("VertexBuffer: %lluMB / %lluMB (%.1f%%)\n",
                Game->Renderer->VB.MemoryUsage >> 20,
                Game->Renderer->VB.MemorySize >> 20,
                100.0 * Game->Renderer->VB.MemoryUsage / Game->Renderer->VB.MemorySize);
        }
        ImGui::End();

        ImGui::Begin("Map");

        ImGui::Checkbox("Enable map view", &Game->World->MapView.IsEnabled);

        ImGui::End();

        //GlobalProfiler.DoGUI();
    }
#endif
    
    Game->World->FrameIndex = Game->FrameIndex;
    HandleInput(Game->World, IO);
    UpdateWorld(Game, Game->World, IO);
}

static void Game_Render(game_state* Game, game_io* IO)
{
    TIMED_FUNCTION();

    renderer* Renderer = Game->Renderer;
    if (IO->IsMinimized)
    {
#if 1
        // HACK: Call ImGui rendering here so that we don't crash on the next ImGui::NewFrame();
        ImGui::Render();
#endif
        return;
    }
    if (IO->NeedRendererResize)
    {
        if (!Renderer_ResizeRenderTargets(Renderer))
        {
            assert(!"Fatal error");
        }
        IO->NeedRendererResize = false;
    }

    renderer_frame_params* FrameParams = Renderer_NewFrame(Renderer, Game->FrameIndex);

    World_Render(Game->World, FrameParams);

    Renderer_SubmitFrame(Renderer, FrameParams);
}

static bool Game_InitImGui(game_state* Game)
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

    if (Renderer_CreateImGuiTexture(Game->Renderer, (u32)TexWidth, (u32)TexHeight, TexData))
    {
        IO.Fonts->SetTexID((ImTextureID)(u64)Game->Renderer->ImGuiTextureID);
        Result = true;
    }
    return Result;
}

static bool InitializeSounds(game_state* Game)
{
    bool Result = true;

    const char* SoundPaths[] = 
    {
        "sound/hit1.wav",
    };
    constexpr u32 SoundCount = CountOf(SoundPaths);

    for (u32 i = 0; i < SoundCount; i++)
    {
        Assert(Game->SoundCount != Game->MaxSoundCount);
        sound* Sound = Game->Sounds + Game->SoundCount++;

        u64 ArenaSave = Game->TransientArena.Used;

        buffer File = Platform.LoadEntireFile(SoundPaths[i], &Game->TransientArena);

        constexpr u32 RiffTag = 'FFIR';
        constexpr u32 WaveTag = 'EVAW';
        constexpr u32 FormatTag = ' tmf';
        constexpr u32 DataTag = 'atad';
        struct riff_chunk
        {
            u32 Tag;
            u32 Size;
            u8 Data[];
        };

        if (File.Size > 12)
        {
            u32 RiffTagInFile = *(u32*)File.Data;
            u32 WaveTagInFile = *(u32*)(File.Data + 8);
            Assert((RiffTagInFile == RiffTag) && (WaveTagInFile == WaveTag));

            u32 FileSize = *(u32*)(File.Data + 4);
            Assert(FileSize == File.Size - 8);

            riff_chunk* FormatChunk = nullptr;
            riff_chunk* DataChunk = nullptr;
            riff_chunk* It = (riff_chunk*)(File.Data + 12);

            while ((u8*)It != File.Data + File.Size)
            {
                Assert((It->Data - File.Data) + It->Size <= (s64)File.Size); // Check for data size corruption

                if (It->Tag == FormatTag)
                {
                    FormatChunk = It;
                }
                else if (It->Tag == DataTag)
                {
                    DataChunk = It;
                }
                else
                {
                    // Unknown chunk type
                }
                It = (riff_chunk*)(It->Data + It->Size);
            }


            if (FormatChunk && DataChunk)
            {
#pragma pack(push, 1)
                struct wave_format
                {
                    u16 Tag;
                    u16 ChannelCount;
                    u32 SamplesPerSec;
                    u32 BytesPerSec;
                    u16 BlockAlign;
                    u16 BitDepth;
                };
#pragma pack(pop)
                wave_format* Format = (wave_format*)FormatChunk->Data;

                // TODO(boti): resampling, format conversion
                Assert(Format->Tag == 1 && // PCM
                       Format->ChannelCount == 1 &&
                       Format->SamplesPerSec == 48000 &&
                       Format->BitDepth == 16);

                Sound->ChannelCount = Format->ChannelCount;
                Sound->SampleCount = DataChunk->Size / Format->BlockAlign;
                u32 Footprint = DataChunk->Size;

                Sound->SampleData = (u8*)PushSize(&Game->PrimaryArena, Footprint, CACHE_LINE_SIZE);
                if (Sound->SampleData)
                {
                    memcpy(Sound->SampleData, DataChunk->Data, Footprint);
                }
                else
                {
                    Assert(!"Failed to allocate sound memory");
                    Result = false;
                    break;
                }
            }
            else
            {
                Assert(!"No data and/or format chunk in .wav file");
                Result = false;
            }
        }
        else
        {
            Assert(!"Corrupt .wav file");
            Result = false;
        }

        Game->TransientArena.Used = ArenaSave;
    }

    return(Result);
}

static bool InitializeTextures(renderer* Renderer, memory_arena* Arena)
{
    bool Result = false;
    const char* TexturePaths[] = 
    {
        "texture/ground_side.bmp",
        "texture/ground_top.bmp",
        "texture/ground_bottom.bmp",
        "texture/stone_side.bmp",
        "texture/coal_side.bmp",
        "texture/iron_side.bmp",
        "texture/trunk_side.bmp",
        "texture/trunk_top.bmp",
        "texture/leaves_side.bmp",
    };
    constexpr u32 TextureCount = CountOf(TexturePaths);

    constexpr u32 TextureWidth = 16;
    constexpr u32 TextureHeight = 16;
    constexpr u32 TextureMipCount = 4;

    u32 TexelCountPerTexture = 0;
    for (u32 i = 0; i < TextureMipCount; i++)
    {
        TexelCountPerTexture += (TextureWidth >> i) * (TextureHeight >> i);
    }

    u32* PixelBuffer = PushArray<u32>(Arena, TexelCountPerTexture * TextureCount);
    u32* PixelBufferAt = PixelBuffer;
    for (u32 TextureIndex = 0; TextureIndex < TextureCount; TextureIndex++)
    {
        buffer BitmapBuffer = Platform.LoadEntireFile(TexturePaths[TextureIndex], Arena);
        if (BitmapBuffer.Data)
        {
            Assert(BitmapBuffer.Size >= sizeof(bmp_file));
            bmp_file* Bitmap = (bmp_file*)BitmapBuffer.Data;
                    
            if ((Bitmap->File.Tag == BMP_FILE_TAG) && 
                (Bitmap->File.Offset == offsetof(bmp_file, Data)) &&
                (Bitmap->Info.HeaderSize == sizeof(bmp_info_header)) &&
                (Bitmap->Info.Planes == 1) &&
                (Bitmap->Info.BitCount == 24) &&
                (Bitmap->Info.Compression == BMP_COMPRESSION_NONE))
            {
                Assert(((u32)Bitmap->Info.Width == TextureWidth) &&
                       ((u32)Abs(Bitmap->Info.Height) == TextureHeight));

                u32* ImageBase = PixelBufferAt;

                u8* Src = Bitmap->Data;
                for (u32 y = 0; y < TextureHeight; y++)
                {
                    for (u32 x = 0; x < TextureWidth; x++)
                    {
                        u8 B = *Src++;
                        u8 G = *Src++;
                        u8 R = *Src++;
                        *PixelBufferAt++ = PackColor(R, G, B);
                    }
                }

                u32* PrevMipLevel = ImageBase;
                for (u32 Mip = 1; Mip < TextureMipCount; Mip++)
                {
                    u32 PrevWidth = TextureWidth >> (Mip - 1);
                    u32 PrevHeight = TextureHeight >> (Mip - 1);
                    u32 CurrentWidth = PrevWidth >> 1;
                    u32 CurrentHeight = PrevHeight >> 1;
                    for (u32 y = 0; y < CurrentHeight; y++)
                    {
                        for (u32 x = 0; x < CurrentWidth; x++)
                        {
                            u32 Color00 = PrevMipLevel[(2*x + 0) + (2*y + 0)*PrevWidth];
                            u32 Color10 = PrevMipLevel[(2*x + 1) + (2*y + 0)*PrevWidth];
                            u32 Color01 = PrevMipLevel[(2*x + 0) + (2*y + 1)*PrevWidth];
                            u32 Color11 = PrevMipLevel[(2*x + 1) + (2*y + 1)*PrevWidth];

                            vec3 C00 = UnpackColor3(Color00);
                            vec3 C10 = UnpackColor3(Color10);
                            vec3 C01 = UnpackColor3(Color01);
                            vec3 C11 = UnpackColor3(Color11);

                            C00 = C00*C00;
                            C10 = C10*C10;
                            C01 = C01*C01;
                            C11 = C11*C11;

                            vec3 Color = 0.25f * (C00 + C10 + C01 + C11);
                            Color = { Sqrt(Color.x), Sqrt(Color.y), Sqrt(Color.z) };
                            *PixelBufferAt++ = PackColor(Color);
                        }
                    }

                    ImageBase += PrevWidth*PrevHeight;
                }
            }
            else
            {
                Assert(!"Unsupported bitmap format");
            }
        }
        else
        {
            Assert(!"Failed to load bitmap");
        }
    }

    Result = Renderer_CreateVoxelTextureArray(Renderer, 
                                              TextureWidth, TextureHeight, TextureMipCount,
                                              TextureCount, (u8*)PixelBuffer);
    return(Result);
}
platform_api Platform;

extern "C" void Game_UpdateAndRender(game_memory* Memory, game_io* IO)
{
    TIMED_FUNCTION();

    Platform = Memory->Platform;
    ImGui::SetAllocatorFunctions(Memory->ImGuiAlloc, Memory->ImGuiFree);
    ImGui::SetCurrentContext(Memory->ImGuiCtx);

    game_state* Game = Memory->Game;
    if (!Game)
    {
        u64 TransientMemorySize = MiB(512);
        u64 PermanentMemorySize = Memory->MemorySize - TransientMemorySize;
        memory_arena PermanentArena = InitializeArena(PermanentMemorySize, Memory->Memory);
        memory_arena TransientArena = InitializeArena(TransientMemorySize, (u8*)Memory->Memory + PermanentMemorySize);
        Game = Memory->Game = PushStruct<game_state>(&PermanentArena);

        bool InitializationSuccessful = false;
        if (Game)
        {
            Game->PrimaryArena = PermanentArena;
            Game->TransientArena = TransientArena;
            void* TransientMemory = PushSize(&Game->PrimaryArena, TransientMemorySize, KiB(64));
            if (TransientMemory)
            {
                Game->TransientArena = InitializeArena(TransientMemorySize, TransientMemory);

                if (InitializeSounds(Game))
                {
                    Game->Renderer = PushStruct<renderer>(&Game->PrimaryArena);
                    if (Game->Renderer)
                    {
                        if (Renderer_Initialize(Game->Renderer, &Game->PrimaryArena))
                        {
                            if (InitializeTextures(Game->Renderer, &Game->TransientArena))
                            {
                                if (Game_InitImGui(Game))
                                {
                                    InitializationSuccessful = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!InitializationSuccessful)
        {
            IO->ShouldQuit = true;
            return;
        }

        Game->HitSound = Game->Sounds + 0;
    }

    Game->TransientArenaMaxUsed = Max(Game->TransientArenaMaxUsed, Game->TransientArena.Used);
    Game->TransientArenaLastUsed = Game->TransientArena.Used;
    Game->TransientArena.Used = 0; // Reset temporary memory

    if (!Game->World)
    {
        Game->World = PushStruct<world>(&Game->PrimaryArena);
        Game->World->Arena = &Game->PrimaryArena;
        Game->World->Renderer = Game->Renderer;
        if (!Initialize(Game->World))
        {
            IO->ShouldQuit = true;
            return;
        }
    }

    // Disable stepping if there was giant lag-spike
    // TODO: The physics step should subdivide the frame when dt gets too large
    if (IO->DeltaTime > 0.4f)
    {
        IO->DeltaTime = 0.0f;
    }

    Game->FrameIndex = IO->FrameIndex;

    Game_Update(Game, IO);
    Game_Render(Game, IO);
}