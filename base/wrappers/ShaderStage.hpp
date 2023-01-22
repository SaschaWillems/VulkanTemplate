/*
 * Vulkan shader abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#if defined(__ANDROID__)
#include "Android.h"
#endif

class ShaderStage {
private:
	VkDevice device = VK_NULL_HANDLE;
	VkShaderModule shaderModule = VK_NULL_HANDLE;
public:
	VkPipelineShaderStageCreateInfo createInfo{};

	operator VkPipelineShaderStageCreateInfo() { return createInfo; };

	ShaderStage(VkDevice device, const std::string filename, VkShaderStageFlagBits stage) {
		this->device = device;
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

		if (is.is_open())
		{
			shaderSize = is.tellg();
			is.seekg(0, std::ios::beg);
			shaderCode = new char[shaderSize];
			is.read(shaderCode, shaderSize);
			is.close();
		} else {
			std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
		}

#endif
		assert(shaderSize > 0);

		VkShaderModuleCreateInfo moduleCreateInfo{};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = shaderSize;
		moduleCreateInfo.pCode = (uint32_t*)shaderCode;
		VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule));

		delete[] shaderCode;

		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		createInfo.stage = stage;
		createInfo.module = shaderModule;
		createInfo.pName = "main";
	}

	~ShaderStage() {
		if (device && shaderModule) {
			vkDestroyShaderModule(device, shaderModule, nullptr);
		}
	}
};
