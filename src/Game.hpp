#pragma once

#include <Common.hpp>
#include <Intrinsics.hpp>
#include <Math.hpp>
#include <Shapes.hpp>

#include <Platform.hpp>
#include <Renderer/RenderAPI.hpp>
#include <Audio.hpp>
#include <World.hpp>

extern platform_api Platform; // Global platform API (accessible from all .cpp files)

struct game_state
{
    memory_arena PrimaryArena;
    memory_arena TransientArena;

    u64 TransientArenaMaxUsed;
    u64 TransientArenaLastUsed;

    u64 FrameIndex;

    b32 IsDebugUIEnabled;

    renderer* Renderer;
    world* World;

    game_audio AudioState;

    static constexpr u32 MaxSoundCount = 1 << 16;
    u32 SoundCount;
    sound Sounds[MaxSoundCount];

    sound* HitSound;
};