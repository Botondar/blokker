#include <common.glsl>

// SHARED
layout(location = 0) vs_out vec3 TexCoord;
layout(location = 1) vs_out float AO;

layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

#if defined(VERTEX_SHADER)

layout(location = ATTRIB_POS) in vec3 v_Position;
layout(location = ATTRIB_TEXCOORD) in uint v_PackedTexCoord;

const float AOTable[4] = { 1.0, 0.75, 0.75, 0.5 };

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
    gl_Position = Transform * vec4(v_Position, 1);
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