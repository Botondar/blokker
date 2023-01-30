#include "Random.hpp"
#include <Common.hpp>

static u32 XorShift32(u32 Seed)
{
    u32 Result = Seed;
    Result ^= Result << 13;
    Result ^= Result >> 17;
    Result ^= Result << 5;
    return(Result);
}

void Perlin2_Init(perlin2* Perlin, u32 Seed)
{
    assert(Perlin);

    for (u32 i = 0; i < perlin2::TableCount; i++)
    {
        Perlin->Permutation[i] = i;
    }

    for (u32 i = perlin2::TableCount - 1; i > 1; i--)
    {
        Seed = XorShift32(Seed);
        u32 j = Seed % (i + 1);
        {
            u32 Temp = Perlin->Permutation[i];
            Perlin->Permutation[i] = Perlin->Permutation[j];
            Perlin->Permutation[j] = Temp;
        }
    }
}

f32 SampleNoise(const perlin2* Perlin, vec2 P)
{
    vec2 LatticeP = Floor(P);
    vec2 P0 = P - LatticeP;
    vec2i Pi = { (s32)LatticeP.x, (s32)LatticeP.y };

    constexpr u32 Mask = perlin2::TableCount - 1;
    vec2 V[2][2] = 
    {
        { { P0.x,        P0.y }, { P0.x,        P0.y - 1.0f } },
        { { P0.x - 1.0f, P0.y }, { P0.x - 1.0f, P0.y - 1.0f } }, 
    };

    f32 GdotV[2][2];
    for (u32 x = 0; x < 2; x++)
    {
        for (u32 y = 0; y < 2; y++)
        {
            // Hash
            u32 Index = (((Pi.x + x) & Mask) + Perlin->Permutation[(Pi.y + y) & Mask]) & Mask;
            u32 Permutation = Perlin->Permutation[Index];

            switch (Permutation & 7u)
            {
                case 0: GdotV[x][y] = +V[x][y].x + V[x][y].y; break;
                case 1: GdotV[x][y] = -V[x][y].x + V[x][y].y; break;
                case 2: GdotV[x][y] = +V[x][y].x - V[x][y].y; break;
                case 3: GdotV[x][y] = -V[x][y].x - V[x][y].y; break;
                case 4: GdotV[x][y] = +V[x][y].y; break;
                case 5: GdotV[x][y] = -V[x][y].y; break;
                case 6: GdotV[x][y] = +V[x][y].x; break;
                case 7: GdotV[x][y] = -V[x][y].x; break;
            };
        }
    }

    vec2 Factor = { Fade5(P0.x), Fade5(P0.y), };

    f32 Result = Blerp(
        GdotV[0][0], GdotV[1][0],
        GdotV[0][1], GdotV[1][1],
        Factor);

    return Result;
}

f32 SampleOctave(const perlin2* Perlin, vec2 P0, u32 OctaveCount, f32 Persistence, f32 Lacunarity)
{
    f32 Result = 0.0f;
    f32 Amplitude = 1.0f;
    f32 Frequency = 1.0f;

#if 0
    mat2 DomainTransform = Identity2();
#else
    constexpr f32 C = 84.0f / 85.0f;
    constexpr f32 S = 13.0f / 85.0f;

    mat2 DomainTransform = Mat2(
        C, S,
        -S, C
    );
#endif
    for (u32 i = 0; i < OctaveCount; i++)
    {
        vec2 P = Frequency * P0;
        Result += Amplitude * SampleNoise(Perlin, P);

        Frequency *= Lacunarity;
        Amplitude *= Persistence;

        P0 = DomainTransform * P0;
    }
    return Result;
}

f32 SampleNoise01(const perlin2* Perlin, vec2 P)
{
    f32 Result = 0.5f * (SampleNoise(Perlin, P) + 1.0f);
    return Result;
}

void Perlin3_Init(perlin3* Perlin, u32 Seed)
{
    assert(Perlin);
    for (u32 i = 0; i < Perlin->TableCount; i++)
    {
        Perlin->Permutation[i] = i;
    }

    for (u32 i = Perlin->TableCount - 1; i > 1; i--)
    {
        Seed = XorShift32(Seed);
        u32 j = Seed % (i + 1);
        {
            u32 Temp = Perlin->Permutation[i];
            Perlin->Permutation[i] = Perlin->Permutation[j];
            Perlin->Permutation[j] = Temp;
        }
    }
}

