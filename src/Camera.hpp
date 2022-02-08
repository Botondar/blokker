#pragma once

#include <Math.hpp>

struct camera 
{
    vec3 P;
    f32 Yaw, Pitch;
    
    mat4 GetGlobalTransform() const;
    mat4 GetLocalTransform() const;
    mat4 GetTransform() const;
    mat4 GetInverseTransform() const;

#if 0
    void GetAxes(vec3& Forward, vec3& Right, vec3& Up) const 
    {
        const f32 SinYaw = Sin(Yaw);
        const f32 CosYaw = Cos(Yaw);
        const f32 SinPitch = Sin(Pitch);
        const f32 CosPitch = Cos(Pitch);
        
        Forward = {  };
    }
#endif
};
