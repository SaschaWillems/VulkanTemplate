/*
* Vulkan texture loader
*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "volk.h"

#include <ktx.h>
#include <ktxvulkan.h>

#include "VulkanTools.h"
#include "Device.hpp"
#include "VulkanContext.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace vks
{
	struct TextureCreateInfo {
		std::string filename;
		VkFormat format;
		VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		bool createSampler = true;
		VkFilter magFilter = VK_FILTER_LINEAR;
		VkFilter minFilter = VK_FILTER_LINEAR;
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	};

	/** @brief Vulkan texture base class */
	class Texture {
	public:
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;

		void updateDescriptor()
		{
			descriptor.sampler = sampler;
			descriptor.imageView = view;
			descriptor.imageLayout = imageLayout;
		}

		void destroy()
		{
			vkDestroyImageView(VulkanContext::device->logicalDevice, view, nullptr);
			vkDestroyImage(VulkanContext::device->logicalDevice, image, nullptr);
			if (sampler)
			{
				vkDestroySampler(VulkanContext::device->logicalDevice, sampler, nullptr);
			}
			vkFreeMemory(VulkanContext::device->logicalDevice, deviceMemory, nullptr);
		}

		ktxResult loadKTXFile(std::string filename, ktxTexture **target)
		{
			ktxResult result = KTX_SUCCESS;
#if defined(__ANDROID__)
			AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			if (!asset) {
				vks::tools::exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
			}
			size_t size = AAsset_getLength(asset);
			assert(size > 0);
			ktx_uint8_t *textureData = new ktx_uint8_t[size];
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);
			result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
			delete[] textureData;
#else
			if (!vks::tools::fileExists(filename)) {
				vks::tools::exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
			}
			result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);			
