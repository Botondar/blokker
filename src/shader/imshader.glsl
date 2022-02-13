#include <common.glsl>

layout(location = 0) vs_out vec4 Color; 

#if defined(VERTEX_SHADER)

layout(location = ATTRIB_POS) in vec3 v_Position;
layout(location = ATTRIB_COLOR) in vec4 v_Color;

layout(push_constant) uniform PushConstants
{
    mat4 Transform;
};

void main()
{
    gl_Position = Transform * vec4(v_Position, 1);
    Color = v_Color;
}

#elif defined(FRAGMENT_SHADER)

layout(location = 0) out vec4 FS_Out0;

void main()
{
    FS_Out0 = vec4(Color.rgb, 1);
}

#endif