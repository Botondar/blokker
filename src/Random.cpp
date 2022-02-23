#include "Random.hpp"
#include <random>
#include <functional>

void Perlin2_Init(perlin2* Perlin, u32 Seed)
{
    assert(Perlin);

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
#if 0
            switch (Permutation & 3u)
            {
                case 0: G[x][y] = { 0.0f, +1.0f }; break;
                case 1: G[x][y] = { 0.0f, -1.0f }; break;
                case 2: G[x][y] = { +1.0f, 0.0f }; break;
                case 3: G[x][y] = { -1.0f, 0.0f }; break;
            };
#elif 0
            switch (Permutation & 7u)
            {
                case 0: G[x][y] = { +1.0f, +1.0f }; break;
                case 1: G[x][y] = { -1.0f, +1.0f }; break;
                case 2: G[x][y] = { +1.0f, -1.0f }; break;
                case 3: G[x][y] = { -1.0f, -1.0f }; break;
                case 4: G[x][y] = { 0.0f, +1.0f }; break;
                case 5: G[x][y] = { 0.0f, -1.0f }; break;
                case 6: G[x][y] = { +1.0f, 0.0f }; break;
                case 7: G[x][y] = { -1.0f, 0.0f }; break;
            };
#else
            G[x][y] = Perlin->Gradients[Permutation];
#endif
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

f32 Perlin2_Octave(const perlin2* Perlin, vec2 P0, u32 OctaveCount, f32 Persistence, f32 Lacunarity)
{
    f32 Result = 0.0f;
    f32 Amplitude = 1.0f;
    f32 Frequency = 1.0f;
    for (u32 i = 0; i < OctaveCount; i++)
    {
        vec2 P = Frequency * P0;
        Result += Amplitude * Perlin2_Sample(Perlin, P);

        Frequency *= Lacunarity;
        Amplitude *= Persistence;
    }
    return Result;
}

f32 Perlin2_SampleUnilateral(const perlin2* Perlin, vec2 P)
{
    f32 Result = 0.5f * (Perlin2_Sample(Perlin, P) + 1.0f);
    return Result;
}

void Perlin3_Init(perlin3* Perlin, u32 Seed)
{
    assert(Perlin);

    std::mt19937 Twister(Seed);
    std::uniform_real_distribution<f32> Distribution(0.0f, 2.0f*PI);
    auto Dice = std::bind(Distribution, Twister);

    for (u32 i = 0; i < Perlin->TableCount; i++)
    {
        Perlin->Permutation[i] = i;
    }

    for (u32 i = Perlin->TableCount - 1; i > 1; i--)
    {
        u32 j = Twister() % (i + 1);
        {
            u32 Temp = Perlin->Permutation[i];
            Perlin->Permutation[i] = Perlin->Permutation[j];
            Perlin->Permutation[j] = Temp;
        }
    }
}

f32 Perlin3_Sample(const perlin3* Perlin, vec3 P)
{
    vec3 LatticeP = Floor(P);
    vec3 P0 = P - LatticeP;
    vec3i Pi = (vec3i)LatticeP;

    constexpr u32 Mod = perlin2::TableCount;
    vec3 G[2][2][2];
    for (u32 x = 0; x < 2; x++)
    {
        for (u32 y = 0; y < 2; y++)
        {
            for (u32 z = 0; z < 2; z++)
            {
                // Hash
                u32 IndexZ = Perlin->Permutation[(Pi.z + z) % Mod];
                u32 IndexY = Perlin->Permutation[((Pi.y + y) + IndexZ) % Mod];
                u32 IndexX = Perlin->Permutation[((Pi.x + x) + IndexY) % Mod];
                u32 Permutation = IndexX;

                switch (Permutation & 15u)
                {
                    // Extend the table size to 16 so that we can bitwise AND instead of mod
                    case 12:
                    case 0:  G[x][y][z] = { 1.0f, 1.0f, 0.0f }; break;
                    case 13:
                    case 1:  G[x][y][z] = { -1.0f, 1.0f, 0.0f }; break;
                    case 14:
                    case 2:  G[x][y][z] = { 1.0f, -1.0f, 0.0f }; break;
                    case 15:
                    case 3:  G[x][y][z] = { -1.0f, -1.0f, 0.0f }; break;
                    case 4:  G[x][y][z] = { 1.0f, 0.0f, 1.0f }; break;
                    case 5:  G[x][y][z] = { -1.0f, 0.0f, 1.0f }; break;
                    case 6:  G[x][y][z] = { 1.0f, 0.0f, -1.0f }; break;
                    case 7:  G[x][y][z] = { -1.0f, 0.0f, -1.0f }; break;
                    case 8:  G[x][y][z] = { 0.0f, 1.0f, 1.0f }; break;
                    case 9:  G[x][y][z] = { 0.0f, -1.0f, 1.0f }; break;
                    case 10: G[x][y][z] = { 0.0f, 1.0f, -1.0f }; break;
                    case 11: G[x][y][z] = { 0.0f, -1.0f, -1.0f }; break;
                }
            }
        }
    }

    vec3 V[2][2][2] = 
    {
#if 1
        // x = 0
        {
            // y = 0
            { { P0.x, P0.y, P0.z }, { P0.x, P0.y, P0.z - 1.0f }, },
            // y = 1
            { { P0.x, P0.y - 1.0f, P0.z }, { P0.x, P0.y - 1.0f, P0.z - 1.0f }, },
        },
        // x = 1
        {
            // y = 0
            { { P0.x - 1.0f, P0.y, P0.z }, { P0.x - 1.0f, P0.y, P0.z - 1.0f }, },
            // y = 1
            { { P0.x - 1.0f, P0.y - 1.0f, P0.z }, { P0.x - 1.0f, P0.y - 1.0f, P0.z - 1.0f }, },
        },
#else
        { { P0.x,        P0.y }, { P0.x,        P0.y - 1.0f } },
        { { P0.x - 1.0f, P0.y }, { P0.x - 1.0f, P0.y - 1.0f } }, 
#endif
    };

    vec3 Factor = { Fade5(P0.x), Fade5(P0.y), Fade5(P0.z) };
#if 1
    f32 Result = Trilerp(
        Dot(G[0][0][0], V[0][0][0]), Dot(G[1][0][0], V[1][0][0]), Dot(G[0][1][0], V[0][1][0]), Dot(G[1][1][0], V[1][1][0]),
        Dot(G[0][0][1], V[0][0][1]), Dot(G[1][0][1], V[1][0][1]), Dot(G[0][1][1], V[0][1][1]), Dot(G[1][1][1], V[1][1][1]),
        Factor);
#else
    f32 Result = Blerp(
        Dot(G[0][0], V[0][0]), Dot(G[1][0], V[1][0]),
        Dot(G[0][1], V[0][1]), Dot(G[1][1], V[1][1]),
        Factor);
#endif
    return Result;
}

f32 Perlin3_Octave(const perlin3* Perlin, vec3 P0, u32 OctaveCount, f32 Persistence, f32 Lacunarity)
{
    f32 Result = 0.0f;

    f32 Amplitude = 1.0f;
    f32 Frequency = 1.0f;
    for (u32 i = 0; i < OctaveCount; i++)
    {
        vec3 P = Frequency * P0;
        Result += Amplitude * Perlin3_Sample(Perlin, P);

        Frequency *= Lacunarity;
        Amplitude *= Persistence;
    }

    return Result;
}