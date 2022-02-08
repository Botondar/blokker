#include "Camera.hpp"

mat4 camera::GetGlobalTransform() const 
{
    const f32 SinYaw = Sin(Yaw);
    const f32 CosYaw = Cos(Yaw);
    const f32 SinPitch = Sin(Pitch);
    const f32 CosPitch = Cos(Pitch);
    
    vec3 Forward = { -SinYaw * CosPitch, CosYaw * CosPitch, SinPitch };
    vec3 Up = { 0.0f, 0.0f, 1.0f };
    vec3 Right = Normalize(Cross(Forward, Up));
    Up = Cross(Right, Forward);
    
    mat4 BasisTransform = Mat4(
        Right.x, Forward.x, Up.x, 0.0f,
        Right.y, Forward.y, Up.y, 0.0f,
        Right.z, Forward.z, Up.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    mat4 Translation = Mat4(
        1.0f, 0.0f, 0.0f, P.x,
        0.0f, 1.0f, 0.0f, P.y,
        0.0f, 0.0f, 1.0f, P.z,
        0.0f, 0.0f, 0.0f, 1.0f);
    
    mat4 Transform = Translation * BasisTransform;
    return Transform;
};

mat4 camera::GetLocalTransform() const
{
    const f32 SinYaw = Sin(Yaw);
    const f32 CosYaw = Cos(Yaw);
    const f32 SinPitch = Sin(Pitch);
    const f32 CosPitch = Cos(Pitch);
    
    vec3 Forward = { -SinYaw * CosPitch, -SinPitch, CosYaw * CosPitch };
    vec3 Up = { 0.0f, -1.0f, 0.0f };
    vec3 Right = Normalize(Cross(Forward, Up));
    Up = Cross(Right, Forward);
    
    mat4 BasisTransform = Mat4(
        Right.x, -Up.x, Forward.x, 0.0f,
        Right.y, -Up.y, Forward.y, 0.0f,
        Right.z, -Up.z, Forward.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    mat4 Translation = Mat4(
        1.0f, 0.0f, 0.0f, P.x,
        0.0f, 1.0f, 0.0f, -P.z,
        0.0f, 0.0f, 1.0f, P.y,
        0.0f, 0.0f, 0.0f, 1.0f);
    
    mat4 Transform = Translation * BasisTransform;
    return Transform;
};

mat4 camera::GetTransform() const
{
    mat4 CameraToWorld = Mat4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    
    mat4 Transform = CameraToWorld * GetLocalTransform();
    return Transform;
};

mat4 camera::GetInverseTransform() const
{
    mat4 Transform = GetTransform();
    
    // Transpose submatrix
    f32 m00 = Transform(0, 0); f32 m01 = Transform(1, 0); f32 m02 = Transform(2, 0);
    f32 m10 = Transform(0, 1); f32 m11 = Transform(1, 1); f32 m12 = Transform(2, 1);
    f32 m20 = Transform(0, 2); f32 m21 = Transform(1, 2); f32 m22 = Transform(2, 2);
    
    vec3 T = { Transform(0, 3), Transform(1, 3), Transform(2, 3) };
    
    vec3 TPrime = 
    {
        -(m00 * T.x + m01 * T.y + m02 * T.z),
        -(m10 * T.x + m11 * T.y + m12 * T.z),
        -(m20 * T.x + m21 * T.y + m22 * T.z),
    };
    
    mat4 InverseTransform = Mat4(
        m00, m01, m02, TPrime.x,
        m10, m11, m12, TPrime.y,
        m20, m21, m22, TPrime.z,
        0.0f, 0.0f, 0.0f, 1.0f);
    return InverseTransform;
}
