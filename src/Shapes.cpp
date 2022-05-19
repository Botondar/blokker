#include "Shapes.hpp"

#include <World.hpp>

aabb MakeAABB(const vec3& a, const vec3& b)
{
    aabb Result = 
    {
        .Min = { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z) },
        .Max = { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) },
    };
    return Result;
}

bool AABB_Intersect(const aabb& A, const aabb& B, vec3& Overlap, int& MinCoord)
{
    vec3 Overlap_ = {};
    f32 MinOverlap = F32_MAX_NORMAL;
    int MinCoord_ = -1;

    bool IsCollision = true;
    for (int i = 0; i < 3; i++)
    {
        if ((A.Min[i] <= B.Max[i]) && (B.Min[i] < A.Max[i]))
        {
            Overlap_[i] = (A.Max[i] < B.Max[i]) ? B.Min[i] - A.Max[i] : B.Max[i] - A.Min[i];
            if (Abs(Overlap_[i]) < MinOverlap)
            {
                MinOverlap = Abs(Overlap_[i]);
                MinCoord_ = i;
            }
        }
        else
        {
            IsCollision = false;
            break;
        }
    }

    if (IsCollision)
    {
        Overlap = Overlap_;
        MinCoord = MinCoord_;
    }

    return IsCollision;
}


bool IntersectRayPlane(vec3 P, vec3 v, vec4 Plane, f32 tMin, f32 tMax, f32* tOut)
{
    bool Result = false;

    vec3 N = { Plane.x, Plane.y, Plane.z };
    f32 NdotV = Dot(N, v);

    if (NdotV != 0.0f)
    {
        f32 t = (-Dot(P, N) - Plane.w) / NdotV;
        if ((tMin <= t) && (t < tMax))
        {
            *tOut = t;
            Result = true;
        }
    }

    return Result;
};

bool IntersectRayAABB(vec3 P, vec3 v, aabb Box, f32 tMin, f32 tMax, f32* tOut, direction* OutDir)
{
    bool Result = false;

    bool AnyIntersection = false;
    f32 tIntersection = -1.0f;
    u32 IntersectionDir = (u32)-1;
    for (u32 i = DIRECTION_First; i < DIRECTION_Count; i++)
    {
        switch (i)
        {
            case DIRECTION_POS_X: 
            {
                vec4 Plane = { +1.0f, 0.0f, 0.0f, -Box.Max.x };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.y <= P0.y) && (P0.y <= Box.Max.y) &&
                        (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
            case DIRECTION_NEG_X: 
            {
                vec4 Plane = { -1.0f, 0.0f, 0.0f, +Box.Min.x };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.y <= P0.y) && (P0.y <= Box.Max.y) &&
                        (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
            case DIRECTION_POS_Y: 
            {
                vec4 Plane = { 0.0f, +1.0f, 0.0f, -Box.Max.y };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                        (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
            case DIRECTION_NEG_Y: 
            {
                vec4 Plane = { 0.0f, -1.0f, 0.0f, +Box.Min.y };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                        (Box.Min.z <= P0.z) && (P0.z <= Box.Max.z))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
            case DIRECTION_POS_Z:
            {
                vec4 Plane = { 0.0f, 0.0f, +1.0f, -Box.Max.z };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                        (Box.Min.y <= P0.y) && (P0.y <= Box.Max.y))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
            case DIRECTION_NEG_Z:
            {
                vec4 Plane = { 0.0f, 0.0f, -1.0f, +Box.Min.z };
                f32 t;
                if (IntersectRayPlane(P, v, Plane, tMin, tMax, &t))
                {
                    vec3 P0 = P + t*v;
                    if ((Box.Min.x <= P0.x) && (P0.x <= Box.Max.x) &&
                        (Box.Min.y <= P0.y) && (P0.y <= Box.Max.y))
                    {
                        AnyIntersection = true;
                        tIntersection = t;
                        IntersectionDir = i;
                        tMax = Min(tMax, t);
                    }
                }
            } break;
        }
    }

    if (AnyIntersection)
    {
        *tOut = tIntersection;
        *OutDir = (direction)IntersectionDir;
        Result = true;
    }

    return Result;
};

bool IntersectFrustumAABB(const frustum& Frustum, const aabb& Box)
{
    bool Result = true;

    vec3 HalfExtent = 0.5f * (Box.Max - Box.Min);
    vec3 CenterP3 = 0.5f * (Box.Min + Box.Max);
    vec4 CenterP = { CenterP3.x, CenterP3.y, CenterP3.z, 1.0f };

    for (u32 i = 0; i < 6; i++)
    {
        f32 EffectiveRadius = 
            Abs(Frustum.Planes[i].x * HalfExtent.x) +
            Abs(Frustum.Planes[i].y * HalfExtent.y) + 
            Abs(Frustum.Planes[i].z * HalfExtent.z);

        if (Dot(CenterP, Frustum.Planes[i]) < -EffectiveRadius)
        {
            Result = false;
            break;
        }
    }

    return Result;
}