#version 450
layout(push_constant) uniform Constants { mat4 worldViewProj; vec4 color; } pc;
layout(location=0) out vec4 outputColor;
void main() { outputColor=pc.color; }
