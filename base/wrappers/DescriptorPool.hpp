/*
* Descriptor pool abstraction class
*
* Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "DeviceResource.h"
#include "Device.hpp"

struct DescriptorPoolCreateInfo {
	const std::string name{ "" };
	Device& device;
	uint32_t maxSets;
	std::vector<VkDescriptorPoolSize> poolSizes;
};

class DescriptorPool : public DeviceResource {
public:
	VkDescriptorPool handle;
	
	DescriptorPool(DescriptorPoolCreateInfo createInfo) : DeviceResource(createInfo.device, createInfo.name) {
		assert(createInfo.poolSizes.size() > 0);
		assert(createInfo.maxSets > 0);
		VkDescriptorPoolCreateInfo CI{};
		CI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		CI.poolSizeCount = static_cast<uint32_t>(createInfo.poolSizes.size());
		CI.pPoolSizes = createInfo.poolSizes.data();
		CI.maxSets = createInfo.maxSets;
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &CI, nullptr, &handle));
	}

	~DescriptorPool() {
		vkDestroyDescriptorPool(device, handle, nullptr);
	}
};