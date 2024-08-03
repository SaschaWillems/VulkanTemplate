/*
 * Vulkan image abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */ 

#pragma once

#include <vector>
#include "volk.h"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "Device.hpp"
#include "DeviceResource.h"
#include "CommandBuffer.hpp"
#include "VulkanContext.h"

struct ImageCreateInfo {
	std::string name{ "" };
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

enum class ImageUseCase { TransferDestination, ShaderRead };

class Image : public DeviceResource {
private:
	VkDeviceMemory memory{ VK_NULL_HANDLE };
public:
	VkImageType type;
	VkFormat format;
	VkImageLayout currentLayout;
	VkExtent3D extent;
	uint32_t mipLevels;
	uint32_t arrayLayers;

	VkImage handle{ VK_NULL_HANDLE };
	
	Image(ImageCreateInfo createInfo) : DeviceResource(createInfo.name) {
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
		VK_CHECK_RESULT(vkCreateImage(VulkanContext::device->logicalDevice, &CI, nullptr, &handle));
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(VulkanContext::device->logicalDevice, handle, &memReqs);
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VulkanContext::device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(VulkanContext::device->logicalDevice, &memAlloc, nullptr, &memory));
		VK_CHECK_RESULT(vkBindImageMemory(VulkanContext::device->logicalDevice, handle, memory, 0));
		// Keep some values for tracking and making dependent resource creation easier (e.g. views)
		type = createInfo.type;
		format = createInfo.format;
		currentLayout = createInfo.initialLayout;
		extent = createInfo.extent;
		mipLevels = createInfo.mipLevels;
		arrayLayers = createInfo.arrayLayers;
		setDebugName((uint64_t)handle, VK_OBJECT_TYPE_IMAGE);
		setDebugName((uint64_t)memory, VK_OBJECT_TYPE_DEVICE_MEMORY);
	}

	~Image() {
		if (handle != VK_NULL_HANDLE) {
			vkDestroyImage(VulkanContext::device->logicalDevice, handle, nullptr);
		}
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(VulkanContext::device->logicalDevice, memory, nullptr);
		}
	}

	operator VkImage() const {
		return handle;
	}

	VkImageSubresourceRange subresourceRange(VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) const {
		return {
			.aspectMask = aspectMask,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = arrayLayers
		};
	}

	void transition(CommandBuffer* cb, ImageUseCase useCase, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = handle;
		barrier.subresourceRange = subresourceRange();

		VkImageLayout newLayout;
		switch (useCase) {
		case(ImageUseCase::TransferDestination):
			newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			break;
		case (ImageUseCase::ShaderRead):
			newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			break;
		}

		switch (currentLayout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			barrier.srcAccessMask = 0;
			break;
		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		}

		switch (newLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			if (barrier.srcAccessMask == 0) {
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		}

		barrier.oldLayout = currentLayout;
		barrier.newLayout = newLayout;

		cb->insertImageMemoryBarrier(barrier, srcStageMask, dstStageMask);

		currentLayout = newLayout;

	}

};