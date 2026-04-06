#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
// REQUIRED: Allows us to cast 64-bit ints into memory buffers!
#extension GL_EXT_buffer_reference2 : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;
// (Removed the isShadowed payload)

layout(binding = 3, set = 0) uniform sampler2D textures[];

hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;


// Update this in raygen, anyhit, and closesthit!
layout(binding = 2, set = 0) uniform UBO {
	mat4 viewInverse;
	mat4 projInverse;
	vec4 lightPos[2]; 
	int vertexSize;
	int padding;
	uint64_t vertexAddressSponza;
	uint64_t indexAddressSponza;
	uint64_t vertexAddressPlane;
	uint64_t indexAddressPlane;
	uint64_t textureIndexAddressSponza; 
} ubo;

// Define the raw memory layout
layout(buffer_reference, std430, buffer_reference_align = 4) buffer Vertices { float v[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer Indices  { uint i[];  };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer TexIndices { int id[]; };

struct Vertex {
	vec3 pos;
	vec3 normal;
	vec2 uv;
};

// Helper function to extract a Vertex from the raw memory float array
Vertex unpackVertex(Vertices verts, uint index) {
	// In vkglTF::Vertex, position is floats 0,1,2 and normal is floats 3,4,5
	uint floatOffset = index * (ubo.vertexSize / 4);
	Vertex v;
	v.pos = vec3(verts.v[floatOffset], verts.v[floatOffset + 1], verts.v[floatOffset + 2]);
	v.normal = vec3(verts.v[floatOffset + 3], verts.v[floatOffset + 4], verts.v[floatOffset + 5]);
	// Extract UVs from floats 6 and 7
	v.uv = vec2(verts.v[floatOffset + 6], verts.v[floatOffset + 7]); 
	return v;
}

void main()
{
	Vertices verts;
	Indices inds;

	// 1. Identify which memory block we hit based on the TLAS Custom Index
	if (gl_InstanceCustomIndexEXT == 0) { 
		// We hit Sponza!
		verts = Vertices(ubo.vertexAddressSponza);
		inds  = Indices(ubo.indexAddressSponza);
	} else {                              
		// We hit the Mirror Plane!
		verts = Vertices(ubo.vertexAddressPlane);
		inds  = Indices(ubo.indexAddressPlane);
	}

	// 2. Fetch the triangle indices
	ivec3 index = ivec3(inds.i[3 * gl_PrimitiveID], inds.i[3 * gl_PrimitiveID + 1], inds.i[3 * gl_PrimitiveID + 2]);

	// 3. Unpack the three vertices of the triangle we hit
	Vertex v0 = unpackVertex(verts, index.x);
	Vertex v1 = unpackVertex(verts, index.y);
	Vertex v2 = unpackVertex(verts, index.z);

	// 4. Calculate exactly where on the triangle we hit using Barycentric coordinates
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);

	// Convert normal from Object Space to World Space
	vec3 worldNormal = normalize(vec3(gl_ObjectToWorldEXT * vec4(normal, 0.0)));

	vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	vec2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;

	// 5. THE MAGIC: Are we the Mirror, or are we Sponza?
	if (gl_InstanceCustomIndexEXT == 1) { 
		
		// WE HIT THE MIRROR! Calculate bounce angle...
		vec3 reflectionDir = reflect(gl_WorldRayDirectionEXT, worldNormal);
		uint rayFlags = gl_RayFlagsNoneEXT;		

		// Shoot the recursive ray!
		traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, worldPos, 0.01, reflectionDir, 1000.0, 0);

		// hitValue now contains the exact color of whatever the bounced ray hit!
		// No tint is applied, making it a perfect, physically accurate mirror.

	} else {
		// WE HIT SPONZA!
		// 1. Check which texture this specific triangle uses
		TexIndices texInds = TexIndices(ubo.textureIndexAddressSponza);
		int texID = texInds.id[gl_PrimitiveID];
		
		vec3 baseColor = vec3(0.8); // Default clay color
		if (texID >= 0) {
			// Dynamically sample from the massive array!
			// MUST use nonuniformEXT because rays hit different materials!
			baseColor = texture(textures[nonuniformEXT(texID)], uv).rgb;
		}
		
		// 2. Ambient Light
		float ambientStrength = 0.05; 
		vec3 finalColor = ambientStrength * baseColor;

		// 3. Lighting Loop (NO SHADOWS)
		for(int i = 0; i < 2; i++) {
			vec3 lightPos = ubo.lightPos[i].xyz;
			float brightness = ubo.lightPos[i].w;
			
			vec3 lightDir = normalize(lightPos - worldPos);
			float dist = length(lightPos - worldPos);
			
			// Calculate the light exactly as if it hit (100% visibility)
			float attenuation = 1.0 / (dist * dist);
			vec3 diffuse = vec3(brightness * attenuation) * max(dot(worldNormal, lightDir), 0.0);
			vec3 diffuseColor = diffuse * baseColor;
			
			// Add light directly to the final color without the shadow multiplier
			finalColor += diffuseColor;
		}

		// Tone mapping
		finalColor = finalColor / (finalColor + vec3(1.0));
		
		hitValue = finalColor;
	}
}