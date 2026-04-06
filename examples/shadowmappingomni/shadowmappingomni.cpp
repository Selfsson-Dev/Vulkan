/*
* Vulkan Example - Planar Reflections (SHADOWS COMPLETELY REMOVED)
* Modified for Thesis Benchmark
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

// Reflection Framebuffer Resolution
#define REFLECTION_DIM 512

class VulkanExample : public VulkanExampleBase
{
public:
	float zNear{ 0.1f };
	float zFar{ 1024.0f };

	struct {
		vkglTF::Model sponza;
		vkglTF::Model plane;
	} models;

	glm::vec4 lightPos[2] = {
		glm::vec4(-3.8f, -3.5f, 0.0f, 20.0f),
		glm::vec4(4.8f, -3.5f, 0.0f, 20.0f)
	};

	struct UniformDataScene {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos[2];
	} uniformDataScene;

	struct UniformDataMirror {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	} uniformDataMirror;

	struct UniformBuffers {
		vks::Buffer sponzaModel;
		vks::Buffer sponzaReflection;
		vks::Buffer mirror;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct {
		VkPipeline sponzaMain{ VK_NULL_HANDLE };
		VkPipeline sponzaReflection{ VK_NULL_HANDLE };
		VkPipeline mirror{ VK_NULL_HANDLE };
	} pipelines;

	struct {
		VkPipelineLayout sponzaMain{ VK_NULL_HANDLE };
		VkPipelineLayout mirror{ VK_NULL_HANDLE };
	} pipelineLayouts;

	struct DescriptorSets {
		VkDescriptorSet sponzaModel{ VK_NULL_HANDLE };
		VkDescriptorSet sponzaReflection{ VK_NULL_HANDLE };
		VkDescriptorSet mirror{ VK_NULL_HANDLE };
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;

	VkDescriptorSetLayout descriptorSetLayoutMain{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutGLTF{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutMirror{ VK_NULL_HANDLE };

	struct FrameBufferAttachment { VkImage image; VkDeviceMemory mem; VkImageView view; };

	// Reflection Pass Setup
	struct ReflectionPass {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color, depth;
		VkRenderPass renderPass;
		VkSampler sampler;
		VkDescriptorImageInfo descriptor;
	} reflectionPass{};

	VulkanExample() : VulkanExampleBase()
	{
		title = "Planar Reflection Benchmark";
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(90.0f, (float)width / (float)height, zNear, zFar);
		camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
		camera.setPosition(glm::vec3(0.0f, 2.5f, 0.0f));
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyImageView(device, reflectionPass.color.view, nullptr);
			vkDestroyImage(device, reflectionPass.color.image, nullptr);
			vkFreeMemory(device, reflectionPass.color.mem, nullptr);
			vkDestroyImageView(device, reflectionPass.depth.view, nullptr);
			vkDestroyImage(device, reflectionPass.depth.image, nullptr);
			vkFreeMemory(device, reflectionPass.depth.mem, nullptr);
			vkDestroyRenderPass(device, reflectionPass.renderPass, nullptr);
			vkDestroySampler(device, reflectionPass.sampler, nullptr);
			vkDestroyFramebuffer(device, reflectionPass.frameBuffer, nullptr);

			vkDestroyPipeline(device, pipelines.sponzaMain, nullptr);
			vkDestroyPipeline(device, pipelines.sponzaReflection, nullptr);
			vkDestroyPipeline(device, pipelines.mirror, nullptr);

			vkDestroyPipelineLayout(device, pipelineLayouts.sponzaMain, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.mirror, nullptr);

			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMain, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutGLTF, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMirror, nullptr);

			for (auto& buffer : uniformBuffers) {
				buffer.sponzaModel.destroy();
				buffer.sponzaReflection.destroy();
				buffer.mirror.destroy();
			}
		}
	}

	void prepareReflectionPass()
	{
		reflectionPass.width = width;
		reflectionPass.height = height;

		VkFormat fbColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = fbColorFormat;
		image.extent.width = reflectionPass.width;
		image.extent.height = reflectionPass.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &reflectionPass.color.image));
		vkGetImageMemoryRequirements(device, reflectionPass.color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &reflectionPass.color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, reflectionPass.color.image, reflectionPass.color.mem, 0));

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = fbColorFormat;
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = reflectionPass.color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &reflectionPass.color.view));

		VkSamplerCreateInfo samplerInfo = vks::initializers::samplerCreateInfo();
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = samplerInfo.addressModeU;
		samplerInfo.addressModeW = samplerInfo.addressModeU;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &reflectionPass.sampler));

		image.format = fbDepthFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &reflectionPass.depth.image));
		vkGetImageMemoryRequirements(device, reflectionPass.depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &reflectionPass.depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, reflectionPass.depth.image, reflectionPass.depth.mem, 0));

		VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = fbDepthFormat;
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (fbDepthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;
		depthStencilView.image = reflectionPass.depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &reflectionPass.depth.view));

		std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
		attchmentDescriptions[0].format = fbColorFormat;
		attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attchmentDescriptions[1].format = fbDepthFormat;
		attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_NONE_KHR;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &reflectionPass.renderPass));

		VkImageView attachments[2];
		attachments[0] = reflectionPass.color.view;
		attachments[1] = reflectionPass.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = reflectionPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = reflectionPass.width;
		fbufCreateInfo.height = reflectionPass.height;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &reflectionPass.frameBuffer));

		reflectionPass.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		reflectionPass.descriptor.imageView = reflectionPass.color.view;
		reflectionPass.descriptor.sampler = reflectionPass.sampler;
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.sponza.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.plane.loadFromFile(getAssetPath() + "models/plane.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 4),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 4)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// 1. Main Pass / Reflection Pass Layout (Sponza) - ONLY UBO NEEDED NOW!
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMain = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutMainInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsMain);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutMainInfo, nullptr, &descriptorSetLayoutMain));

		// 2. GLTF Materials Layout (Textures)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsGLTF = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutGLTFInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsGLTF);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutGLTFInfo, nullptr, &descriptorSetLayoutGLTF));

		// 3. Mirror Plane Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMirror = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutMirrorInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsMirror);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutMirrorInfo, nullptr, &descriptorSetLayoutMirror));

		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VkDescriptorSetAllocateInfo allocInfoMain = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutMain, 1);

			// Sponza Model (Main)
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMain, &descriptorSets[i].sponzaModel));
			std::vector<VkWriteDescriptorSet> sponzaModelWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].sponzaModel.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(sponzaModelWrites.size()), sponzaModelWrites.data(), 0, nullptr);

			// Sponza Model (Reflection)
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMain, &descriptorSets[i].sponzaReflection));
			std::vector<VkWriteDescriptorSet> sponzaReflectionWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaReflection, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].sponzaReflection.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(sponzaReflectionWrites.size()), sponzaReflectionWrites.data(), 0, nullptr);

			// Mirror Plane
			VkDescriptorSetAllocateInfo allocInfoMirror = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutMirror, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMirror, &descriptorSets[i].mirror));
			std::vector<VkWriteDescriptorSet> mirrorWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].mirror, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].mirror.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].mirror, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &reflectionPass.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(mirrorWrites.size()), mirrorWrites.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayoutMain, descriptorSetLayoutGLTF };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoMain = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoMain, nullptr, &pipelineLayouts.sponzaMain));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoMirror = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayoutMirror, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoMirror, nullptr, &pipelineLayouts.mirror));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		// 1. Sponza Main Pipeline
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.sponzaMain, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sponzaMain));

		// 2. Sponza Reflection Pipeline (Inverted rendering)
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCI.renderPass = reflectionPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sponzaReflection));

		// 3. Mirror Plane Pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/mirror.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/mirror.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.layout = pipelineLayouts.mirror;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.mirror));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.sponzaModel, sizeof(UniformDataScene)));
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.sponzaReflection, sizeof(UniformDataScene)));
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.mirror, sizeof(UniformDataMirror)));

			VK_CHECK_RESULT(buffer.sponzaModel.map());
			VK_CHECK_RESULT(buffer.sponzaReflection.map());
			VK_CHECK_RESULT(buffer.mirror.map());
		}
	}

	void updateUniformBuffers()
	{
		// 1. Main Sponza Render
		uniformDataScene.projection = camera.matrices.perspective;
		uniformDataScene.view = camera.matrices.view;
		uniformDataScene.model = glm::mat4(1.0f);
		uniformDataScene.lightPos[0] = lightPos[0];
		uniformDataScene.lightPos[1] = lightPos[1];
		memcpy(uniformBuffers[currentBuffer].sponzaModel.mapped, &uniformDataScene, sizeof(UniformDataScene));

		// 2. Sponza Reflection Render (INVERTED Y)
		UniformDataScene uniformDataReflect = uniformDataScene;
		glm::mat4 reflectModel = glm::mat4(1.0f);
		reflectModel = glm::translate(reflectModel, glm::vec3(0.0f, -0.005f, 0.0f));
		reflectModel = glm::scale(reflectModel, glm::vec3(1.0f, -1.0f, 1.0f));
		reflectModel = glm::translate(reflectModel, glm::vec3(0.0f, 0.005f, 0.0f));

		uniformDataReflect.model = reflectModel;
		uniformDataReflect.lightPos[0].y = -(uniformDataReflect.lightPos[0].y - (-0.005f)) + (-0.005f);
		uniformDataReflect.lightPos[1].y = -(uniformDataReflect.lightPos[1].y - (-0.005f)) + (-0.005f);

		memcpy(uniformBuffers[currentBuffer].sponzaReflection.mapped, &uniformDataReflect, sizeof(UniformDataScene));

		// 3. Mirror Plane 
		uniformDataMirror.projection = camera.matrices.perspective;
		uniformDataMirror.view = camera.matrices.view;
		uniformDataMirror.model = glm::mat4(1.0f);
		uniformDataMirror.model = glm::translate(uniformDataMirror.model, glm::vec3(0.0f, -0.005f, 0.0f));
		uniformDataMirror.model = glm::scale(uniformDataMirror.model, glm::vec3(15.0f));
		memcpy(uniformBuffers[currentBuffer].mirror.mapped, &uniformDataMirror, sizeof(UniformDataMirror));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		prepareReflectionPass();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		/* -------------------------------------------------------------
			Pass 1: Reflection Rendering (Draw Sponza Upside-Down)
		--------------------------------------------------------------*/
		{
			VkClearValue clearValues[2];
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = reflectionPass.renderPass;
			renderPassBeginInfo.framebuffer = reflectionPass.frameBuffer;
			renderPassBeginInfo.renderArea.extent.width = reflectionPass.width;
			renderPassBeginInfo.renderArea.extent.height = reflectionPass.height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)reflectionPass.width, (float)reflectionPass.height, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(reflectionPass.width, reflectionPass.height, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sponzaReflection);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sponzaMain, 0, 1, &descriptorSets[currentBuffer].sponzaReflection, 0, nullptr);
			models.sponza.draw(cmdBuffer, vkglTF::RenderFlags::BindImages, pipelineLayouts.sponzaMain, 1);

			vkCmdEndRenderPass(cmdBuffer);
		}

		/* -------------------------------------------------------------
			Pass 2: Main Render (Sponza + Floor Mirror)
		--------------------------------------------------------------*/
		{
			VkClearValue clearValues[2];
			clearValues[0].color = defaultClearColor;
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = renderPass;
			renderPassBeginInfo.framebuffer = frameBuffers[currentImageIndex];
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			// Draw normal Sponza
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sponzaMain);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sponzaMain, 0, 1, &descriptorSets[currentBuffer].sponzaModel, 0, nullptr);
			models.sponza.draw(cmdBuffer, vkglTF::RenderFlags::BindImages, pipelineLayouts.sponzaMain, 1);

			// Draw Mirror Plane 
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.mirror);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.mirror, 0, 1, &descriptorSets[currentBuffer].mirror, 0, nullptr);
			models.plane.draw(cmdBuffer);

			drawUI(cmdBuffer);

			vkCmdEndRenderPass(cmdBuffer);
		}

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