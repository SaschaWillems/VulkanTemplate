/*
 * Vulkan image abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */ 

#pragma once

#include <vector>
#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "Device.hpp"

struct ImageCreateInfo {
	vks::VulkanDevice& device;
	VkImageType type;
	VkFormat format;
	VkExtent3D extent;
	uint32_t mipLevels{ 1 };
	uint32_t arrayLayers{ 1 };
	VkSampleCountFlagBits samples{ VK_SAMPLE_COUNT_1_BIT };
	VkImageTiling tiling{ VK_IMAGE_TILING_OPTIMAL };
	VkImageUsageFlags usage;
	VkSharingMode sharingMode{ VK_SHARING_MODE_EXCLUSIVE };
	uint32_t queueFamilyIndexCount{ 0 };
	const uint32_t* pQueueFamilyIndices{ nullptr };
	VkImageLayout initialLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

class Image {
private:
	VkDeviceMemory memory;
public:
	vks::VulkanDevice& device;
	VkImageType type;
	VkFormat format;
	VkImageLayout currentLayout;
	VkExtent3D extent;
	uint32_t mipLevels;
	uint32_t arrayLayers;

	VkImage handle;
	
	Image(ImageCreateInfo createInfo) : device(createInfo.device) {
		VkImageCreateInfo CI{};
		CI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		CI.imageType = createInfo.type;
		CI.format = createInfo.format;
		CI.extent = createInfo.extent;
		CI.mipLevels = createInfo.mipLevels;
		CI.arrayLayers = createInfo.arrayLayers;
		CI.samples = createInfo.samples;
		CI.tiling = createInfo.tiling;
		CI.usage = createInfo.usage;
		CI.sharingMode = createInfo.sharingMode;
		VK_CHECK_RESULT(vkCreateImage(device, &CI, nullptr, &handle));
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, handle, &memReqs);
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = device.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, handle, memory, 0));
		// Keep some values for tracking and making dependent resource creation easier (e.g. views)
		type = createInfo.type;
		format = createInfo.format;
		currentLayout = createInfo.initialLayout;
		extent = createInfo.extent;
		mipLevels = createInfo.mipLevels;
		arrayLayers = createInfo.arrayLayers;
	}

	~Image() {
		vkDestroyImage(device, handle, nullptr);
	}

	operator VkImage() const {
		return handle;
	}
};