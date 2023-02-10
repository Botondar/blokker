#pragma once

#include <Common.hpp>
#include <Math.hpp>

#include <Platform.hpp>
#include <Renderer/Renderer.hpp>
#include <Audio.hpp>
#include <World.hpp>

extern platform_api Platform; // Global platform API (accessible from all .cpp files)

struct game_state
{
    memory_arena PrimaryArena;
    memory_arena TransientArena;

    u64 FrameIndex;

    renderer* Renderer;
    world* World;

    game_audio AudioState;

    static constexpr u32 MaxSoundCount = 1 << 16;
    u32 SoundCount;
    sound Sounds[MaxSoundCount];

    sound* HitSound;
};