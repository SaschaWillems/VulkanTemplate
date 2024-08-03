/*
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "DeviceResource.h"

DeviceResource::DeviceResource(const std::string name) : name(name) {};

void DeviceResource::setDebugName(uint64_t handle, VkObjectType type) {
	// @todo: or if validation
	if (VulkanContext::device->hasDebugUtils) {
		VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.objectType = type,
			.objectHandle = handle,
			.pObjectName = name.c_str()
		};
		vkSetDebugUtilsObjectNameEXT(VulkanContext::device->logicalDevice, &objectNameInfo);
	}
}