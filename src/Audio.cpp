#include "Audio.hpp"

static bool PlaySound(game_audio* Audio, sound* Sound)
{
    bool Result = false;
    if (Sound)
    {
        for (u32 SoundIndex = 0; SoundIndex < Audio->MaxPlayingSoundCount; SoundIndex++)
        {
            playing_sound* PlayingSound = Audio->PlayingSounds + SoundIndex;
            if (AtomicCompareExchange(&PlayingSound->IsValid, true, false) == false)
            {
                Result = true;
                AtomicExchange(&PlayingSound->SampleIndex, 0);
                AtomicExchangePointer((void**)&PlayingSound->Source, Sound);
                break;
            }
        }
    }
    return(Result);
}

extern "C" void Game_GetAudioSamples(game_memory* Memory, u32 SampleCount, audio_sample* Samples)
{
    if (Memory->Game)
    {
        game_audio* Audio = &Memory->Game->AudioState;

        for (u32 SoundIndex = 0; SoundIndex < Audio->MaxPlayingSoundCount; SoundIndex++)
        {
            playing_sound* Sound = Audio->PlayingSounds + SoundIndex;
            if (AtomicLoad(&Sound->IsValid))
            {
                if (Sound->Source)
                {
                    Assert(Sound->Source->ChannelCount == 1); // TODO

                    audio_sample* Dst = Samples;
                    for (u32 SampleIndex = 0; SampleIndex < SampleCount; SampleIndex++)
                    {
                        if (Sound->SampleIndex == Sound->Source->SampleCount)
                        {
                            AtomicExchange(&Sound->IsValid, false);
                            break;
                        }

                        s16 SourceSample = ((s16*)Sound->Source->SampleData)[Sound->SampleIndex++];
                        f32 Sample = SourceSample / 32768.0f;
                        Dst->Left += Sample;
                        Dst->Right += Sample;
                        Dst++;
                    }
                }
                else
                {
                    AtomicExchange(&Sound->IsValid, false);
                }
            }
        }
    }
}
