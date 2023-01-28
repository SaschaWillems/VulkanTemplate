/*
 * Vulkan descriptor set layout abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"

struct DescriptorSetLayoutCreateInfo {
	vks::VulkanDevice& device;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
};


class DescriptorSetLayout {
private:
	vks::VulkanDevice& device;
public:
	VkDescriptorSetLayout handle = VK_NULL_HANDLE;

	DescriptorSetLayout(DescriptorSetLayoutCreateInfo createInfo) : device(createInfo.device) {
		this->device = device;
		VkDescriptorSetLayoutCreateInfo CI = vks::initializers::descriptorSetLayoutCreateInfo(createInfo.bindings.data(), static_cast<uint32_t>(createInfo.bindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &CI, nullptr, &handle));
	}

	~DescriptorSetLayout() {
		vkDestroyDescriptorSetLayout(device, handle, nullptr);
	}
};