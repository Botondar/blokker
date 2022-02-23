#pragma once 

#include <Common.hpp>
#include <Math.hpp>

struct perlin2
{
    static constexpr u32 TableCount = 256;
    u32 Permutation[TableCount];
    vec2 Gradients[TableCount];
};

void Perlin2_Init(perlin2* Perlin, u32 Seed);
f32 Perlin2_Sample(const perlin2* Perlin, vec2 P);
f32 Perlin2_Octave(const perlin2* Perlin, vec2 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);
f32 Perlin2_SampleUnilateral(const perlin2* Perlin, vec2 P);

struct perlin3
{
    static constexpr u32 TableCount = 256;
    u32 Seed;
    u32 Permutation[TableCount];
};

void Perlin3_Init(perlin3* Perlin, u32 Seed);
f32 Perlin3_Sample(const perlin3* Perlin, vec3 P);
f32 Perlin3_Octave(const perlin3* Perlin, vec3 P, u32 OctaveCount, f32 Persistence, f32 Lacunarity);