#pragma once

#include <Common.hpp>
#include <Math.hpp>

#include <Platform.hpp>
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
    bool MouseButtons[MOUSE_ButtonCount];
    vec2 MouseP;
    vec2 MouseDelta;
    f32 WheelDelta;

    bool EscapePressed;
    bool BacktickPressed;
    bool MPressed;

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
    memory_arena PrimaryArena;
    memory_arena TransientArena;

    renderer* Renderer;

    // TODO: this maybe shouldn't be here
    bool NeedRendererResize;
    bool IsMinimized;

    u64 FrameIndex;

    world* World;
};

bool Game_Initialize(game_memory* Memory);
void Game_UpdateAndRender(
    game_memory* Memory,
    game_input* Input, 
    f32 DeltaTime);