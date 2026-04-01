#version 450

layout (binding = 1) uniform sampler2D samplerReflection;

layout (location = 0) in vec4 inProjCoords;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	// Convert projected coordinates to UV space
	vec3 projCoords = inProjCoords.xyz / inProjCoords.w;
	vec2 uv = projCoords.xy * 0.5 + 0.5;
	
	vec4 reflectionColor = texture(samplerReflection, uv);
	
	// Add a slight blue-ish polished marble tint to the mirror
	//outFragColor = reflectionColor * vec4(0.8, 0.9, 1.0, 1.0);
	outFragColor = reflectionColor;
}