/*
* Vulkan Example - Hardware accelerated ray tracing example for doing reflections
* Modified for Thesis Benchmark: Sponza + Mirror Plane using Buffer Device Address
*/

#include "VulkanRaytracingSample.h"
#include "VulkanglTFModel.h"

class VulkanExample : public VulkanRaytracingSample
{
public:

	VkPhysicalDeviceDescriptorIndexingFeatures enabledDescriptorIndexingFeatures{};
	std::vector<AccelerationStructure> bottomLevelAS;
	AccelerationStructure topLevelAS{};

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
	struct ShaderBindingTables {
		ShaderBindingTable raygen;
		ShaderBindingTable miss;
		ShaderBindingTable hit;
	} shaderBindingTables;

	// UPDATED: Added 64-bit memory addresses for both models
	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		glm::vec4 lightPos[2];
		int32_t vertexSize{ 0 };
		int32_t padding{ 0 };
		uint64_t vertexAddressSponza;
		uint64_t indexAddressSponza;
		uint64_t vertexAddressPlane;
		uint64_t indexAddressPlane;
		uint64_t textureIndexAddressSponza; // <--- ADDED
	} uniformData;

	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets{};
	vks::Buffer textureIndexBufferSponza;
	struct {
		vkglTF::Model sponza;
		vkglTF::Model plane;
	} models;

	VulkanExample() : VulkanRaytracingSample()
	{
		title = "Ray Tracing Reflections (Thesis Benchmark)";

		// Lock camera for exact 1:1 PSNR testing
		width = 1920;
		height = 1080;
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(90.0f, (float)width / (float)height, 0.1f, 1024.0f);
		camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
		camera.setPosition(glm::vec3(0.0f, 2.5f, 0.0f));

		enableExtensions();
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		deleteStorageImage();
		for (auto& blas : bottomLevelAS) {
			deleteAccelerationStructure(blas);
		}
		deleteAccelerationStructure(topLevelAS);
		shaderBindingTables.raygen.destroy();
		shaderBindingTables.miss.destroy();
		shaderBindingTables.hit.destroy();
		for (auto& buffer : uniformBuffers) {
			buffer.destroy();
		}
		textureIndexBufferSponza.destroy();
	}

	void loadAssets()
	{
		vkglTF::memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.sponza.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.plane.loadFromFile(getAssetPath() + "models/plane.gltf", vulkanDevice, queue, glTFLoadingFlags);

		std::cout << "========================================" << std::endl;
		std::cout << "SPONZA TEXTURES LOADED: " << models.sponza.textures.size() << std::endl;
		std::cout << "========================================" << std::endl;

		// 1. Create an array big enough to hold an ID for every single triangle
		uint32_t sponzaTriangleCount = static_cast<uint32_t>(models.sponza.indices.count) / 3;
		std::vector<int32_t> triangleTextures(sponzaTriangleCount, -1);

		// 2. Crawl the glTF nodes to map materials to triangles
		std::function<void(vkglTF::Node*)> traverseNode = [&](vkglTF::Node* node) {
			if (node->mesh) {
				for (vkglTF::Primitive* primitive : node->mesh->primitives) {
					int32_t texIndex = -1; // -1 means no texture (clay color)

					// Sponza often uses the older Diffuse Texture extension! Check both!
					if (primitive->material.baseColorTexture != nullptr) {
						texIndex = primitive->material.baseColorTexture->index;
					}
					else if (primitive->material.diffuseTexture != nullptr) {
						texIndex = primitive->material.diffuseTexture->index;
					}

					uint32_t firstTriangle = primitive->firstIndex / 3;
					uint32_t triCount = primitive->indexCount / 3;
					for (uint32_t i = 0; i < triCount; i++) {
						triangleTextures[firstTriangle + i] = texIndex;
					}
				}
			}
			for (auto child : node->children) {
				traverseNode(child);
			}
			};

		// KICK OFF THE TRAVERSAL HERE!
		for (auto node : models.sponza.nodes) {
			traverseNode(node);
		}

		// 3. Upload the map to the GPU using a STAGING BUFFER
		size_t bufferSize = triangleTextures.size() * sizeof(int32_t);

		// A. Create a temporary staging buffer on the CPU side (Host Visible)
		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			bufferSize,
			triangleTextures.data())); // We CAN map data here!

		// B. Create the actual, ultra-fast buffer on the GPU (Device Local)
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&textureIndexBufferSponza,
			bufferSize)); // Notice we don't pass the data pointer here!

		// C. Command the GPU to copy the data from the Staging Buffer into VRAM
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion{};
		copyRegion.size = bufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, textureIndexBufferSponza.buffer, 1, &copyRegion);
		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		// D. Destroy the temporary staging buffer (we don't need it anymore)
		stagingBuffer.destroy();
	}

	void buildBLAS(vkglTF::Model& model)
	{
		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
		vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(model.vertices.buffer);
		indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(model.indices.buffer);

		uint32_t numTriangles = static_cast<uint32_t>(model.indices.count) / 3;

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.flags = 0; // Strip the global opaque flag!
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		accelerationStructureGeometry.geometry.triangles.maxVertex = model.vertices.count - 1;
		accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(vkglTF::Vertex);
		accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;

		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &numTriangles, &buildSizesInfo);

		AccelerationStructure blas{};
		createAccelerationStructure(blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, buildSizesInfo);

		ScratchBuffer scratchBuffer = createScratchBuffer(buildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildGeometryInfo.dstAccelerationStructure = blas.handle;
		buildGeometryInfo.geometryCount = 1;
		buildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		buildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
		buildRangeInfo.primitiveCount = numTriangles;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfos = { &buildRangeInfo };

		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfo, buildRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
		bottomLevelAS.push_back(blas);
	}

	void createBottomLevelAccelerationStructure()
	{
		buildBLAS(models.sponza); // Will be index 0
		buildBLAS(models.plane);  // Will be index 1
	}

	void createTopLevelAccelerationStructure()
	{
		std::vector<VkAccelerationStructureInstanceKHR> instances;

		// 1. Sponza - Identity Matrix (No scale, no translation)
		VkTransformMatrixKHR sponzaMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		};

		VkAccelerationStructureInstanceKHR sponzaInstance{};
		sponzaInstance.transform = sponzaMatrix;
		sponzaInstance.instanceCustomIndex = 0;
		sponzaInstance.mask = 0xFF;
		// Add the TRIANGLE_FACING_CULL_DISABLE back in!
		sponzaInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
		sponzaInstance.accelerationStructureReference = bottomLevelAS[0].deviceAddress;
		instances.push_back(sponzaInstance);

		// 2. Mirror Plane - Scale by 15.0f, Translate Y by -0.005f
		// Note: The 4th column is translation (X, Y, Z). The diagonals are scale.
		VkTransformMatrixKHR mirrorMatrix = {
			15.0f,  0.0f,   0.0f,   0.0f,
			 0.0f, 15.0f,   0.0f,  -0.005f,
			 0.0f,  0.0f,  15.0f,   0.0f
		};

		VkAccelerationStructureInstanceKHR mirrorInstance{};
		mirrorInstance.transform = mirrorMatrix;
		mirrorInstance.instanceCustomIndex = 1;
		mirrorInstance.mask = 0xFF;
		mirrorInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		mirrorInstance.accelerationStructureReference = bottomLevelAS[1].deviceAddress;
		instances.push_back(mirrorInstance);

		// Buffer for instance data
		vks::Buffer instancesBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&instancesBuffer,
			instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
			instances.data()));

		VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
		instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		accelerationStructureGeometry.flags = 0; // Strip the global opaque flag!
		accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		uint32_t primitive_count = static_cast<uint32_t>(instances.size());
		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &primitive_count, &accelerationStructureBuildSizesInfo);

		createAccelerationStructure(topLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, accelerationStructureBuildSizesInfo);

		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
		buildGeometryInfo.geometryCount = 1;
		buildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		buildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
		buildRangeInfo.primitiveCount = primitive_count;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfos = { &buildRangeInfo };

		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfo, buildRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
		instancesBuffer.destroy();
	}

	void createShaderBindingTables() {
		const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t handleSizeAligned = vks::tools::alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);
		const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
		const uint32_t sbtSize = groupCount * handleSizeAligned;

		std::vector<uint8_t> shaderHandleStorage(sbtSize);
		VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

		createShaderBindingTable(shaderBindingTables.raygen, 1);
		createShaderBindingTable(shaderBindingTables.miss, 2);
		createShaderBindingTable(shaderBindingTables.hit, 1);

		// 1. Copy Raygen
		memcpy(shaderBindingTables.raygen.mapped, shaderHandleStorage.data(), handleSize);

		// 2. Copy Miss 0 and Miss 1 (Safely aligned!)
		uint8_t* missMapped = reinterpret_cast<uint8_t*>(shaderBindingTables.miss.mapped);
		memcpy(missMapped, shaderHandleStorage.data() + handleSizeAligned, handleSize);
		memcpy(missMapped + handleSizeAligned, shaderHandleStorage.data() + (handleSizeAligned * 2), handleSize);

		// 3. Copy Hit
		memcpy(shaderBindingTables.hit.mapped, shaderHandleStorage.data() + (handleSizeAligned * 3), handleSize);
	}

	void createDescriptorSets()
	{
		uint32_t textureCount = static_cast<uint32_t>(models.sponza.textures.size());

		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxConcurrentFrames },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxConcurrentFrames },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount * maxConcurrentFrames } // Allocate enough room!
		};

		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < maxConcurrentFrames; i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));

			VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
			descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
			descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
			descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;

			std::vector<VkDescriptorImageInfo> textureDescriptors;
			for (auto& tex : models.sponza.textures) {
				textureDescriptors.push_back(tex.descriptor);
			}

			VkWriteDescriptorSet textureArrayWrite{};
			textureArrayWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			textureArrayWrite.dstSet = descriptorSets[i];
			textureArrayWrite.dstBinding = 3;
			textureArrayWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureArrayWrite.descriptorCount = textureCount;
			textureArrayWrite.pImageInfo = textureDescriptors.data();

			VkWriteDescriptorSet accelerationStructureWrite{};
			accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
			accelerationStructureWrite.dstSet = descriptorSets[i];
			accelerationStructureWrite.dstBinding = 0;
			accelerationStructureWrite.descriptorCount = 1;
			accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

			VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
							accelerationStructureWrite,
							vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
							vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &uniformBuffers[i].descriptor),
							textureArrayWrite // Push the massive array to Binding 3!
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
		}
	}

	void createRayTracingPipeline()
	{
		uint32_t textureCount = static_cast<uint32_t>(models.sponza.textures.size());

		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
			// THE FIX IS HERE: Added VK_SHADER_STAGE_ANY_HIT_BIT_KHR so it can read the memory addresses!
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 3, textureCount)
		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCI, nullptr, &pipelineLayout));

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		VkSpecializationMapEntry specializationMapEntry = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		uint32_t maxRecursion = 4;
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &specializationMapEntry, sizeof(maxRecursion), &maxRecursion);

		// 1. RAYGEN GROUP
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
			shaderStages.back().pSpecializationInfo = &specializationInfo;
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		// 2. MISS GROUP (Both Miss Shaders go here!)
		{
			// Miss 0: Background/Sky
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);

			// Miss 1: Shadows
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroups.push_back(shaderGroup);
		}

		// 3. CLOSEST HIT GROUP (Now with Any-Hit!)
		{
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
			shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;

			// ADD THE ANY-HIT SHADER
			shaderStages.push_back(loadShader(getShadersPath() + "raytracingreflections/anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
			shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;

			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI = vks::initializers::rayTracingPipelineCreateInfoKHR();
		rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		rayTracingPipelineCI.pStages = shaderStages.data();
		rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
		rayTracingPipelineCI.pGroups = shaderGroups.data();
		rayTracingPipelineCI.maxPipelineRayRecursionDepth = std::min(uint32_t(4), rayTracingPipelineProperties.maxRayRecursionDepth);
		rayTracingPipelineCI.layout = pipelineLayout;
		VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline));
	}

	void createUniformBuffer()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData), &uniformData));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void handleResize()
	{
		createStorageImage(swapChain.colorFormat, { width, height, 1 });
		VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
		for (auto i = 0; i < maxConcurrentFrames; i++) {
			VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
			vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
		}
		resized = false;
	}

	void updateUniformBuffers()
	{
		uniformData.projInverse = glm::inverse(camera.matrices.perspective);
		uniformData.viewInverse = glm::inverse(camera.matrices.view);

		// Updated to match your rasterization light pos array logic for later!
		uniformData.lightPos[0] = glm::vec4(-3.8f, -3.5f, 0.0f, 40.0f);
		uniformData.lightPos[1] = glm::vec4(4.8f, -3.5f, 0.0f, 40.0f);
		uniformData.vertexSize = sizeof(vkglTF::Vertex);

		// BDA - Pass exactly where the data lives on the GPU
		uniformData.vertexAddressSponza = getBufferDeviceAddress(models.sponza.vertices.buffer);
		uniformData.indexAddressSponza = getBufferDeviceAddress(models.sponza.indices.buffer);
		uniformData.vertexAddressPlane = getBufferDeviceAddress(models.plane.vertices.buffer);
		uniformData.indexAddressPlane = getBufferDeviceAddress(models.plane.indices.buffer);

		uniformData.textureIndexAddressSponza = getBufferDeviceAddress(textureIndexBufferSponza.buffer);

		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(uniformData));
	}

	void getEnabledFeatures()
	{
		// 1. Enable Bindless Texturing Features
		enabledDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		enabledDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		enabledDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;

		// 2. Chain them into the Buffer Device Address features
		enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
		enabledBufferDeviceAddresFeatures.pNext = &enabledDescriptorIndexingFeatures; // <--- CHAIN HERE

		// 3. Chain into Ray Tracing Pipeline
		enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		// 4. Chain into Acceleration Structure
		enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

		deviceCreatepNextChain = &enabledAccelerationStructureFeatures;
	}

	void prepare()
	{
		VulkanRaytracingSample::prepare();
		loadAssets();
		createBottomLevelAccelerationStructure();
		createTopLevelAccelerationStructure();
		createStorageImage(swapChain.colorFormat, { width, height, 1 });
		createUniformBuffer();
		createRayTracingPipeline();
		createShaderBindingTables();
		createDescriptorSets();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		if (resized) handleResize();

		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, 0);

		VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
		vkCmdTraceRaysKHR(cmdBuffer, &shaderBindingTables.raygen.stridedDeviceAddressRegion, &shaderBindingTables.miss.stridedDeviceAddressRegion, &shaderBindingTables.hit.stridedDeviceAddressRegion, &emptySbtEntry, width, height, 1);

		vks::tools::setImageLayout(cmdBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
		vks::tools::setImageLayout(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);

		VkImageCopy copyRegion{};
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.srcOffset = { 0, 0, 0 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstOffset = { 0, 0, 0 };
		copyRegion.extent = { width, height, 1 };
		vkCmdCopyImage(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		vks::tools::setImageLayout(cmdBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange);
		vks::tools::setImageLayout(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);

		drawUI(cmdBuffer, frameBuffers[currentImageIndex]);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	virtual void render()
	{
		if (!prepared) return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}
};

VULKAN_EXAMPLE_MAIN()