#include <common.glsl>

layout(location = 0) vs_out vec2 TexCoord;
layout(location = 1) vs_out vec4 Color; 

#if defined(VERTEX_SHADER)

layout(location = ATTRIB_POS) in vec2 v_Position;
layout(location = ATTRIB_TEXCOORD) in vec2 v_UV;
layout(location = ATTRIB_COLOR) in vec4 v_Color;

layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

void main()
{
    gl_Position = Transform * vec4(v_Position, 0, 1);
    Color = v_Color;
    Color.rgb = pow(Color.rgb, vec3(2.2)); // NOTE(boti): ImGui colors are sRGB, but the OM stage also performs sRGB correction
    TexCoord = v_UV;
}

#elif defined(FRAGMENT_SHADER)

layout(binding = 0) uniform sampler2D Sampler;

layout(location = 0) out vec4 FS_Out0;

void main()
{
    FS_Out0 = Color * texture(Sampler, TexCoord);
}

#endif