#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	// Return a dark grayish-blue for the sky/background
	hitValue = vec3(0.05, 0.05, 0.1);
}