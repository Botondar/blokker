#pragma once 

#include <Common.hpp>
#include <Math.hpp>
#include <Intrinsics.hpp>

inline u32 XorShift32(u32 Seed)
{
    u32 Result = Seed;
    Result ^= Result << 13;
    Result ^= Result >> 17;
    Result ^= Result << 5;
    return(Result);
}

struct perlin2
{
    static constexpr u32 TableCount = 256;
    u32 Permutation[TableCount];
};

void Perlin2_Init(perlin2* Perlin, u32 Seed);
f32 SampleNoise(const perlin2* Perlin, vec2 P);
f32 SampleOctave(const perlin2* Perlin, vec2 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);
f32 SampleNoise01(const perlin2* Perlin, vec2 P);

struct perlin3
{
    static constexpr u32 TableCount = 256;
    u32 Seed;
    u32 Permutation[TableCount];
};

void Perlin3_Init(perlin3* Perlin, u32 Seed);
f32 SampleNoise(const perlin3* Perlin, vec3 P);
f32 OctaveNoise(const perlin3* Perlin, vec3 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);