/*
 * Vulkan pipeline abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include "volk.h"
#if defined(__ANDROID__)
#include "Android.h"
#endif
#include "Device.hpp"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "PipelineLayout.hpp"

struct PipelineCreateInfo {
	vks::VulkanDevice& device;
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };
	std::vector<std::string> shaders{};
	VkPipelineCache cache{ VK_NULL_HANDLE };
	VkPipelineLayout layout;
	const void* pNext{ nullptr };
	VkPipelineCreateFlags flags;
	uint32_t stageCount;
	//const VkPipelineShaderStageCreateInfo* pStages;
	const VkPipelineVertexInputStateCreateInfo* vertexInputState{ nullptr };
	const VkPipelineInputAssemblyStateCreateInfo* inputAssemblyState{ nullptr };
	const VkPipelineTessellationStateCreateInfo* tessellationState{ nullptr };
	const VkPipelineViewportStateCreateInfo* viewportState{ nullptr };
	const VkPipelineRasterizationStateCreateInfo* rasterizationState{ nullptr };
	const VkPipelineMultisampleStateCreateInfo* multisampleState{ nullptr };
	const VkPipelineDepthStencilStateCreateInfo* depthStencilState{ nullptr };
	const VkPipelineColorBlendStateCreateInfo* colorBlendState{ nullptr };
	const VkPipelineDynamicStateCreateInfo* dynamicState{ nullptr };
	VkRenderPass renderPass;
	uint32_t subpass;
	VkPipeline basePipelineHandle;
	int32_t basePipelineIndex;
};

class Pipeline {
private:
	vks::VulkanDevice& device;
	VkPipeline pso = VK_NULL_HANDLE;
	std::vector<VkShaderModule> shaderModules{};
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};
	
	void addShader(std::string filename) {
		// Get shader stage from file extension (.spv is omitted)
		size_t extpos = filename.find('.');
		size_t extend = filename.find('.', extpos + 1);
		assert(extpos != std::string::npos);
		std::string ext = filename.substr(extpos + 1, extend - extpos - 1);
		VkShaderStageFlagBits shaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		if (ext == "vert") { shaderStage = VK_SHADER_STAGE_VERTEX_BIT; }
		if (ext == "frag") { shaderStage = VK_SHADER_STAGE_FRAGMENT_BIT; }
		assert(shaderStage != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM);
		// Load from file
		size_t shaderSize{ 0 };
		char* shaderCode = nullptr;
#if defined(__ANDROID__)
		// Load shader from compressed asset
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		shaderSize = AAsset_getLength(asset);
		shaderCode = new char[shaderSize];
		AAsset_read(asset, shaderCode, shaderSize);
		AAsset_close(asset);
#else
		std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);
		if (is.is_open()) {
			shaderSize = is.tellg();
			is.seekg(0, std::ios::beg);
			shaderCode = new char[shaderSize];
			is.read(shaderCode, shaderSize);
			is.close();
		}
		else {
			std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
		}
#endif
		assert(shaderSize > 0);

		VkShaderModuleCreateInfo moduleCreateInfo{};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = shaderSize;
		moduleCreateInfo.pCode = (uint32_t*)shaderCode;
		VkShaderModule shaderModule;
		VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule));
		delete[] shaderCode;
		shaderModules.push_back(shaderModule);

		VkPipelineShaderStageCreateInfo shaderStageCI{};
		shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStageCI.stage = shaderStage;
		shaderStageCI.module = shaderModule;
		shaderStageCI.pName = "main";
		shaderStages.push_back(shaderStageCI);
	}
public:
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };

	Pipeline(PipelineCreateInfo createInfo) : device(createInfo.device) {
		for (auto& shader : createInfo.shaders) {
			addShader(shader);
		}
		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.layout = createInfo.layout;
		pipelineCI.pVertexInputState = createInfo.vertexInputState;
		pipelineCI.pInputAssemblyState = createInfo.inputAssemblyState;
		pipelineCI.pTessellationState = createInfo.tessellationState;
		pipelineCI.pViewportState = createInfo.viewportState;
		pipelineCI.pRasterizationState = createInfo.rasterizationState;
		pipelineCI.pMultisampleState = createInfo.multisampleState;
		pipelineCI.pDepthStencilState = createInfo.depthStencilState;
		pipelineCI.pColorBlendState = createInfo.colorBlendState;
		pipelineCI.pDynamicState = createInfo.dynamicState;
		pipelineCI.pNext = createInfo.pNext;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, createInfo.cache, 1, &pipelineCI, nullptr, &pso));
		
		// Shader modules can be safely destroyed after pipeline creation
		for (auto& shaderModule : shaderModules) {
			vkDestroyShaderModule(device, shaderModule, nullptr);
		}
		shaderModules.clear();

		bindPoint = createInfo.bindPoint;
		// @todo: pstages
		// @todo: set stypes for stuff != null
	};

	~Pipeline() {
		vkDestroyPipeline(device, pso, nullptr);
	}

	operator VkPipeline() { return pso; };
};