#endif		
			return result;
		}
	};

	class Texture2D : public Texture {
	public:
		Texture2D(TextureCreateInfo createInfo)
		{
			ktxTexture* ktxTexture;
			ktxResult result = loadKTXFile(createInfo.filename, &ktxTexture);
			assert(result == KTX_SUCCESS);

			width = ktxTexture->baseWidth;
			height = ktxTexture->baseHeight;
			mipLevels = ktxTexture->numLevels;

			ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
			ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

			// Get device properites for the requested texture format
			VkFormatProperties formatProperties;
			vkGetPhysicalDeviceFormatProperties(VulkanContext::device->physicalDevice, createInfo.format, &formatProperties);

			VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;
			VkCommandBuffer copyCmd = VulkanContext::device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;
			VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
			bufferCreateInfo.size = ktxTextureSize;
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateBuffer(VulkanContext::device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
			vkGetBufferMemoryRequirements(VulkanContext::device->logicalDevice, stagingBuffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = VulkanContext::device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(VulkanContext::device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(VulkanContext::device->logicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(VulkanContext::device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, ktxTextureData, ktxTextureSize);
			vkUnmapMemory(VulkanContext::device->logicalDevice, stagingMemory);

			// Setup buffer copy regions for each mip level
			std::vector<VkBufferImageCopy> bufferCopyRegions;

			for (uint32_t i = 0; i < mipLevels; i++)
			{
				ktx_size_t offset;
				KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
				assert(result == KTX_SUCCESS);

				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = i;
				bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> i;
				bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> i;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);
			}

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = createInfo.format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = createInfo.imageUsageFlags;
			// Ensure that the TRANSFER_DST bit is set for staging
			if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			VK_CHECK_RESULT(vkCreateImage(VulkanContext::device->logicalDevice, &imageCreateInfo, nullptr, &image));

			vkGetImageMemoryRequirements(VulkanContext::device->logicalDevice, image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;

			memAllocInfo.memoryTypeIndex = VulkanContext::device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(VulkanContext::device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(VulkanContext::device->logicalDevice, image, deviceMemory, 0));

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 1;

			// Image barrier for optimal image (target)
			// Optimal image will be used as destination for the copy
			vks::tools::setImageLayout(
				copyCmd,
				image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy mip levels from staging buffer
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			// Change texture image layout to shader read after all mip levels have been copied
			this->imageLayout = imageLayout;
			vks::tools::setImageLayout(
				copyCmd,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				imageLayout,
				subresourceRange);

			// @todo: transfer queue
			VulkanContext::device->flushCommandBuffer(copyCmd, VulkanContext::graphicsQueue);

			// Clean up staging resources
			vkFreeMemory(VulkanContext::device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(VulkanContext::device->logicalDevice, stagingBuffer, nullptr);
			
			ktxTexture_Destroy(ktxTexture);

			VkImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCreateInfo.format = createInfo.format;
			viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			viewCreateInfo.subresourceRange.levelCount = mipLevels;
			viewCreateInfo.image = image;
			VK_CHECK_RESULT(vkCreateImageView(VulkanContext::device->logicalDevice, &viewCreateInfo, nullptr, &view));

			if (createInfo.createSampler) {
				VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
				samplerCreateInfo.magFilter = createInfo.magFilter;
				samplerCreateInfo.minFilter = createInfo.minFilter;
				samplerCreateInfo.mipmapMode = createInfo.mipmapMode;
				samplerCreateInfo.addressModeU = createInfo.addressModeU;
				samplerCreateInfo.addressModeV = createInfo.addressModeV;
				samplerCreateInfo.addressModeW = createInfo.addressModeV;
				samplerCreateInfo.mipLodBias = 0.0f;
				samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
				samplerCreateInfo.minLod = 0.0f;
				samplerCreateInfo.maxLod = (float)mipLevels;
				samplerCreateInfo.maxAnisotropy = VulkanContext::device->properties.limits.maxSamplerAnisotropy;
				samplerCreateInfo.anisotropyEnable = VulkanContext::device->enabledFeatures.samplerAnisotropy;
				VK_CHECK_RESULT(vkCreateSampler(VulkanContext::device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));
			}

			// Update descriptor image info member that can be used for setting up descriptor sets
			updateDescriptor();
		}
	};

	class TextureCubeMap : public Texture {
	public:
		TextureCubeMap(TextureCreateInfo createInfo)
		{
			ktxTexture* ktxTexture;
			ktxResult result = loadKTXFile(createInfo.filename, &ktxTexture);
			assert(result == KTX_SUCCESS);

			width = ktxTexture->baseWidth;
			height = ktxTexture->baseHeight;
			mipLevels = ktxTexture->numLevels;

			ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
			ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

			VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
			bufferCreateInfo.size = ktxTextureSize;
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(VulkanContext::device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(VulkanContext::device->logicalDevice, stagingBuffer, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = VulkanContext::device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(VulkanContext::device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(VulkanContext::device->logicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(VulkanContext::device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, ktxTextureData, ktxTextureSize);
			vkUnmapMemory(VulkanContext::device->logicalDevice, stagingMemory);

			// Setup buffer copy regions for each face including all of it's miplevels
			std::vector<VkBufferImageCopy> bufferCopyRegions;

			for (uint32_t face = 0; face < 6; face++)
			{
				for (uint32_t level = 0; level < mipLevels; level++)
				{
					ktx_size_t offset;
					KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
					assert(result == KTX_SUCCESS);

					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = level;
					bufferCopyRegion.imageSubresource.baseArrayLayer = face;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
					bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);
				}
			}

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = createInfo.format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = createInfo.imageUsageFlags;
			// Ensure that the TRANSFER_DST bit is set for staging
			if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


			VK_CHECK_RESULT(vkCreateImage(VulkanContext::device->logicalDevice, &imageCreateInfo, nullptr, &image));

			vkGetImageMemoryRequirements(VulkanContext::device->logicalDevice, image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = VulkanContext::device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(VulkanContext::device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(VulkanContext::device->logicalDevice, image, deviceMemory, 0));

			// Use a separate command buffer for texture loading
			VkCommandBuffer copyCmd = VulkanContext::device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			vks::tools::setImageLayout(
				copyCmd,
				image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data());

			// Change texture image layout to shader read after all faces have been copied
			this->imageLayout = imageLayout;
			vks::tools::setImageLayout(
				copyCmd,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				imageLayout,
				subresourceRange);

			// @todo: Transfer queue
			VulkanContext::device->flushCommandBuffer(copyCmd, VulkanContext::graphicsQueue);

			// Create image view
			VkImageViewCreateInfo viewCreateInfo = vks::initializers::imageViewCreateInfo();
			viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			viewCreateInfo.format = createInfo.format;
			viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			viewCreateInfo.subresourceRange.layerCount = 6;
			viewCreateInfo.subresourceRange.levelCount = mipLevels;
			viewCreateInfo.image = image;
			VK_CHECK_RESULT(vkCreateImageView(VulkanContext::device->logicalDevice, &viewCreateInfo, nullptr, &view));

			if (createInfo.createSampler) {
				VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
				samplerCreateInfo.magFilter = createInfo.magFilter;
				samplerCreateInfo.minFilter = createInfo.minFilter;
				samplerCreateInfo.mipmapMode = createInfo.mipmapMode;
				samplerCreateInfo.addressModeU = createInfo.addressModeU;
				samplerCreateInfo.addressModeV = createInfo.addressModeV;
				samplerCreateInfo.addressModeW = createInfo.addressModeV;
				samplerCreateInfo.mipLodBias = 0.0f;
				samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
				samplerCreateInfo.minLod = 0.0f;
				samplerCreateInfo.maxLod = (float)mipLevels;
				samplerCreateInfo.maxAnisotropy = VulkanContext::device->properties.limits.maxSamplerAnisotropy;
				samplerCreateInfo.anisotropyEnable = VulkanContext::device->enabledFeatures.samplerAnisotropy;
				VK_CHECK_RESULT(vkCreateSampler(VulkanContext::device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));
			}

			// Clean up staging resources
			ktxTexture_Destroy(ktxTexture);
			vkFreeMemory(VulkanContext::device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(VulkanContext::device->logicalDevice, stagingBuffer, nullptr);

			// Update descriptor image info member that can be used for setting up descriptor sets
			updateDescriptor();
		}
	};

}
