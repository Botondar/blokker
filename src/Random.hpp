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
f32 Perlin2_Sample(const perlin2* Perlin, vec2 P);
f32 Perlin2_Octave(const perlin2* Perlin, vec2 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);
f32 Perlin2_SampleUnilateral(const perlin2* Perlin, vec2 P);

__m128 Perlin2_Sample(const perlin2* Perlin, __m128 x, __m128 y);

struct perlin3
{
    static constexpr u32 TableCount = 256;
    u32 Seed;
    u32 Permutation[TableCount];
};

void Perlin3_Init(perlin3* Perlin, u32 Seed);
f32 Perlin3_Sample(const perlin3* Perlin, vec3 P);
f32 Perlin3_Octave(const perlin3* Perlin, vec3 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);