/*
 * Vulkan Template
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "volk.h"
#include "Device.hpp"
#include "vk_mem_alloc.h"

#pragma once

class VulkanContext {
public:
	static VkQueue copyQueue;
	static VkQueue graphicsQueue;
	static Device* device;
	static VmaAllocator vmaAllocator;
};

extern VulkanContext vulkanContext;