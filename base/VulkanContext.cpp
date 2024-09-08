/*
 * Vulkan Template
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanContext.h"

VulkanContext vulkanContext{};

VkQueue VulkanContext::copyQueue = VK_NULL_HANDLE;
VkQueue VulkanContext::graphicsQueue = VK_NULL_HANDLE;
Device* VulkanContext::device = nullptr;
VmaAllocator VulkanContext::vmaAllocator = VK_NULL_HANDLE;
