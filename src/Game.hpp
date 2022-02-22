#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Random.hpp>
#include <Renderer.hpp>
#include <Camera.hpp>
#include <Chunk.hpp>

#include <imgui/imgui.h>

struct player
{
    vec3 P;
    vec3 Velocity;
    f32 Yaw, Pitch;
    bool WasGroundedLastFrame;

    static constexpr f32 Height = 1.8f;
    static constexpr f32 EyeHeight = 1.75f;
    static constexpr f32 LegHeight = 0.51f;
    static constexpr f32 Width = 0.6f;

    f32 HeadBob;
    f32 BobAmplitude = 0.05f;
    f32 BobFrequency = 2.0f;

    static constexpr f32 WalkBobAmplitude = 0.025f;
    static constexpr f32 WalkBobFrequency = 2.0f;

    static constexpr f32 RunBobAmplitude = 0.075f;
    static constexpr f32 RunBobFrequency = 2.5f;

    static constexpr f32 DefaultFov = ToRadians(80.0f);

    f32 FieldOfView;
    f32 CurrentFov;
    f32 TargetFov;

    static constexpr f32 BlockBreakTime = 0.5f;
    bool HasTargetBlock;
    vec3i TargetBlock;
    int TargetDirection;
    f32 BreakTime;

    static constexpr f32 MaxBlockPlacementFrequency = 0.2f;
    f32 TimeSinceLastBlockPlacement;
};

static camera Player_GetCamera(const player* Player);
static aabb Player_GetAABB(const player* Player);
static void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right);
static vec3 Player_GetForward(const player* Player);

enum mouse_button : u32
{
    MOUSE_LEFT = 0,
    MOUSE_RIGHT = 1,
    MOUSE_MIDDLE = 2,
    MOUSE_EXTRA0 = 3,
    MOUSE_EXTRA1 = 4,
    MOUSE_ButtonCount,
};

struct game_input
{
    bool IsCursorEnabled;
    vec2 MouseP;
    vec2 MouseDelta;
    bool MouseButtons[MOUSE_ButtonCount];

    bool EscapePressed;
    bool BacktickPressed;

    bool Forward;
    bool Back;
    bool Right;
    bool Left;

    bool Space;
    bool LeftShift;
    bool LeftControl;
    bool LeftAlt;
};

struct game_state
{
    vulkan_renderer* Renderer;

    // TODO: this maybe shouldn't be here
    bool NeedRendererResize;
    bool IsMinimized;

    struct 
    {
        bool IsDebuggingEnabled;
        bool IsHitboxEnabled;
    } Debug;

    u64 FrameIndex;
    player Player;

    perlin2 Perlin;

    static constexpr u32 MaxChunkCount = 16384;

    u32 ChunkCount;
    chunk* Chunks;
    chunk_data* ChunkData;
};

bool Game_Initialize(game_state* GameState);
void Game_UpdateAndRender(
    game_state* GameState,
    game_input* Input, 
    f32 DeltaTime);
