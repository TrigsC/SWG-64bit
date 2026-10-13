#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(set=0,binding=0,std140) uniform Constants { mat4 worldViewProj;mat4 world;vec4 color; } pc;
layout(location=0) out vec3 worldNormal;
layout(location=1) out vec2 textureUV;
void main() { gl_Position=vec4(position,1)*pc.worldViewProj;worldNormal=normalize((vec4(normal,0)*pc.world).xyz);textureUV=uv; }
