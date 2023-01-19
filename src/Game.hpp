#pragma once

#include <Common.hpp>
#include <Math.hpp>

#include <Platform.hpp>
#include <Renderer/Renderer.hpp>
#include <World.hpp>

#include <imgui/imgui.h>

extern platform_api Platform; // Global platform API (accessible from all .cpp files)

struct game_state
{
    memory_arena PrimaryArena;
    memory_arena TransientArena;

    u64 FrameIndex;

    renderer* Renderer;
    world* World;
};

bool Game_Initialize(game_memory* Memory);
void Game_UpdateAndRender(game_memory* Memory, game_io* IO);