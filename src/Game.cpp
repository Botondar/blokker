#include "Game.hpp"

#include <Profiler.hpp>
#include <bmp.hpp>
#include <Float.hpp>

#include "Renderer/Renderer.cpp"

#include "Math.cpp"
#include "Random.cpp"
#include "Common.cpp"
#include "Memory.cpp"
#include "Camera.cpp"
#include "Chunk.cpp"
#include "Shapes.cpp"
#include "Profiler.cpp"

#include "World.cpp"
#include "Player.cpp"

static bool Game_InitImGui(game_state* Game);

static void Game_Render(game_state* Game, f32 DeltaTime);

static void Game_Update(game_state* Game, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    if (Input->EscapePressed)
    {
        Input->IsCursorEnabled = ToggleCursor();
    }
    if (Input->BacktickPressed)
    {
        Game->World->Debug.IsDebuggingEnabled = !Game->World->Debug.IsDebuggingEnabled;
    }

    // ImGui
    {
        // TODO: pass input
        ImGuiIO& IO = ImGui::GetIO();
        IO.DisplaySize = { (f32)Game->Renderer->SwapchainSize.width, (f32)Game->Renderer->SwapchainSize.height };
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

    if (Game->World->Debug.IsDebuggingEnabled)
    {
        ImGui::Begin("Debug");
        {
            ImGui::Text("FrameTime: %.2fms", 1000.0f*DeltaTime);
            ImGui::Text("FPS: %.1f", 1.0f / DeltaTime);
            ImGui::Checkbox("Hitboxes", &Game->World->Debug.IsHitboxEnabled);
            ImGui::Text("PlayerP: { %.1f, %.1f, %.1f }", 
                        Game->World->Player.P.x, Game->World->Player.P.y, Game->World->Player.P.z);
            if (ImGui::Button("Reset player"))
            {
                World_ResetPlayer(Game->World);
            }

            ImGui::Checkbox("Debug camera", &Game->World->Debug.IsDebugCameraEnabled);
            ImGui::Text("DebugCameraP: { %.1f, %.1f, %.1f }", 
                Game->World->Debug.DebugCamera.P.x,
                Game->World->Debug.DebugCamera.P.y,
                Game->World->Debug.DebugCamera.P.z);
            if (ImGui::Button("Teleport debug camera to player"))
            {
                Game->World->Debug.DebugCamera = Player_GetCamera(&Game->World->Player);
            }
            if (ImGui::Button("Teleport player to debug camera"))
            {
                Game->World->Player.P = Game->World->Debug.DebugCamera.P;
            }

        }
        ImGui::End();

        ImGui::Begin("Memory");
        {
            ImGui::Text("RenderTarget: %lluMB / %lluMB (%.1f%%)\n",
                Game->Renderer->RTHeap.HeapOffset >> 20,
                Game->Renderer->RTHeap.HeapSize >> 20,
                100.0 * ((f64)Game->Renderer->RTHeap.HeapOffset / (f64)Game->Renderer->RTHeap.HeapSize));
            ImGui::Text("VertexBuffer: %lluMB / %lluMB (%.1f%%)\n",
                Game->Renderer->VB.MemoryUsage >> 20,
                Game->Renderer->VB.MemorySize >> 20,
                100.0 * Game->Renderer->VB.MemoryUsage / Game->Renderer->VB.MemorySize);

            ImGui::Text("Chunks: %u/%u\n", Game->World->ChunkCount, Game->World->MaxChunkCount);
            ImGui::Text("Chunk header size: %d bytes", sizeof(chunk));
        }
        ImGui::End();

        ImGui::Begin("Map");

        ImGui::Checkbox("Enable map view", &Game->World->MapView.IsEnabled);

        ImGui::End();

        GlobalProfiler.DoGUI();
    }

    Game->World->FrameIndex = Game->FrameIndex;

    Game->TransientArena.Used = 0; // Reset temporary memory
    World_HandleInput(Game->World, Input, DeltaTime);

    World_Update(Game->World, Input, DeltaTime, &Game->TransientArena);
}

static void Game_Render(game_state* GameState, f32 DeltaTime)
{
    TIMED_FUNCTION();

    renderer* Renderer = GameState->Renderer;
    if (GameState->IsMinimized)
    {
        // HACK: Call ImGui rendering here so that we don't crash on the next ImGui::NewFrame();
        ImGui::Render();
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

    World_Render(GameState->World, FrameParams);

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

bool Game_Initialize(game_memory* Memory)
{
    game_state* Game = nullptr;
    {
        memory_arena BootstrapArena = InitializeArena(Memory->MemorySize, Memory->Memory);
        Game = Memory->Game = PushStruct<game_state>(&BootstrapArena);
        if (Game)
        {
            Game->PrimaryArena = BootstrapArena;
            u64 TransientMemorySize = MiB(512);
            void* TransientMemory = PushSize(&Game->PrimaryArena, TransientMemorySize, KiB(64));
            if (TransientMemory)
            {
                Game->TransientArena = InitializeArena(TransientMemorySize, TransientMemory);
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

    Game->Renderer = PushStruct<renderer>(&Game->PrimaryArena);
    if (Game->Renderer)
    {
        if (!Renderer_Initialize(Game->Renderer, &Game->TransientArena))
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    
    if (!Game_InitImGui(Game))
    {
        return false;
    }

    Game->World = PushStruct<world>(&Game->PrimaryArena);
    Game->World->Arena = &Game->PrimaryArena;
    Game->World->Renderer = Game->Renderer;
    if (!World_Initialize(Game->World))
    {
        return false;
    }
    
    DebugPrint("Game init done.\n");
    return true;
}

void Game_UpdateAndRender(game_memory* Memory, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    // Disable stepping if there was giant lag-spike
    // TODO: The physics step should subdivide the frame when dt gets too large
    if (DeltaTime > 0.4f)
    {
        DeltaTime = 0.0f;
    }

    game_state* Game = Memory->Game;
    Game_Update(Game, Input, DeltaTime);
    Game_Render(Game, DeltaTime);
}