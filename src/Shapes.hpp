#pragma once

#include <Common.hpp>
#include <Math.hpp>

enum direction : u32;

struct frustum
{
    union 
    {
        struct 
        {
            vec4 Near;
            vec4 Far;
            vec4 Left;
            vec4 Right;
            vec4 Top;
            vec4 Bottom;
        };
        vec4 Planes[6];
    };
};

struct aabb
{
    vec3 Min;
    vec3 Max;
};

aabb MakeAABB(const vec3& a, const vec3& b);

// NOTE: Overlap is the amount that A overlaps B. in each direction (B is treated as static)
bool AABB_Intersect(const aabb& A, const aabb& B, vec3& Overlap, int& MinCoord);

bool IntersectRayPlane(vec3 P, vec3 v, vec4 Plane, f32 tMin, f32 tMax, f32* tOut);
bool IntersectRayAABB(vec3 P, vec3 v, aabb Box, f32 tMin, f32 tMax, f32* tOut, direction* OutDir);

bool IntersectRayFrustumAABB(const frustum& Frustum, const aabb& Box);
