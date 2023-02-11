/*
 * Vulkan Template
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "volk.h"
#include "Device.hpp"

#pragma once

class VulkanContext {
public:
	static VkQueue copyQueue;
	static VkQueue graphicsQueue;
	static Device* device;
};

extern VulkanContext vulkanContext;