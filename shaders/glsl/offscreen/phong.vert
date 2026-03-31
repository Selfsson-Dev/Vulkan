#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inNormal;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec4 lightPos;
} ubo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;

void main() 
{
	outColor = inColor;

	// 1. Calculate positions
	vec4 worldPos = ubo.model * vec4(inPos, 1.0);
	vec4 viewPos = ubo.view * worldPos;

	// 2. Transform the Normal to View Space
	outNormal = mat3(ubo.view * ubo.model) * inNormal;

	// 3. Transform the World Space light to View Space
	vec4 viewLightPos = ubo.view * ubo.lightPos;
	outLightVec = viewLightPos.xyz - viewPos.xyz;

	// 4. Vector from vertex to the camera (Camera is at 0,0,0 in View Space)
	outViewVec = -viewPos.xyz;

	// Final screen position
	gl_Position = ubo.projection * viewPos;

	// Clip against reflection plane
	vec4 clipPlane = vec4(0.0, 0.0, 0.0, 0.0);	
	gl_ClipDistance[0] = dot(vec4(inPos, 1.0), clipPlane);	
}