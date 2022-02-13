#include <common.glsl>

// SHARED
layout(location = 0) vs_out vec3 TexCoord;
layout(location = 1) vs_out vec4 Color;

layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

#if defined(VERTEX_SHADER)

layout(location = ATTRIB_POS) in vec3 v_Position;
layout(location = ATTRIB_TEXCOORD) in vec3 v_UVW;
layout(location = ATTRIB_COLOR) in vec4 v_Color;

void main()
{
    gl_Position = Transform * vec4(v_Position, 1);
    TexCoord = v_UVW;
    Color = vec4(v_Color.rgb, 1);
}

#elif defined(FRAGMENT_SHADER)

layout(set = 0, binding = 0) uniform texture2DArray Texture;
layout(set = 0, binding = 1) uniform sampler Sampler;

layout(location = 0) out vec4 FS_Out0;

void main()
{
    FS_Out0 = vec4((texture(sampler2DArray(Texture, Sampler), TexCoord).rgb), 1);
}

#endif