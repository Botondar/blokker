#include "Shapes.hpp"

static aabb MakeAABB(const vec3& a, const vec3& b)
{
    aabb Result = 
    {
        .Min = { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z) },
        .Max = { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) },
    };
    return Result;
}

static bool AABB_Intersect(const aabb& A, const aabb& B, vec3& Overlap, int& MinCoord)
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
