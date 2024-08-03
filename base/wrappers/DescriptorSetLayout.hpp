/*
 * Vulkan descriptor set layout abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "VulkanContext.h"

struct DescriptorSetLayoutCreateInfo {
	bool descriptorIndexing = false;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
};

class DescriptorSetLayout {
public:
	VkDescriptorSetLayout handle = VK_NULL_HANDLE;

	DescriptorSetLayout(DescriptorSetLayoutCreateInfo createInfo) {
		VkDescriptorSetLayoutCreateInfo CI = vks::initializers::descriptorSetLayoutCreateInfo(createInfo.bindings.data(), static_cast<uint32_t>(createInfo.bindings.size()));
		VkDescriptorSetLayoutBindingFlagsCreateInfo setLayoutBindingFlags{};
			setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
		if (createInfo.descriptorIndexing) {
			// @todo: Right now only support descriptor index for layout with single binding
			assert(createInfo.bindings.size() == 1);
			setLayoutBindingFlags.bindingCount = 1;
			VkDescriptorBindingFlags descriptorBindingFlags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
			setLayoutBindingFlags.pBindingFlags = &descriptorBindingFlags;
			CI.pNext = &setLayoutBindingFlags;
		}
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(VulkanContext::device->logicalDevice, &CI, nullptr, &handle));
	}

	~DescriptorSetLayout() {
		vkDestroyDescriptorSetLayout(VulkanContext::device->logicalDevice, handle, nullptr);
	}
};