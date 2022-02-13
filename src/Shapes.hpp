#pragma once

#include <Common.hpp>
#include <Math.hpp>

struct aabb
{
    vec3 Min;
    vec3 Max;
};

// NOTE: Overlap is the amount that A overlaps B. in each direction (B is treated as static)
static bool AABB_Intersect(const aabb& A, const aabb& B, vec3& Overlap, int& MinCoord);
