#pragma once 

#include <Common.hpp>
#include <Math.hpp>
#include <Intrinsics.hpp>

struct perlin2
{
    static constexpr u32 TableCount = 256;
    u32 Permutation[TableCount];
};

void Perlin2_Init(perlin2* Perlin, u32 Seed);
f32 SampleNoise(const perlin2* Perlin, vec2 P);
f32 SampleOctave(const perlin2* Perlin, vec2 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);
f32 SampleNoise01(const perlin2* Perlin, vec2 P);

__m128 SampleNoise(const perlin2* Perlin, __m128 x, __m128 y);
__m128 SampleOctave(const perlin2* Perlin, __m128 x, __m128 y, u32 OctaveCount, f32 Persistence, f32 Lacunarity);

struct perlin3
{
    static constexpr u32 TableCount = 256;
    u32 Seed;
    u32 Permutation[TableCount];
};

void Perlin3_Init(perlin3* Perlin, u32 Seed);
f32 SampleNoise(const perlin3* Perlin, vec3 P);
f32 OctaveNoise(const perlin3* Perlin, vec3 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);