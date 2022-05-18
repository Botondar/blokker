#include <common.glsl>

// SHARED
layout(location = 0) vs_out vec3 TexCoord;
layout(location = 1) vs_out float AO;

layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

#if defined(VERTEX_SHADER)

layout(location = ATTRIB_POS) in uint v_PackedPosition;
layout(location = ATTRIB_TEXCOORD) in uint v_PackedTexCoord;
layout(location = ATTRIB_CHUNK_P) in vec2 v_ChunkP;

const float AOTable[4] = { 1.0, 0.75, 0.5, 0.25 };

vec3 UnpackPosition(in uint PackedPosition)
{
    //constexpr packed_position POSITION_X_MASK = 0xF8000000;
    //constexpr packed_position POSITION_Y_MASK = 0x07C00000;
    //constexpr packed_position POSITION_Z_MASK = 0x003FFFFF;
    //constexpr packed_position POSITION_X_SHIFT = 27;
    //constexpr packed_position POSITION_Y_SHIFT = 22;

    vec3 Result = vec3(
        float((PackedPosition & 0xF8000000u) >> 27u),
        float((PackedPosition & 0x07C00000u) >> 22u),
        float(PackedPosition & 0x003FFFFFu));
    //Result.z = 0;
    return Result;
}

vec3 UnpackTexCoord(in uint Packed, out float AO)
{
    uint Layer = Packed & 0x07FF;
    uint u = (Packed & 0x0800) >> 11;
    uint v = (Packed & 0x1000) >> 12;
    uint AOType = (Packed & 0xC000) >> 14;

    AO = AOTable[AOType];

    vec3 Result = vec3(float(u), float(v), float(Layer));
    return Result;
}

void main()
{
    vec3 P = UnpackPosition(v_PackedPosition) + vec3(v_ChunkP, 0);
    gl_Position = Transform * vec4(P, 1);
    TexCoord = UnpackTexCoord(v_PackedTexCoord, AO);
}

#elif defined(FRAGMENT_SHADER)

layout(set = 0, binding = 0) uniform texture2DArray Texture;
layout(set = 0, binding = 1) uniform sampler Sampler;

layout(location = 0) out vec4 FS_Out0;

void main()
{
    vec3 Color = texture(sampler2DArray(Texture, Sampler), TexCoord).rgb;
    Color *= AO;
    FS_Out0 = vec4(Color, 1);
}

#endif