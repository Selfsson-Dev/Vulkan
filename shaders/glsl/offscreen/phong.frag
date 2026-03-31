#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	// Normalize our interpolated vectors
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = reflect(-L, N); 

	// Ambient lighting (bumped slightly so the dark areas aren't pitch black)
	vec3 ambient = vec3(0.15) * inColor;

	// Diffuse lighting
	float diff = max(dot(N, L), 0.0);
	vec3 diffuse = diff * inColor;
	
	// Specular lighting
	vec3 specular = vec3(0.0);
	if (diff > 0.0) // Only compute specular if the light is hitting the front of the face
	{
		float spec = pow(max(dot(R, V), 0.0), 32.0); // 32.0 is the shininess factor
		specular = vec3(0.5) * spec; // 0.5 limits the bright white reflection intensity
	}

	outFragColor = vec4(ambient + diffuse + specular, 1.0);
}