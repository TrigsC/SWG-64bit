#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(push_constant) uniform Constants { mat4 viewProj; } pc;
layout(location=0) out vec2 textureUV;
layout(location=1) out vec4 tint;
void main() { gl_Position=vec4(position,1)*pc.viewProj;textureUV=uv;tint=color; }
