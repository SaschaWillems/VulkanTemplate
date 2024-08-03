/*
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string.h>
#include "volk.h"
#include "Device.hpp"
#include "VulkanContext.h"

class DeviceResource {
public:
	std::string name{ "" };
	DeviceResource(const std::string name = "");
	void setDebugName(uint64_t handle, VkObjectType type);
};