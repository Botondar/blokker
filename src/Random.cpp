#include "Random.hpp"
#include <random>
#include <functional>
#include <Common.hpp>

void Perlin2_Init(perlin2* Perlin, u32 Seed)
{
    assert(Perlin);

    std::mt19937 Twister(Seed);
    for (u32 i = 0; i < perlin2::TableCount; i++)
    {
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

__m128 Perlin2_Sample(const perlin2* Perlin, __m128 x, __m128 y)
{
    // Grid cell corner
    __m128 x0 = _mm_round_ps(x, _MM_FROUND_TO_NEG_INF|_MM_FROUND_NO_EXC);
    __m128 y0 = _mm_round_ps(y, _MM_FROUND_TO_NEG_INF|_MM_FROUND_NO_EXC);
    
    // Grid cell corner in integer
    __m128i ix = _mm_cvtps_epi32(x0);
    __m128i iy = _mm_cvtps_epi32(y0);

    // Relative coordinates inside the grid cell
    __m128 dx = _mm_sub_ps(x, x0);
    __m128 dy = _mm_sub_ps(y, y0);
    
    __m128i TableMask = _mm_set1_epi32(Perlin->TableCount - 1);

    __m128 GdotV[2][2];
    for (u32 ox = 0; ox < 2; ox++)
    {
        for (u32 oy = 0; oy < 2; oy++)
        {
            // Grid point
            __m128i iox4 = _mm_set1_epi32(ox);
            __m128i ioy4 = _mm_set1_epi32(oy);

            // Get permutation table value
            __m128i Idx0 = _mm_and_si128(TableMask, _mm_add_epi32(ioy4, iy));
            __m128i Idx1 = _mm_and_si128(TableMask, _mm_add_epi32(_mm_add_epi32(iox4, ix), _mm_i32gather_epi32((const int*)Perlin->Permutation, Idx0, 1)));
            __m128i Permutation = _mm_i32gather_epi32((const int*)Perlin->Permutation, Idx1, 1);
            __m128i Hash = _mm_and_si128(Permutation, _mm_set1_epi32(7));

            // Calculate the offset to the { ox, oy } grid point
            __m128 ox4 = _mm_cvtepi32_ps(iox4);
            __m128 oy4 = _mm_cvtepi32_ps(ioy4);

            __m128 dxCurrent = _mm_sub_ps(dx, ox4);
            __m128 dyCurrent = _mm_sub_ps(dy, oy4);

            // This is probably _really_ bad, but we don't have mask_add:
            // Calculate all the potential gradient values and use the Hash to determine which one to blend in
            // - too many calculations
            // - register pressure
#if 0
            case 0: GdotV[x][y] = +V[x][y].x + V[x][y].y; break;
            case 1: GdotV[x][y] = -V[x][y].x + V[x][y].y; break;
            case 2: GdotV[x][y] = +V[x][y].x - V[x][y].y; break;
            case 3: GdotV[x][y] = -V[x][y].x - V[x][y].y; break;
            case 4: GdotV[x][y] = +V[x][y].y; break;
            case 5: GdotV[x][y] = -V[x][y].y; break;
            case 6: GdotV[x][y] = +V[x][y].x; break;
            case 7: GdotV[x][y] = -V[x][y].x; break;
#endif
            __m128 GdV[8];
            GdV[0] = _mm_add_ps(dxCurrent, dyCurrent);
            GdV[1] = _mm_sub_ps(dyCurrent, dxCurrent);
            GdV[2] = _mm_sub_ps(dxCurrent, dyCurrent);
            GdV[3] = _mm_sub_ps(_mm_xor_ps(dxCurrent, _mm_set1_ps(-0.0)), dyCurrent); // XOR the sign bit to negate
            GdV[4] = dyCurrent;
            GdV[5] = _mm_xor_ps(dyCurrent, _mm_set1_ps(-0.0));
            GdV[6] = dxCurrent;
            GdV[7] = _mm_xor_ps(dxCurrent, _mm_set1_ps(-0.0));
            
            __m128i Mask[8];
            for (u32 i = 0; i < 8; i++)
            {
                Mask[i] = _mm_cmpeq_epi32(Hash, _mm_set1_epi32(i));
                GdotV[ox][oy] = _mm_blendv_ps(GdotV[ox][oy], GdV[i], _mm_castsi128_ps(Mask[i]));
            }
        }
    }
#if 0
    vec2 Factor = { Fade5(P0.x), Fade5(P0.y), };

    f32 Result = Blerp(
        GdotV[0][0], GdotV[1][0],
        GdotV[0][1], GdotV[1][1],
        Factor);
#endif

    __m128 u = _mm_fmadd_ps(dx, _mm_set1_ps(6.0f), _mm_set1_ps(-15.0f));
    u = _mm_fmadd_ps(dx, u, _mm_set1_ps(10.0f));
    u = _mm_mul_ps(dx, _mm_mul_ps(dx, _mm_mul_ps(u, dx)));
    __m128 v = _mm_fmadd_ps(dy, _mm_set1_ps(6.0f), _mm_set1_ps(-15.0f));
    v = _mm_fmadd_ps(dy, v, _mm_set1_ps(10.0f));
    v = _mm_mul_ps(dy, _mm_mul_ps(dy, _mm_mul_ps(u, dy)));

    __m128 s0 = Lerp(GdotV[0][0], GdotV[1][0], u);
    __m128 s1 = Lerp(GdotV[0][1], GdotV[1][1], u);
    __m128 Result = Lerp(s0, s1, v);
    return Result;
}

f32 Perlin2_Octave(const perlin2* Perlin, vec2 P0, u32 OctaveCount, f32 Persistence, f32 Lacunarity)
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
        Result += Amplitude * Perlin2_Sample(Perlin, P);

        Frequency *= Lacunarity;
        Amplitude *= Persistence;

        P0 = DomainTransform * P0;
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