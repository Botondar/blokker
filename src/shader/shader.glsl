#version 450 core

#if defined(VERTEX_SHADER)
#define vs_out out
#elif defined(FRAGMENT_SHADER)
#define vs_out in
#endif

// SHARED
layout(location = 0) vs_out vec2 TexCoord;
layout(location = 1) vs_out vec4 Color;


layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

#ifdef VERTEX_SHADER

layout(location = 0) in vec3 v_Position;
layout(location = 1) in vec2 v_UV;
layout(location = 2) in vec3 v_Color;

void main()
{
    gl_Position = Transform * vec4(v_Position, 1);
    TexCoord = v_UV;
    Color = vec4(v_Color, 1);
}
#endif

#ifdef FRAGMENT_SHADER

layout(set = 0, binding = 0) uniform texture2D Texture;
layout(set = 0, binding = 1) uniform sampler Sampler;

layout(location = 0) out vec4 FS_Out0;

void main()
{
    FS_Out0 = vec4((texture(sampler2D(Texture, Sampler), TexCoord).rgb), 1);
}

#endif