#version 450
layout(location=0) in vec3 worldNormal;
layout(location=1) in vec3 vertexColor;
layout(location=0) out vec4 outputColor;
void main() { float shade=.5+.5*clamp(dot(normalize(worldNormal),normalize(vec3(.4,1,-.3))),0,1);outputColor=vec4(vertexColor*shade,1); }
