#include "Game.hpp"

#include <Profiler.hpp>
#include <Float.hpp>

#include "Common.cpp"
#include "Random.cpp"
#include "Audio.cpp"
#include "Camera.cpp"
#include "Chunk.cpp"
#include "Shapes.cpp"
#include "Profiler.cpp"

#include "World.cpp"
#include "Player.cpp"

//
// Internal interface
//
static bool InitializeSounds(game_state* Game);
static bool InitializeImGui(game_state* Game);
static bool InitializeTextures(memory_arena* Arena, renderer_init_info* RendererInfo);

static void DoDebugUI(game_state* Game, game_io* IO, render_frame* Frame);

//
// Implementation
//

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
            InitializeSounds(Game);
            InitializeImGui(Game);

            renderer_init_info RendererInfo = {};
            InitializeTextures(&Game->TransientArena, &RendererInfo);
            u8* ImGuiTextureData = nullptr;
            ImGui::GetIO().Fonts->GetTexDataAsAlpha8(&ImGuiTextureData,
                                                     (s32*)&RendererInfo.ImGuiTextureWidth,
                                                     (s32*)&RendererInfo.ImGuiTextureHeight);
            RendererInfo.ImGuiTextureData = (void*)ImGuiTextureData;

            Game->Renderer = CreateRenderer(&Game->PrimaryArena, &Game->TransientArena, &RendererInfo);
            if (Game->Renderer)
            {
                InitializationSuccessful = true;
            }
        }

        if (!InitializationSuccessful)
        {
            IO->ShouldQuit = true;
            return;
        }
    }
    
    Game->TransientArenaMaxUsed = Max(Game->TransientArenaMaxUsed, Game->TransientArena.Used);
    Game->TransientArenaLastUsed = Game->TransientArena.Used;
    ResetArena(&Game->TransientArena);

    if (!Game->World)
    {
        Game->World = PushStruct<world>(&Game->PrimaryArena);
        Game->World->Arena = &Game->PrimaryArena;
        if (!InitializeWorld(Game->World))
        {
            IO->ShouldQuit = true;
            return;
        }
    }

    if (IO->IsMinimized)
    {
        return;
    }

    // Disable stepping if there was giant lag-spike
    if (IO->DeltaTime > 0.4f)
    {
        IO->DeltaTime = 0.0f;
    }

    Game->FrameIndex = IO->FrameIndex;
    if (IO->EscapePressed)
    {
        IO->IsCursorEnabled = Platform.ToggleCursor();
    }

    render_frame* Frame = BeginRenderFrame(Game->Renderer, IO->NeedRendererResize);
    IO->NeedRendererResize = false;

    DoDebugUI(Game, IO, Frame);
    
    UpdateAndRenderWorld(Game, Game->World, IO, Frame);

    ImGui::Render();
    ImDrawData* DrawData = ImGui::GetDrawData();
    RenderImGui(Frame, DrawData);
    EndRenderFrame(Frame);
}

static void DoDebugUI(game_state* Game, game_io* IO, render_frame* Frame)
{
    {
        ImGuiIO& ImIO = ImGui::GetIO();
        ImIO.DisplaySize = { (f32)Frame->RenderExtent.x, (f32)Frame->RenderExtent.y };
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

    if (IO->BacktickPressed)
    {
        Game->IsDebugUIEnabled = !Game->IsDebugUIEnabled;
    }
    if (Game->IsDebugUIEnabled)
    {
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
#if 0
            ImGui::Text("RenderTarget: %lluMB / %lluMB (%.1f%%)\n",
                        Game->Renderer->RTHeap.HeapOffset >> 20,
                        Game->Renderer->RTHeap.HeapSize >> 20,
                        100.0 * ((f64)Game->Renderer->RTHeap.HeapOffset / (f64)Game->Renderer->RTHeap.HeapSize));
            ImGui::Text("VertexBuffer: %lluMB / %lluMB (%.1f%%)\n",
                        Game->Renderer->VB.MemoryUsage >> 20,
                        Game->Renderer->VB.MemorySize >> 20,
                        100.0 * Game->Renderer->VB.MemoryUsage / Game->Renderer->VB.MemorySize);
#endif
        }
        ImGui::End();

        ImGui::Begin("Debug");
        {
            ImGui::Text("FrameTime: %.2fms", 1000.0f*IO->DeltaTime);
            ImGui::Text("FPS: %.1f", 1.0f / IO->DeltaTime);
        }
        ImGui::End();
    }
}

static bool InitializeImGui(game_state* Game)
{
    bool Result = true;

    ImGui::StyleColorsDark();
    ImGuiIO& IO = ImGui::GetIO();
    IO.BackendPlatformName = "Blokker";

    IO.ImeWindowHandle = nullptr; // TODO(boti): needed for IME positionion

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

        memory_arena_checkpoint Checkpoint = ArenaCheckpoint(&Game->TransientArena);

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

        RestoreArena(&Game->TransientArena, Checkpoint);
    }

    if (Result)
    {
        Game->HitSound = Game->Sounds + 0;
    }

    return(Result);
}


constexpr u16 BMP_FILE_TAG = 'MB';

enum bmp_compression_type : u32
{
    BMP_COMPRESSION_NONE = 0,
};

#pragma pack(push, 1)
struct bmp_file_header
{
    u16 Tag;
    u32 FileSize;
    u16 Reserved1;
    u16 Reserved2;
    u32 Offset;
};

struct bmp_info_header
{
    u32 HeaderSize;
    s32 Width;
    s32 Height;
    u16 Planes;
    u16 BitCount;
    u32 Compression;
    u32 ImageSize;
    s32 PixelsPerMeterX;
    s32 PixelsPerMeterY;
    u32 ClrUsed;
    u32 ClrImportant;
};

struct bmp_file
{
    bmp_file_header File;
    bmp_info_header Info;
    u8 Data[];
};
#pragma pack(pop)

static bool InitializeTextures(memory_arena* Arena, renderer_init_info* RendererInfo)
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

    RendererInfo->TextureWidth = TextureWidth;
    RendererInfo->TextureHeight = TextureHeight;
    RendererInfo->TextureArrayCount = TextureCount;
    RendererInfo->TextureMipCount = TextureMipCount;
    RendererInfo->TextureData = (void*)PushArray<u32>(Arena, TexelCountPerTexture * TextureCount);
    u32* PixelBufferAt = (u32*)RendererInfo->TextureData;
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

    return(Result);
}