f32 SampleNoise(const perlin3* Perlin, vec3 P)
{
#define PERLIN_PRECOMPUTE_GDOTV 1
    vec3 LatticeP = Floor(P);
    vec3 P0 = P - LatticeP;
    vec3i Pi = (vec3i)LatticeP;

    vec3 V[2][2][2] = 
    {
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
    };

    constexpr u32 Mask = perlin2::TableCount - 1;
#if PERLIN_PRECOMPUTE_GDOTV
    f32 GdotV[2][2][2];
#else
    vec3 G[2][2][2];
#endif
    for (u32 x = 0; x < 2; x++)
    {
        for (u32 y = 0; y < 2; y++)
        {
            for (u32 z = 0; z < 2; z++)
            {
                // Hash
                u32 IndexZ = Perlin->Permutation[(Pi.z + z) & Mask];
                u32 IndexY = Perlin->Permutation[((Pi.y + y) + IndexZ) & Mask];
                u32 IndexX = Perlin->Permutation[((Pi.x + x) + IndexY) & Mask];
                u32 Permutation = IndexX;

                // Extend the table size to 16 so that we can bitwise AND instead of mod
                switch (Permutation & 15u)
                {
#if PERLIN_PRECOMPUTE_GDOTV
                    case 12:
                    case 0:  GdotV[x][y][z] = +V[x][y][z].x + V[x][y][z].y; break;
                    case 13:
                    case 1:  GdotV[x][y][z] = -V[x][y][z].x + V[x][y][z].y; break;
                    case 14:
                    case 2:  GdotV[x][y][z] = +V[x][y][z].x - V[x][y][z].y; break;
                    case 15:
                    case 3:  GdotV[x][y][z] = -V[x][y][z].x - V[x][y][z].y; break;
                    case 4:  GdotV[x][y][z] = +V[x][y][z].x + V[x][y][z].z; break;
                    case 5:  GdotV[x][y][z] = -V[x][y][z].x + V[x][y][z].z; break;
                    case 6:  GdotV[x][y][z] = +V[x][y][z].x - V[x][y][z].z; break;
                    case 7:  GdotV[x][y][z] = -V[x][y][z].x - V[x][y][z].z; break;
                    case 8:  GdotV[x][y][z] = +V[x][y][z].y + V[x][y][z].z; break;
                    case 9:  GdotV[x][y][z] = -V[x][y][z].y + V[x][y][z].z; break;
                    case 10: GdotV[x][y][z] = +V[x][y][z].y - V[x][y][z].z; break;
                    case 11: GdotV[x][y][z] = -V[x][y][z].y - V[x][y][z].z; break;
#else
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
#endif
                }
            }
        }
    }

    vec3 Factor = { Fade5(P0.x), Fade5(P0.y), Fade5(P0.z) };

    f32 Result = Trilerp(
#if PERLIN_PRECOMPUTE_GDOTV
        GdotV[0][0][0], GdotV[1][0][0], GdotV[0][1][0], GdotV[1][1][0],
        GdotV[0][0][1], GdotV[1][0][1], GdotV[0][1][1], GdotV[1][1][1],
#else
        Dot(G[0][0][0], V[0][0][0]), Dot(G[1][0][0], V[1][0][0]), Dot(G[0][1][0], V[0][1][0]), Dot(G[1][1][0], V[1][1][0]),
        Dot(G[0][0][1], V[0][0][1]), Dot(G[1][0][1], V[1][0][1]), Dot(G[0][1][1], V[0][1][1]), Dot(G[1][1][1], V[1][1][1]),
#endif
        Factor);
    return Result;
#undef PERLIN_PRECOMPUTE_GDOTV
}

f32 OctaveNoise(const perlin3* Perlin, vec3 P0, u32 OctaveCount, f32 Persistence, f32 Lacunarity)
{
    f32 Result = 0.0f;

    f32 Amplitude = 1.0f;
    f32 Frequency = 1.0f;
    for (u32 i = 0; i < OctaveCount; i++)
    {
        vec3 P = Frequency * P0;
        Result += Amplitude * SampleNoise(Perlin, P);

        Frequency *= Lacunarity;
        Amplitude *= Persistence;
    }

    return Result;
}