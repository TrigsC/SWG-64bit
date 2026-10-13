#version 450
layout(set=0,binding=0,std140) uniform Constants { mat4 worldViewProj;mat4 world;vec4 color; } pc;
layout(set=1,binding=0) uniform sampler2D meshTexture;
layout(location=0) in vec3 worldNormal;
layout(location=1) in vec2 textureUV;
layout(location=0) out vec4 outputColor;
void main() { float shade=.5+.5*clamp(dot(normalize(worldNormal),normalize(vec3(.4,1,-.3))),0,1);vec4 texel=texture(meshTexture,textureUV);outputColor=vec4(texel.rgb*pc.color.rgb*shade,texel.a*pc.color.a); }
