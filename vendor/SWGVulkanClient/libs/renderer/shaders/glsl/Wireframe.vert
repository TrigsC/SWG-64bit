#version 450
layout(location=0) in vec3 position;
layout(push_constant) uniform Constants { mat4 worldViewProj; vec4 color; } pc;
void main() { gl_Position=vec4(position,1)*pc.worldViewProj; }
