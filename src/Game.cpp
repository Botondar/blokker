#include "Game.hpp"

#include <Profiler.hpp>
#include <bmp.hpp>
#include <Float.hpp>

#include <vector>

#include "Renderer/Renderer.cpp"

#include "Math.cpp"
#include "Random.cpp"
#include "Common.cpp"
#include "Camera.cpp"
#include "Chunk.cpp"
#include "Shapes.cpp"
#include "Profiler.cpp"

#include "World.cpp"
#include "Player.cpp"

static bool Game_InitImGui(game_state* GameState);

static void Game_Render(game_state* GameState, f32 DeltaTime);

static void Game_Update(game_state* GameState, game_input* Input, f32 DeltaTime)
{
    TIMED_FUNCTION();

    if (Input->EscapePressed)
    {
        Input->IsCursorEnabled = ToggleCursor();
    }
    if (Input->BacktickPressed)
    {
        GameState->World.Debug.IsDebuggingEnabled = !GameState->World.Debug.IsDebuggingEnabled;
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

    if (GameState->World.Debug.IsDebuggingEnabled)
    {
        ImGui::Begin("Debug");
        {
            ImGui::Text("FrameTime: %.2fms", 1000.0f*DeltaTime);
            ImGui::Text("FPS: %.1f", 1.0f / DeltaTime);
            ImGui::Checkbox("Hitboxes", &GameState->World.Debug.IsHitboxEnabled);
            ImGui::Text("PlayerP: { %.1f, %.1f, %.1f }", GameState->World.Player.P.x, GameState->World.Player.P.y, GameState->World.Player.P.z);
            if (ImGui::Button("Reset player"))
            {
                World_ResetPlayer(&GameState->World);
            }

            ImGui::Checkbox("Debug camera", &GameState->World.Debug.IsDebugCameraEnabled);
            ImGui::Text("DebugCameraP: { %.1f, %.1f, %.1f }", 
                GameState->World.Debug.DebugCamera.P.x,
                GameState->World.Debug.DebugCamera.P.y,
                GameState->World.Debug.DebugCamera.P.z);
            if (ImGui::Button("Teleport debug camera to player"))
            {
                GameState->World.Debug.DebugCamera = Player_GetCamera(&GameState->World.Player);
            }
            if (ImGui::Button("Teleport player to debug camera"))
            {
                GameState->World.Player.P = GameState->World.Debug.DebugCamera.P;
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

            ImGui::Text("Chunks: %u/%u\n", GameState->World.ChunkCount, GameState->World.MaxChunkCount);
            ImGui::Text("Chunk header size: %d bytes", sizeof(chunk));
        }
        ImGui::End();

        ImGui::Begin("Map");

        ImGui::Checkbox("Enable map view", &GameState->World.MapView.IsEnabled);

        ImGui::End();

        GlobalProfiler.DoGUI();
    }

    GameState->World.FrameIndex = GameState->FrameIndex;
    World_Update(&GameState->World, Input, DeltaTime);
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

    World_Render(&GameState->World, FrameParams);

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

    GameState->World.Renderer = GameState->Renderer;

    if (!World_Initialize(&GameState->World))
    {
        return false;
    }
    
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