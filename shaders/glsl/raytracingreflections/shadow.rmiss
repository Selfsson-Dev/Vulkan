#version 460
#extension GL_EXT_ray_tracing : require

// Location 1 matches the shadow payload we will define in the closest hit shader
layout(location = 1) rayPayloadInEXT bool isShadowed;

void main()
{
	// The ray made it all the way to the light source without hitting geometry!
	isShadowed = false;
}