#version 450

// Array of 2 shadow map samplers
layout (binding = 1) uniform samplerCube shadowCubeMaps[2];

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inEyePos;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec4 inLightPos0;
layout (location = 5) in vec4 inLightPos1;

layout (location = 0) out vec4 outFragColor;

#define EPSILON 0.15
#define SHADOW_OPACITY 0.5

// Reusable function to calculate lighting and shadows per light
vec3 calcPointLight(vec4 light, samplerCube shadowMap, vec3 normal) 
{
	vec3 lightPos = light.xyz;
	float brightness = light.w; // Extract the brightness!
	
	vec3 lightDir = normalize(lightPos - inWorldPos);
	
	// Calculate attenuation (how fast the light fades over distance)
	// Using inverse-square law for realistic physical light falloff
	float dist = length(lightPos - inWorldPos);
	float attenuation = 1.0 / (dist * dist);
	
	// Apply both brightness and attenuation to the diffuse color
	vec3 IDiffuse = vec3(brightness * attenuation) * max(dot(normal, lightDir), 0.0);
	vec3 diffuseColor = IDiffuse * inColor;
	
	// Shadow calculation
	vec3 lightVec = inWorldPos - lightPos;
	float sampledDist = texture(shadowMap, lightVec).r;
	
	float shadow = (dist <= sampledDist + EPSILON) ? 1.0 : SHADOW_OPACITY;
	
	return diffuseColor * shadow;
}

void main() 
{
	vec3 N = normalize(inNormal);
	vec3 finalColor = vec3(0.01); // Lowered base ambient so the lights pop more!
	
	finalColor += calcPointLight(inLightPos0, shadowCubeMaps[0], N);
	finalColor += calcPointLight(inLightPos1, shadowCubeMaps[1], N);

	// Simple tone mapping to prevent colors from burning out to pure white instantly
	// (Optional, but helps when using high brightness values)
	finalColor = finalColor / (finalColor + vec3(1.0));
	
	outFragColor = vec4(finalColor, 1.0);
}