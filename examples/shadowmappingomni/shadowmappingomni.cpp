/*
* Vulkan Example - Omni directional shadows using dynamic cube maps
* Modified for 2 Point Lights and Sponza exclusively.
*
* Copyright (C) 2016-2025 by Sascha Willems - www.saschawillems.de
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

class VulkanExample : public VulkanExampleBase
{
public:
	float zNear{ 0.1f };
	float zFar{ 1024.0f };

	struct {
		vkglTF::Model sponza;
	} models;

	// Set your exact light positions here!
	glm::vec4 lightPos[2] = {
		glm::vec4(-3.8f, -3.5f, 0.0f, 40.0f),
		glm::vec4(4.8f, -3.5f, 0.0f, 40.0f)
	};

	struct UniformDataScene {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos[2]; // Passed as an array to the shader
	} uniformDataScene;

	struct UniformDataOffscreen {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos;
	};
	UniformDataOffscreen uniformDataOffscreen[2];

	struct UniformBuffers {
		vks::Buffer sponzaModel;
		std::array<vks::Buffer, 2> offscreen; // One UBO per light for offscreen passes
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct {
		VkPipeline sponzaMain{ VK_NULL_HANDLE };
		VkPipeline offscreen{ VK_NULL_HANDLE };
	} pipelines;

	struct {
		VkPipelineLayout sponzaMain{ VK_NULL_HANDLE };
		VkPipelineLayout offscreen{ VK_NULL_HANDLE };
	} pipelineLayouts;

	struct DescriptorSets {
		VkDescriptorSet sponzaModel{ VK_NULL_HANDLE };
		std::array<VkDescriptorSet, 2> offscreen;
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;

	VkDescriptorSetLayout descriptorSetLayoutMain{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutOffscreen{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutGLTF{ VK_NULL_HANDLE }; // <--- ADDED GLTF LAYOUT

	// 2 Shadow Cubemaps
	std::array<vks::Texture, 2> shadowCubeMaps;
	std::array<std::array<VkImageView, 6>, 2> shadowCubeMapFaceImageViews{};

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct OffscreenPass {
		std::array<VkFramebuffer, 6> frameBuffers;
		FrameBufferAttachment depth;
	};

	VkRenderPass offscreenRenderPass{ VK_NULL_HANDLE };
	std::array<OffscreenPass, 2> offscreenPasses;

	const uint32_t offscreenImageSize{ 1024 };
	const VkFormat offscreenImageFormat{ VK_FORMAT_R32_SFLOAT };
	VkFormat offscreenDepthFormat{ VK_FORMAT_UNDEFINED };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Two Point Light Shadows (Sponza)";
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(90.0f, (float)width / (float)height, zNear, zFar);
		camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
		camera.setPosition(glm::vec3(0.0f, 2.5f, 0.0f));
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		if (device) {
			for (uint32_t l = 0; l < 2; l++) {
				for (uint32_t i = 0; i < 6; i++) {
					vkDestroyImageView(device, shadowCubeMapFaceImageViews[l][i], nullptr);
					vkDestroyFramebuffer(device, offscreenPasses[l].frameBuffers[i], nullptr);
				}
				vkDestroyImageView(device, shadowCubeMaps[l].view, nullptr);
				vkDestroyImage(device, shadowCubeMaps[l].image, nullptr);
				vkDestroySampler(device, shadowCubeMaps[l].sampler, nullptr);
				vkFreeMemory(device, shadowCubeMaps[l].deviceMemory, nullptr);

				vkDestroyImageView(device, offscreenPasses[l].depth.view, nullptr);
				vkDestroyImage(device, offscreenPasses[l].depth.image, nullptr);
				vkFreeMemory(device, offscreenPasses[l].depth.mem, nullptr);
			}

			vkDestroyRenderPass(device, offscreenRenderPass, nullptr);
			vkDestroyPipeline(device, pipelines.sponzaMain, nullptr);
			vkDestroyPipeline(device, pipelines.offscreen, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.sponzaMain, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMain, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutOffscreen, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutGLTF, nullptr); // <--- DESTROY GLTF LAYOUT

			for (auto& buffer : uniformBuffers) {
				buffer.sponzaModel.destroy();
				buffer.offscreen[0].destroy();
				buffer.offscreen[1].destroy();
			}
		}
	}

	void prepareCubeMaps()
	{
		VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		for (uint32_t l = 0; l < 2; l++) {
			shadowCubeMaps[l].width = offscreenImageSize;
			shadowCubeMaps[l].height = offscreenImageSize;

			VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = offscreenImageFormat;
			imageCreateInfo.extent = { shadowCubeMaps[l].width, shadowCubeMaps[l].height, 1 };
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 6;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &shadowCubeMaps[l].image));
			vkGetImageMemoryRequirements(device, shadowCubeMaps[l].image, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &shadowCubeMaps[l].deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device, shadowCubeMaps[l].image, shadowCubeMaps[l].deviceMemory, 0));

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 6;
			vks::tools::setImageLayout(
				layoutCmd,
				shadowCubeMaps[l].image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 1.0f;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = 1.0f;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &shadowCubeMaps[l].sampler));

			VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
			view.image = VK_NULL_HANDLE;
			view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			view.format = offscreenImageFormat;
			view.components = { VK_COMPONENT_SWIZZLE_R };
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			view.subresourceRange.layerCount = 6;
			view.image = shadowCubeMaps[l].image;
			VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &shadowCubeMaps[l].view));

			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.subresourceRange.layerCount = 1;
			view.image = shadowCubeMaps[l].image;

			for (uint32_t i = 0; i < 6; i++)
			{
				view.subresourceRange.baseArrayLayer = i;
				VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &shadowCubeMapFaceImageViews[l][i]));
			}
		}

		vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
	}

	void prepareOffscreenRenderpass()
	{
		VkAttachmentDescription osAttachments[2] = {};
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &offscreenDepthFormat);
		assert(validDepthFormat);

		osAttachments[0].format = offscreenImageFormat;
		osAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		osAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		osAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		osAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		osAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		osAttachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		osAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		osAttachments[1].format = offscreenDepthFormat;
		osAttachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		osAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		osAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		osAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		osAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		osAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		osAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;

		// Memory dependencies for 12 back-to-back renders
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo = vks::initializers::renderPassCreateInfo();
		renderPassCreateInfo.attachmentCount = 2;
		renderPassCreateInfo.pAttachments = osAttachments;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &offscreenRenderPass));
	}

	void prepareOffscreenFramebuffers()
	{
		VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		for (uint32_t l = 0; l < 2; l++) {
			VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = offscreenDepthFormat;
			imageCreateInfo.extent.width = offscreenImageSize;
			imageCreateInfo.extent.height = offscreenImageSize;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

			VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &offscreenPasses[l].depth.image));

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device, offscreenPasses[l].depth.image, &memReqs);

			VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPasses[l].depth.mem));
			VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPasses[l].depth.image, offscreenPasses[l].depth.mem, 0));

			vks::tools::setImageLayout(
				layoutCmd,
				offscreenPasses[l].depth.image,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
			depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			depthStencilView.format = offscreenDepthFormat;
			depthStencilView.flags = 0;
			depthStencilView.subresourceRange = {};
			depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (offscreenDepthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
				depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
			depthStencilView.subresourceRange.baseMipLevel = 0;
			depthStencilView.subresourceRange.levelCount = 1;
			depthStencilView.subresourceRange.baseArrayLayer = 0;
			depthStencilView.subresourceRange.layerCount = 1;
			depthStencilView.image = offscreenPasses[l].depth.image;
			VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &offscreenPasses[l].depth.view));

			VkImageView attachments[2];
			attachments[1] = offscreenPasses[l].depth.view;

			VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
			fbufCreateInfo.renderPass = offscreenRenderPass;
			fbufCreateInfo.attachmentCount = 2;
			fbufCreateInfo.pAttachments = attachments;
			fbufCreateInfo.width = offscreenImageSize;
			fbufCreateInfo.height = offscreenImageSize;
			fbufCreateInfo.layers = 1;

			for (uint32_t i = 0; i < 6; i++) {
				attachments[0] = shadowCubeMapFaceImageViews[l][i];
				VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &offscreenPasses[l].frameBuffers[i]));
			}
		}

		vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.sponza.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 5),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 5)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// 1. Layout for Main Pass (Only Set 0: UBO and 2 Shadow Maps)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMain = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutMainInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsMain);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutMainInfo, nullptr, &descriptorSetLayoutMain));

		// 2. Layout for glTF Materials (Set 1: Sponza's built-in 5 texture bindings)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsGLTF = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutGLTFInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsGLTF);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutGLTFInfo, nullptr, &descriptorSetLayoutGLTF));

		// 3. Layout for Offscreen Passes (Only needs the Uniform Buffer)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsOffscreen = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutOffscreenInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsOffscreen);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutOffscreenInfo, nullptr, &descriptorSetLayoutOffscreen));

		for (auto i = 0; i < uniformBuffers.size(); i++) {
			// Sponza Model (Main pass Set 0)
			VkDescriptorSetAllocateInfo allocInfoMain = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutMain, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMain, &descriptorSets[i].sponzaModel));

			VkDescriptorImageInfo texDescriptor0 = vks::initializers::descriptorImageInfo(shadowCubeMaps[0].sampler, shadowCubeMaps[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			VkDescriptorImageInfo texDescriptor1 = vks::initializers::descriptorImageInfo(shadowCubeMaps[1].sampler, shadowCubeMaps[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			std::vector<VkWriteDescriptorSet> sponzaModelWriteDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].sponzaModel.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptor0),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptor1)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(sponzaModelWriteDescriptorSets.size()), sponzaModelWriteDescriptorSets.data(), 0, nullptr);

			// Offscreen descriptors (One for each light)
			VkDescriptorSetAllocateInfo allocInfoOffscreen = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutOffscreen, 1);
			for (uint32_t l = 0; l < 2; l++) {
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoOffscreen, &descriptorSets[i].offscreen[l]));
				std::vector<VkWriteDescriptorSet> offScreenWriteDescriptorSets = {
					vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen[l], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].offscreen[l].descriptor),
				};
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(offScreenWriteDescriptorSets.size()), offScreenWriteDescriptorSets.data(), 0, nullptr);
			}
		}
	}

	void preparePipelines()
	{
		// 1. CONNECT BOTH LAYOUTS TO THE PIPELINE!
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayoutMain, descriptorSetLayoutGLTF };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoMain = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoMain, nullptr, &pipelineLayouts.sponzaMain));

		// Offscreen pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoOffscreen = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayoutOffscreen, 1);
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		pipelineLayoutCreateInfoOffscreen.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfoOffscreen.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoOffscreen, nullptr, &pipelineLayouts.offscreen));

		// Common Pipeline Setup
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		// Sponza Main pipeline
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
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

		// Offscreen pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE; // Fix to stop light leaking through thin walls
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.layout = pipelineLayouts.offscreen;
		pipelineCI.renderPass = offscreenRenderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.sponzaModel, sizeof(UniformDataScene)));
			VK_CHECK_RESULT(buffer.sponzaModel.map());

			for (uint32_t l = 0; l < 2; l++) {
				VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.offscreen[l], sizeof(UniformDataOffscreen)));
				VK_CHECK_RESULT(buffer.offscreen[l].map());
			}
		}
	}

	void updateUniformBuffers()
	{
		// Update offscreen data for each light
		for (uint32_t l = 0; l < 2; l++) {
			uniformDataOffscreen[l].projection = glm::perspective((float)(M_PI / 2.0), 1.0f, zNear, zFar);
			uniformDataOffscreen[l].view = glm::mat4(1.0f);
			uniformDataOffscreen[l].model = glm::translate(glm::mat4(1.0f), glm::vec3(-lightPos[l].x, -lightPos[l].y, -lightPos[l].z));
			uniformDataOffscreen[l].lightPos = lightPos[l];
			memcpy(uniformBuffers[currentBuffer].offscreen[l].mapped, &uniformDataOffscreen[l], sizeof(UniformDataOffscreen));
		}

		// Update main scene Sponza data
		uniformDataScene.projection = camera.matrices.perspective;
		uniformDataScene.view = camera.matrices.view;
		uniformDataScene.model = glm::mat4(1.0f);
		uniformDataScene.lightPos[0] = lightPos[0];
		uniformDataScene.lightPos[1] = lightPos[1];
		memcpy(uniformBuffers[currentBuffer].sponzaModel.mapped, &uniformDataScene, sizeof(UniformDataScene));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		prepareCubeMaps();
		setupDescriptors();
		prepareOffscreenRenderpass();
		preparePipelines();
		prepareOffscreenFramebuffers();
		prepared = true;
	}

	void updateCubeFace(uint32_t lightIndex, uint32_t faceIndex, VkCommandBuffer commandBuffer)
	{
		VkClearValue clearValues[2];
		clearValues[0].color = { { zFar, zFar, zFar, 1.0f } }; // RESTORED ZFAR SKY CLEAR FIX
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = offscreenRenderPass;
		renderPassBeginInfo.framebuffer = offscreenPasses[lightIndex].frameBuffers[faceIndex];
		renderPassBeginInfo.renderArea.extent.width = offscreenImageSize;
		renderPassBeginInfo.renderArea.extent.height = offscreenImageSize;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		glm::mat4 viewMatrix = glm::mat4(1.0f);
		switch (faceIndex)
		{
		case 0: viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)); viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)); break;
		case 1:	viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)); viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)); break;
		case 2:	viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); break;
		case 3:	viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); break;
		case 4:	viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)); break;
		case 5:	viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)); break;
		}

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdPushConstants(commandBuffer, pipelineLayouts.offscreen, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewMatrix);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets[currentBuffer].offscreen[lightIndex], 0, nullptr);

		models.sponza.draw(commandBuffer);

		vkCmdEndRenderPass(commandBuffer);
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		/*
			Pass 1: Offscreen Rendering
		*/
		{
			VkViewport viewport = vks::initializers::viewport((float)offscreenImageSize, (float)offscreenImageSize, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(offscreenImageSize, offscreenImageSize, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			for (uint32_t l = 0; l < 2; l++) {
				for (uint32_t face = 0; face < 6; face++) {
					updateCubeFace(l, face, cmdBuffer);
				}
			}
		}

		/*
			Pass 2: Main Rendering
		*/
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

			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sponzaMain);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sponzaMain, 0, 1, &descriptorSets[currentBuffer].sponzaModel, 0, nullptr);

			// 2. TELL THE LOADER TO AUTO-BIND ITS TEXTURES INTO SET 1
			models.sponza.draw(cmdBuffer, vkglTF::RenderFlags::BindImages, pipelineLayouts.sponzaMain, 1);

			drawUI(cmdBuffer);

			vkCmdEndRenderPass(cmdBuffer);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	virtual void render()
	{
		if (!prepared)
			return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}
};

VULKAN_EXAMPLE_MAIN()