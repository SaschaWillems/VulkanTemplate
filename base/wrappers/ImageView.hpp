/*
 * Vulkan image view abstraction class
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
#include "Image.hpp"
#include "VulkanContext.h"

class ImageView {
private:
	Image* image = nullptr;
	VkImageViewType type;
	VkFormat format;
	VkImageSubresourceRange range;
	VkImageViewType viewTypeFromImage(Image* image) {
		switch (image->type) {
		case VK_IMAGE_TYPE_1D:
			return image->arrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		case VK_IMAGE_TYPE_2D:
			return image->arrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		case VK_IMAGE_TYPE_3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		default:
			return VK_IMAGE_VIEW_TYPE_2D;
		// @todo: cubemaps
		}
	}
public:
	VkImageView handle;

	ImageView(Image* image) {
		VkImageViewCreateInfo CI{};
		CI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		CI.viewType = viewTypeFromImage(image);
		CI.format = image->format;
		// @todo: from image or argument
		CI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		CI.subresourceRange.levelCount = image->mipLevels;
		CI.subresourceRange.layerCount = image->arrayLayers;
		CI.image = image->handle;
		VK_CHECK_RESULT(vkCreateImageView(VulkanContext::device->logicalDevice, &CI, nullptr, &handle));
	}

	~ImageView() {
		vkDestroyImageView(VulkanContext::device->logicalDevice, handle, nullptr);
	}

	operator VkImageView() const {
		return handle;
	}
};