/*
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string.h>
#include "volk.h"
#include "Device.hpp"

class DeviceResource {
public:
	Device& device;
	std::string name{ "" };
	DeviceResource(Device& device, const std::string name = "");
	void setDebugName(uint64_t handle, VkObjectType type);
};