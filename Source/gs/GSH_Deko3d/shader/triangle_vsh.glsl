#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Transformation
{
    mat4 projMtx;
} u;

void main()
{
    gl_Position = u.projMtx * vec4(inPos, 1.0f);
    outColor = inColor;
}