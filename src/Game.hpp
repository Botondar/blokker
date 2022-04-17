#pragma once

#include <Common.hpp>
#include <Math.hpp>

#include <Renderer/Renderer.hpp>
#include <World.hpp>

#include <imgui/imgui.h>

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
    renderer* Renderer;

    // TODO: this maybe shouldn't be here
    bool NeedRendererResize;
    bool IsMinimized;

    u64 FrameIndex;

    world World;
};

bool Game_Initialize(game_state* GameState);
void Game_UpdateAndRender(
    game_state* GameState,
    game_input* Input, 
    f32 DeltaTime);