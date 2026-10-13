#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 color;
layout(push_constant) uniform Constants { mat4 viewProj; } pc;
layout(location=0) out vec3 worldNormal;
layout(location=1) out vec3 vertexColor;
void main() { gl_Position=vec4(position,1)*pc.viewProj;worldNormal=normal;vertexColor=color; }
