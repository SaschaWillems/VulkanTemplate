/*
 * Command pool abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "volk.h"
#include "DeviceResource.h"
#include "VulkanTools.h"
#include "Device.hpp"

struct CommandPoolCreateInfo {
	const std::string name{ "" };
	Device& device;
	uint32_t queueFamilyIndex;
	VkCommandPoolCreateFlags flags;
};

class CommandPool : public DeviceResource {
public:
	VkCommandPool handle;
	CommandPool(CommandPoolCreateInfo createInfo) : DeviceResource(createInfo.device, createInfo.name) {
		VkCommandPoolCreateInfo CI = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = createInfo.flags,
			.queueFamilyIndex = createInfo.queueFamilyIndex,
		};
		VK_CHECK_RESULT(vkCreateCommandPool(device, &CI, nullptr, &handle));
		setDebugName((uint64_t)handle, VK_OBJECT_TYPE_COMMAND_POOL);
	}
	~CommandPool() {
		vkDestroyCommandPool(device, handle, nullptr);
	}
};