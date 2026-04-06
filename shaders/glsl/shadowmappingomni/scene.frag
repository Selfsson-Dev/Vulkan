#version 450

// Shadow bindings completely removed!
layout (set = 1, binding = 0) uniform sampler2D colorMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inEyePos;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec4 inLightPos0;
layout (location = 5) in vec4 inLightPos1;
layout (location = 6) in vec2 inUV;
layout (location = 7) in float inIsReflection;

layout (location = 0) out vec4 outFragColor;

vec3 calcPointLight(vec4 light, vec3 normal, vec4 texColor) 
{
	vec3 lightPos = light.xyz;
	float brightness = light.w;
	
	vec3 lightDir = normalize(lightPos - inWorldPos);
	float dist = length(lightPos - inWorldPos);
	float attenuation = 1.0 / (dist * dist);
	
	vec3 IDiffuse = vec3(brightness * attenuation) * max(dot(normal, lightDir), 0.0);
	vec3 diffuseColor = IDiffuse * texColor.rgb * inColor;
	
	return diffuseColor;
}

void main() 
{
	if (inIsReflection > 0.5 && inWorldPos.y < -0.02) {
		discard;
	}

	vec4 texColor = texture(colorMap, inUV);
	if (texColor.a < 0.5) {
		discard;
	}

	vec3 N = normalize(inNormal);
	vec3 finalColor = vec3(0.01); 
	
	finalColor += calcPointLight(inLightPos0, N, texColor);
	finalColor += calcPointLight(inLightPos1, N, texColor);
	
	finalColor = finalColor / (finalColor + vec3(1.0));
	
	outFragColor = vec4(finalColor, 1.0);
}