/*
* Vulkan Example - Omni shadows + Planar Reflections
* Modified for 2 Point Lights, Sponza, and a Mirror Plane.
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
		vkglTF::Model plane; // <--- The Mirror Surface
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

	struct UniformDataOffscreen {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos;
	};
	UniformDataOffscreen uniformDataOffscreen[2];

	struct UniformDataMirror {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	} uniformDataMirror;

	struct UniformBuffers {
		vks::Buffer sponzaModel;
		std::array<vks::Buffer, 2> offscreen;
		vks::Buffer sponzaReflection; // <--- Sponza rendered upside-down
		vks::Buffer mirror;           // <--- The mirror plane
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct {
		VkPipeline sponzaMain{ VK_NULL_HANDLE };
		VkPipeline offscreen{ VK_NULL_HANDLE };
		VkPipeline sponzaReflection{ VK_NULL_HANDLE }; // <--- New pass pipeline
		VkPipeline mirror{ VK_NULL_HANDLE };           // <--- Mirror rendering pipeline
	} pipelines;

	struct {
		VkPipelineLayout sponzaMain{ VK_NULL_HANDLE };
		VkPipelineLayout offscreen{ VK_NULL_HANDLE };
		VkPipelineLayout mirror{ VK_NULL_HANDLE };
	} pipelineLayouts;

	struct DescriptorSets {
		VkDescriptorSet sponzaModel{ VK_NULL_HANDLE };
		std::array<VkDescriptorSet, 2> offscreen;
		VkDescriptorSet sponzaReflection{ VK_NULL_HANDLE };
		VkDescriptorSet mirror{ VK_NULL_HANDLE };
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;

	VkDescriptorSetLayout descriptorSetLayoutMain{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutOffscreen{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutGLTF{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutMirror{ VK_NULL_HANDLE }; // <--- Layout for mirror

	// Shadows
	std::array<vks::Texture, 2> shadowCubeMaps;
	std::array<std::array<VkImageView, 6>, 2> shadowCubeMapFaceImageViews{};
	VkRenderPass offscreenRenderPass{ VK_NULL_HANDLE };

	struct FrameBufferAttachment { VkImage image; VkDeviceMemory mem; VkImageView view; };
	struct OffscreenPass { std::array<VkFramebuffer, 6> frameBuffers; FrameBufferAttachment depth; };
	std::array<OffscreenPass, 2> offscreenPasses;

	const uint32_t offscreenImageSize{ 1024 };
	const VkFormat offscreenImageFormat{ VK_FORMAT_R32_SFLOAT };
	VkFormat offscreenDepthFormat{ VK_FORMAT_UNDEFINED };

	// Reflection
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
		title = "Omni Shadows + Planar Reflection";
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(90.0f, (float)width / (float)height, zNear, zFar);
		camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
		camera.setPosition(glm::vec3(0.0f, 2.5f, 0.0f));
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		if (device) {
			// Clean up Reflection Pass
			vkDestroyImageView(device, reflectionPass.color.view, nullptr);
			vkDestroyImage(device, reflectionPass.color.image, nullptr);
			vkFreeMemory(device, reflectionPass.color.mem, nullptr);
			vkDestroyImageView(device, reflectionPass.depth.view, nullptr);
			vkDestroyImage(device, reflectionPass.depth.image, nullptr);
			vkFreeMemory(device, reflectionPass.depth.mem, nullptr);
			vkDestroyRenderPass(device, reflectionPass.renderPass, nullptr);
			vkDestroySampler(device, reflectionPass.sampler, nullptr);
			vkDestroyFramebuffer(device, reflectionPass.frameBuffer, nullptr);

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
			vkDestroyPipeline(device, pipelines.sponzaReflection, nullptr);
			vkDestroyPipeline(device, pipelines.mirror, nullptr);

			vkDestroyPipelineLayout(device, pipelineLayouts.sponzaMain, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.mirror, nullptr);

			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMain, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutOffscreen, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutGLTF, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMirror, nullptr);

			for (auto& buffer : uniformBuffers) {
				buffer.sponzaModel.destroy();
				buffer.sponzaReflection.destroy();
				buffer.mirror.destroy();
				buffer.offscreen[0].destroy();
				buffer.offscreen[1].destroy();
			}
		}
	}

	void prepareReflectionPass()
	{
		reflectionPass.width = width;   // 'width' is the main window width
		reflectionPass.height = height; // 'height' is the main window height

		// OR for the Industry Standard Half-Res test:
		// reflectionPass.width = width / 2;
		// reflectionPass.height = height / 2;

		VkFormat fbColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		// Color attachment
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

		// Create sampler
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

		// Depth stencil attachment
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

		// Render Pass setup
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

	void prepareCubeMaps() { /* Keep your existing shadow cubemap setup here */
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
	void prepareOffscreenRenderpass() { /* Keep existing setup */
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
	void prepareOffscreenFramebuffers() { /* Keep existing setup */
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
		models.plane.loadFromFile(getAssetPath() + "models/plane.gltf", vulkanDevice, queue, glTFLoadingFlags); // <--- LOAD MIRROR PLANE
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 8), // Increased for reflections
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 8)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 8);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// 1. Main Pass / Reflection Pass Layout (Sponza)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMain = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutMainInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsMain);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutMainInfo, nullptr, &descriptorSetLayoutMain));

		// 2. GLTF Materials Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsGLTF = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutGLTFInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsGLTF);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutGLTFInfo, nullptr, &descriptorSetLayoutGLTF));

		// 3. Shadow Pass Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsOffscreen = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutOffscreenInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsOffscreen);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutOffscreenInfo, nullptr, &descriptorSetLayoutOffscreen));

		// 4. Mirror Plane Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMirror = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // Reflection Texture
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutMirrorInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindingsMirror);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutMirrorInfo, nullptr, &descriptorSetLayoutMirror));

		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VkDescriptorImageInfo texDescriptor0 = vks::initializers::descriptorImageInfo(shadowCubeMaps[0].sampler, shadowCubeMaps[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			VkDescriptorImageInfo texDescriptor1 = vks::initializers::descriptorImageInfo(shadowCubeMaps[1].sampler, shadowCubeMaps[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// Sponza Model (Main)
			VkDescriptorSetAllocateInfo allocInfoMain = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutMain, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMain, &descriptorSets[i].sponzaModel));
			std::vector<VkWriteDescriptorSet> sponzaModelWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].sponzaModel.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptor0),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaModel, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptor1)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(sponzaModelWrites.size()), sponzaModelWrites.data(), 0, nullptr);

			// Sponza Model (Reflection Pass - uses same layout/textures, but different UBO)
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMain, &descriptorSets[i].sponzaReflection));
			std::vector<VkWriteDescriptorSet> sponzaReflectionWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaReflection, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].sponzaReflection.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaReflection, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptor0),
				vks::initializers::writeDescriptorSet(descriptorSets[i].sponzaReflection, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptor1)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(sponzaReflectionWrites.size()), sponzaReflectionWrites.data(), 0, nullptr);

			// Mirror Plane
			VkDescriptorSetAllocateInfo allocInfoMirror = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutMirror, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoMirror, &descriptorSets[i].mirror));
			std::vector<VkWriteDescriptorSet> mirrorWrites = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].mirror, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].mirror.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].mirror, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &reflectionPass.descriptor) // Binds the rendered reflection
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(mirrorWrites.size()), mirrorWrites.data(), 0, nullptr);

			// Offscreen descriptors (Shadows)
			VkDescriptorSetAllocateInfo allocInfoOffscreen = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutOffscreen, 1);
			for (uint32_t l = 0; l < 2; l++) {
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoOffscreen, &descriptorSets[i].offscreen[l]));
				std::vector<VkWriteDescriptorSet> offScreenWrites = {
					vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen[l], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].offscreen[l].descriptor),
				};
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(offScreenWrites.size()), offScreenWrites.data(), 0, nullptr);
			}
		}
	}

	void preparePipelines()
	{
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayoutMain, descriptorSetLayoutGLTF };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoMain = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoMain, nullptr, &pipelineLayouts.sponzaMain));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoMirror = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayoutMirror, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoMirror, nullptr, &pipelineLayouts.mirror));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfoOffscreen = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayoutOffscreen, 1);
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		pipelineLayoutCreateInfoOffscreen.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfoOffscreen.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfoOffscreen, nullptr, &pipelineLayouts.offscreen));

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
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT; // <--- MUST flip culling for inverted render!
		pipelineCI.renderPass = reflectionPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sponzaReflection));

		// 3. Mirror Plane Pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/mirror.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/mirror.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.layout = pipelineLayouts.mirror;
		pipelineCI.renderPass = renderPass; // Rendered in main pass
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.mirror));

		// 4. Shadow Pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
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
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.sponzaReflection, sizeof(UniformDataScene)));
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.mirror, sizeof(UniformDataMirror)));

			VK_CHECK_RESULT(buffer.sponzaModel.map());
			VK_CHECK_RESULT(buffer.sponzaReflection.map());
			VK_CHECK_RESULT(buffer.mirror.map());

			for (uint32_t l = 0; l < 2; l++) {
				VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.offscreen[l], sizeof(UniformDataOffscreen)));
				VK_CHECK_RESULT(buffer.offscreen[l].map());
			}
		}
	}

	void updateUniformBuffers()
	{
		// 1. Shadow Passes
		for (uint32_t l = 0; l < 2; l++) {
			uniformDataOffscreen[l].projection = glm::perspective((float)(M_PI / 2.0), 1.0f, zNear, zFar);
			uniformDataOffscreen[l].view = glm::mat4(1.0f);
			uniformDataOffscreen[l].model = glm::translate(glm::mat4(1.0f), glm::vec3(-lightPos[l].x, -lightPos[l].y, -lightPos[l].z));
			uniformDataOffscreen[l].lightPos = lightPos[l];
			memcpy(uniformBuffers[currentBuffer].offscreen[l].mapped, &uniformDataOffscreen[l], sizeof(UniformDataOffscreen));
		}

		// 2. Main Sponza Render
		uniformDataScene.projection = camera.matrices.perspective;
		uniformDataScene.view = camera.matrices.view;
		uniformDataScene.model = glm::mat4(1.0f);
		uniformDataScene.lightPos[0] = lightPos[0];
		uniformDataScene.lightPos[1] = lightPos[1];
		memcpy(uniformBuffers[currentBuffer].sponzaModel.mapped, &uniformDataScene, sizeof(UniformDataScene));

		// -------------------------------------------------------------
				// 3. Sponza Reflection Render (INVERTED Y)
				// -------------------------------------------------------------
		UniformDataScene uniformDataReflect = uniformDataScene;

		// Mathematically reflect the scene across the mirror's height (Y = -0.005f)
		glm::mat4 reflectModel = glm::mat4(1.0f);
		// Step 3: Move it back to the mirror's height
		reflectModel = glm::translate(reflectModel, glm::vec3(0.0f, -0.005f, 0.0f));
		// Step 2: Flip it upside down
		reflectModel = glm::scale(reflectModel, glm::vec3(1.0f, -1.0f, 1.0f));
		// Step 1: Shift the world so the mirror is at origin (0.0)
		reflectModel = glm::translate(reflectModel, glm::vec3(0.0f, 0.005f, 0.0f));

		uniformDataReflect.model = reflectModel;

		// We must also perfectly reflect the lights across the Y = -0.005f plane!
		// Formula: reflected_Y = -(light_Y - plane_Y) + plane_Y
		uniformDataReflect.lightPos[0].y = -(uniformDataReflect.lightPos[0].y - (-0.005f)) + (-0.005f);
		uniformDataReflect.lightPos[1].y = -(uniformDataReflect.lightPos[1].y - (-0.005f)) + (-0.005f);

		memcpy(uniformBuffers[currentBuffer].sponzaReflection.mapped, &uniformDataReflect, sizeof(UniformDataScene));

		// -------------------------------------------------------------
		// 4. Mirror Plane 
		// -------------------------------------------------------------
		uniformDataMirror.projection = camera.matrices.perspective;
		uniformDataMirror.view = camera.matrices.view;
		uniformDataMirror.model = glm::mat4(1.0f);
		// Raise the plane slightly (Negative Y is UP in Vulkan!)
		uniformDataMirror.model = glm::translate(uniformDataMirror.model, glm::vec3(0.0f, -0.005f, 0.0f));
		// Scale it to cover the entire Sponza floor
		uniformDataMirror.model = glm::scale(uniformDataMirror.model, glm::vec3(15.0f));
		memcpy(uniformBuffers[currentBuffer].mirror.mapped, &uniformDataMirror, sizeof(UniformDataMirror));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		prepareCubeMaps();
		prepareReflectionPass();
		setupDescriptors();
		prepareOffscreenRenderpass();
		preparePipelines();
		prepareOffscreenFramebuffers();
		prepared = true;
	}

	void updateCubeFace(uint32_t lightIndex, uint32_t faceIndex, VkCommandBuffer commandBuffer)
	{
		VkClearValue clearValues[2];
		clearValues[0].color = { { zFar, zFar, zFar, 1.0f } };
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

		/* -------------------------------------------------------------
			Pass 1: Shadow Maps (12 depth renders)
		--------------------------------------------------------------*/
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

		/* -------------------------------------------------------------
			Pass 2: Reflection Rendering (Draw Sponza Upside-Down)
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
			Pass 3: Main Render (Sponza + Floor Mirror)
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