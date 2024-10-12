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

#include "vk_mem_alloc.h"

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

	struct TextureFromBufferCreateInfo {
		void* buffer;
		VkDeviceSize bufferSize;
		uint32_t texWidth;
		uint32_t texHeight;
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
		VmaAllocation imgAllocation;
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
			if (sampler)
			{
				vkDestroySampler(VulkanContext::device->logicalDevice, sampler, nullptr);
			}
			vmaDestroyImage(VulkanContext::vmaAllocator, image, imgAllocation);
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
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			VmaAllocationCreateInfo bufferAllocInfo{
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO
			};
			VmaAllocation bufferAllocation;
			VK_CHECK_RESULT(vmaCreateBuffer(VulkanContext::vmaAllocator, &bufferCreateInfo, &bufferAllocInfo, &stagingBuffer, &bufferAllocation, nullptr));

			uint8_t *data;
			VK_CHECK_RESULT(vmaMapMemory(VulkanContext::vmaAllocator, bufferAllocation, (void**)&data));
			memcpy(data, ktxTextureData, ktxTextureSize);

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
			VmaAllocationCreateInfo imgAllocInfo{ 
				.usage = VMA_MEMORY_USAGE_AUTO
			};
			VK_CHECK_RESULT(vmaCreateImage(VulkanContext::vmaAllocator, &imageCreateInfo, &imgAllocInfo, &image, &imgAllocation, nullptr));

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
			this->imageLayout = createInfo.imageLayout;
			vks::tools::setImageLayout(
				copyCmd,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				imageLayout,
				subresourceRange);

			// @todo: transfer queue
			VulkanContext::device->flushCommandBuffer(copyCmd, VulkanContext::graphicsQueue);

			// Clean up staging resources
			vmaUnmapMemory(VulkanContext::vmaAllocator, bufferAllocation);
			vmaDestroyBuffer(VulkanContext::vmaAllocator, stagingBuffer, bufferAllocation);
			
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

		Texture2D(TextureFromBufferCreateInfo createInfo)
		{
			assert(createInfo.buffer);

			width = createInfo.texWidth;
			height = createInfo.texHeight;
			mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

			VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Use a separate command buffer for texture loading
			VkCommandBuffer copyCmd = VulkanContext::device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
			bufferCreateInfo.size = createInfo.bufferSize;
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
			uint8_t* data;
			VK_CHECK_RESULT(vkMapMemory(VulkanContext::device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, createInfo.buffer, createInfo.bufferSize);
			vkUnmapMemory(VulkanContext::device->logicalDevice, stagingMemory);

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = 0;

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
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
				1,
				&bufferCopyRegion
			);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			// @todo
			VkQueue copyQueue = VulkanContext::graphicsQueue;

			VulkanContext::device->flushCommandBuffer(copyCmd, copyQueue, true);

			vkFreeMemory(VulkanContext::device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(VulkanContext::device->logicalDevice, stagingBuffer, nullptr);

			// Generate the mip chain
			VkCommandBuffer blitCmd = VulkanContext::device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			for (uint32_t i = 1; i < mipLevels; i++) {
				VkImageBlit imageBlit{};

				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstOffsets[1].x = int32_t(width >> i);
				imageBlit.dstOffsets[1].y = int32_t(height >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = 0;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.image = image;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			subresourceRange.levelCount = mipLevels;
			imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			VulkanContext::device->flushCommandBuffer(blitCmd, copyQueue, true);

			// Change texture image layout to shader read after all mip levels have been copied
			//this->imageLayout = createInfo.imageLayout;
			//vks::tools::setImageLayout(
			//	copyCmd,
			//	image,
			//	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			//	imageLayout,
			//	subresourceRange);

			// @todo: create full mip chain

			// @todo: transfer queu
			//VulkanContext::device->flushCommandBuffer(copyCmd, VulkanContext::graphicsQueue);

			// Create image view
			VkImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCreateInfo.pNext = NULL;
			viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCreateInfo.format = createInfo.format;
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
		TextureCubeMap() { };
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
			this->imageLayout = createInfo.imageLayout;
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
