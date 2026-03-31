#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inNormal;

// Updated UBO to handle 2 light positions
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
// Send both light positions to the fragment shader
layout (location = 4) out vec4 outLightPos0;
layout (location = 5) out vec4 outLightPos1;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	outColor = inColor;
	outNormal = inNormal;
	
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos.xyz, 1.0);
	
	outEyePos = vec3(ubo.model * vec4(inPos, 1.0f));
	outWorldPos = inPos;
	
	// Pass the individual light positions forward
	outLightPos0 = ubo.lightPos[0];
	outLightPos1 = ubo.lightPos[1];
}