#pragma once

struct sound
{
    u32 ChannelCount;
    u32 SampleCount;
    u8* SampleData;
};

struct playing_sound
{
    b32 IsValid;
    u32 SampleIndex;
    sound* Source;
};

struct game_audio
{
    static constexpr u32 MaxPlayingSoundCount = 512;
    playing_sound PlayingSounds[MaxPlayingSoundCount];
};

static bool PlaySound(game_audio* Audio, sound* Sound);