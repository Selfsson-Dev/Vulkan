#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;     // Catch the UVs
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inNormal;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec4 lightPos[2]; 
} ubo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outEyePos;
layout (location = 3) out vec3 outWorldPos;
layout (location = 4) out vec4 outLightPos0;
layout (location = 5) out vec4 outLightPos1;
layout (location = 6) out vec2 outUV;   // Pass them to Fragment

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	outColor = inColor;
	outNormal = mat3(ubo.model) * inNormal;
	outUV = inUV;
	
	vec4 worldPos = ubo.model * vec4(inPos, 1.0);
	gl_Position = ubo.projection * ubo.view * worldPos;
	
	outEyePos = vec3(worldPos);
	outWorldPos = worldPos.xyz;
	
	outLightPos0 = ubo.lightPos[0];
	outLightPos1 = ubo.lightPos[1];
}