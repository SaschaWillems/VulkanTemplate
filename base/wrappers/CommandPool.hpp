/*
 * Command pool abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "Device.hpp"

struct CommandPoolCreateInfo {
	vks::VulkanDevice& device;
	uint32_t queueFamilyIndex;
	VkCommandPoolCreateFlags flags;
};

class CommandPool {
private:
	vks::VulkanDevice& device;
public:
	VkCommandPool handle;
	CommandPool(CommandPoolCreateInfo createInfo) : device(createInfo.device) {
		VkCommandPoolCreateInfo CI{};
		CI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		CI.queueFamilyIndex = createInfo.queueFamilyIndex;
		CI.flags = createInfo.flags;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &CI, nullptr, &handle));
	}
	~CommandPool() {
		vkDestroyCommandPool(device, handle, nullptr);
	}
};