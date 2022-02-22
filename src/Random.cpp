#include "Random.hpp"
#include <random>
#include <functional>

void Perlin2_Init(perlin2* Perlin, u32 Seed)
{
    std::mt19937 Twister(Seed);
    std::uniform_real_distribution<f32> Distribution(0.0f, 2.0f*PI);
    auto Dice = std::bind(Distribution, Twister);

    for (u32 i = 0; i < perlin2::TableCount; i++)
    {
        f32 Angle = Dice();
        Perlin->Gradients[i] = { Cos(Angle), Sin(Angle) };
        Perlin->Permutation[i] = i;
    }

    for (u32 i = perlin2::TableCount - 1; i > 1; i--)
    {
        u32 j = Twister() % (i + 1);
        {
            u32 Temp = Perlin->Permutation[i];
            Perlin->Permutation[i] = Perlin->Permutation[j];
            Perlin->Permutation[j] = Temp;
        }
    }
}

f32 Perlin2_Sample(const perlin2* Perlin, vec2 P)
{
    vec2 LatticeP = Floor(P);
    vec2 P0 = P - LatticeP;
    vec2i Pi = { (s32)LatticeP.x, (s32)LatticeP.y };

    constexpr u32 Mod = perlin2::TableCount;
    vec2 G[2][2];
    for (u32 x = 0; x < 2; x++)
    {
        for (u32 y = 0; y < 2; y++)
        {
            // Hash
            u32 Index = (((Pi.x + x) % Mod) + Perlin->Permutation[(Pi.y + y) % Mod]) % Mod;
            u32 Permutation = Perlin->Permutation[Index];
            G[x][y] = Perlin->Gradients[Permutation];
        }
    }

    vec2 V[2][2] = 
    {
        { { P0.x,        P0.y }, { P0.x,        P0.y - 1.0f } },
        { { P0.x - 1.0f, P0.y }, { P0.x - 1.0f, P0.y - 1.0f } }, 
    };

    vec2 Factor = { Fade5(P0.x), Fade5(P0.y), };

    f32 Result = Blerp(
        Dot(G[0][0], V[0][0]), Dot(G[1][0], V[1][0]),
        Dot(G[0][1], V[0][1]), Dot(G[1][1], V[1][1]),
        Factor);

    return Result;
}

f32 Perlin2_Octave(const perlin2* Perlin, vec2 P0, u32 OctaveCount)
{
    f32 Result = 0.0f;
    f32 Amplitude = 1.0f;
    f32 Frequency = 1.0f;
    for (u32 i = 0; i < OctaveCount; i++)
    {
        vec2 P = Frequency * P0;
        Result += Amplitude * Perlin2_Sample(Perlin, P);

        Frequency *= 2.0f;
        Amplitude *= 0.5f;
    }
    return Result;
}

f32 Perlin2_SampleUnilateral(const perlin2* Perlin, vec2 P)
{
    f32 Result = 0.5f * (Perlin2_Sample(Perlin, P) + 1.0f);
    return Result;
}