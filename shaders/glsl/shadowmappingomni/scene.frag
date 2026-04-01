#version 450

layout (binding = 1) uniform samplerCube shadowCubeMap0;
layout (binding = 2) uniform samplerCube shadowCubeMap1;

layout (set = 1, binding = 0) uniform sampler2D colorMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inEyePos;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec4 inLightPos0;
layout (location = 5) in vec4 inLightPos1;
layout (location = 6) in vec2 inUV;
layout (location = 7) in float inIsReflection; // <--- Catch the reflection flag

layout (location = 0) out vec4 outFragColor;

#define EPSILON 0.15
#define SHADOW_OPACITY 0.5

vec3 calcPointLight(vec4 light, samplerCube shadowMap, vec3 normal, vec4 texColor) 
{
	vec3 lightPos = light.xyz;
	float brightness = light.w;
	
	vec3 lightDir = normalize(lightPos - inWorldPos);
	float dist = length(lightPos - inWorldPos);
	float attenuation = 1.0 / (dist * dist);
	
	vec3 IDiffuse = vec3(brightness * attenuation) * max(dot(normal, lightDir), 0.0);
	vec3 diffuseColor = IDiffuse * texColor.rgb * inColor;
	
	vec3 lightVec = inWorldPos - lightPos;
	
	// FIX: The shadow cubemap is upright, but our reflection world is upside down!
	// We must flip the Y lookup vector so the floor doesn't read the ceiling's shadows.
	if (inIsReflection > 0.5) {
		lightVec.y = -lightVec.y; 
	}
	
	float sampledDist = texture(shadowMap, lightVec).r;
	float shadow = (dist <= sampledDist + EPSILON) ? 1.0 : SHADOW_OPACITY;
	
	return diffuseColor * shadow;
}

void main() 
{
	// 1. DELETE THE FLIPPED FOUNDATION!
	// Vulkan -Y is UP. The mirror is at -0.005. 
	// We discard any geometry sticking up higher than -0.02 in the reflection pass.
	if (inIsReflection > 0.5 && inWorldPos.y < -0.02) {
		discard;
	}

	vec4 texColor = texture(colorMap, inUV);
	if (texColor.a < 0.5) {
		discard;
	}

	vec3 N = normalize(inNormal);
	vec3 finalColor = vec3(0.01); 
	
	finalColor += calcPointLight(inLightPos0, shadowCubeMap0, N, texColor);
	finalColor += calcPointLight(inLightPos1, shadowCubeMap1, N, texColor);
	
	finalColor = finalColor / (finalColor + vec3(1.0));
	
	outFragColor = vec4(finalColor, 1.0);
}