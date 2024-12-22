/*
 * Sampler abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "Device.hpp"
#include "VulkanContext.h"

struct SamplerCreateInfo {
	std::string name{ "" };
	VkFilter magFilter = VK_FILTER_LINEAR;
	VkFilter minFilter = VK_FILTER_LINEAR;
	VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	float mipLodBias = 0.0f;
	VkBool32 anisotropyEnable = VK_FALSE;
	float maxAnisotropy = 0.0f;
	VkBool32 compareEnable = VK_FALSE;
	VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
	float minLod = 0.0f;
	float maxLod = 0.0f;
	VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	VkBool32 unnormalizedCoordinates = VK_FALSE;
};

class Sampler : public DeviceResource {
public:
	VkSampler handle{ VK_NULL_HANDLE };
	VkDescriptorImageInfo descriptor{};

	Sampler(SamplerCreateInfo createInfo) : DeviceResource(createInfo.name) {
		VkSamplerCreateInfo CI{};
		CI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		CI.magFilter = createInfo.magFilter;
		CI.minFilter = createInfo.minFilter;
		CI.mipmapMode = createInfo.mipmapMode;
		CI.addressModeU = createInfo.addressModeU;
		CI.addressModeV = createInfo.addressModeV;
		CI.addressModeW = createInfo.addressModeW;
		CI.mipLodBias = createInfo.mipLodBias;
		CI.anisotropyEnable = createInfo.anisotropyEnable;
		CI.maxAnisotropy = createInfo.maxAnisotropy;
		CI.compareEnable = createInfo.compareEnable;
		CI.compareOp = createInfo.compareOp;
		CI.minLod = createInfo.minLod;
		CI.maxLod = createInfo.maxLod;
		CI.borderColor = createInfo.borderColor;
		CI.unnormalizedCoordinates = createInfo.unnormalizedCoordinates;
		VK_CHECK_RESULT(vkCreateSampler(VulkanContext::device->logicalDevice, &CI, nullptr, &handle));
		descriptor.sampler = handle;
	}

	~Sampler() {
		vkDestroySampler(VulkanContext::device->logicalDevice, handle, nullptr);
	}

	operator VkSampler() const {
		return handle;
	}
};