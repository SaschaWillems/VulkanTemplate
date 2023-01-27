/*
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string.h>
#include "Device.hpp"

class IDeviceResource {
public:
	std::string name{ "" };
	vks::VulkanDevice& device;
	IDeviceResource(vks::VulkanDevice& device) : device(device) {};
};