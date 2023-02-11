/*
 * Vulkan device abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <exception>
#include <assert.h>
#include <algorithm>
#include "volk.h"
#include "VulkanTools.h"
//#include "Buffer.hpp"

enum class QueueType { Graphics, Compute, Transfer };

struct DeviceCreateInfo {
	VkPhysicalDevice physicalDevice;
	std::vector<const char*> enabledExtensions;
	// @odo: queue types (see other units) or make it separate arguments (dedicatedTransfer, etc.)
	VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	void* pNextChain;
	bool useSwapChain = true;
};

struct Device
{
	/** @brief Physical device representation */
	VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
	/** @brief Logical device representation (application's view of the device) */
	VkDevice logicalDevice{ VK_NULL_HANDLE };
	/** @brief Properties of the physical device including limits that the application can check against */
	VkPhysicalDeviceProperties properties{};
	/** @brief Features of the physical device that an application can use to check if a feature is supported */
	VkPhysicalDeviceFeatures features{};
	/** @brief Features that have been enabled for use on the physical device */
	inline static VkPhysicalDeviceFeatures enabledFeatures{};
	inline static VkPhysicalDeviceVulkan11Features enabledFeatures11{};
	inline static VkPhysicalDeviceVulkan12Features enabledFeatures12{};
	inline static VkPhysicalDeviceVulkan13Features enabledFeatures13{};
	/** @brief Memory types and heaps of the physical device */
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	/** @brief Queue family properties of the physical device */
	std::vector<VkQueueFamilyProperties> queueFamilyProperties{};
	/** @brief List of extensions supported by the device */
	std::vector<std::string> supportedExtensions{};

	/** @brief Default command pool for the graphics queue family index */
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	VkCommandPool commandPoolTransfer{ VK_NULL_HANDLE };

	/** @brief Contains queue family indices */
	struct {
		uint32_t graphics;
		uint32_t compute;
		uint32_t transfer;
	} queueFamilyIndices;

	bool hasDedicatedTransferQueue{ false };
	bool hasDedicatedComputeQueue{ false };
	bool hasDebugUtils{ false };

	/**  @brief Typecast to VkDevice */
	operator VkDevice() { return logicalDevice; };

	Device(DeviceCreateInfo createInfo) : physicalDevice(createInfo.physicalDevice) {
		assert(physicalDevice);

		// Store Properties features, limits and properties of the physical device for later use
		vkGetPhysicalDeviceProperties(physicalDevice, &properties);
		vkGetPhysicalDeviceFeatures(physicalDevice, &features);
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
		// Queue family properties, used for setting up requested queues upon device creation
		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		assert(queueFamilyCount > 0);
		queueFamilyProperties.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
		// Get list of supported extensions
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
		if (extCount > 0) {
			std::vector<VkExtensionProperties> extensions(extCount);
			if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
				for (auto ext : extensions) {
					supportedExtensions.push_back(ext.extensionName);
				}
			}
		}

		// Create the logical device
		// Desired queues need to be requested upon logical device creation
		// Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
		// requests different queue types
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
		// Get queue family indices for the requested queue family types
		const float defaultQueuePriority(0.0f);
		// Graphics queue
		if (createInfo.requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT) {
			queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
			VkDeviceQueueCreateInfo queueInfo{};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &defaultQueuePriority;
			queueCreateInfos.push_back(queueInfo);
		} else {
			queueFamilyIndices.graphics = 0;
		}

		// Dedicated compute queue
		if (createInfo.requestedQueueTypes & VK_QUEUE_COMPUTE_BIT) {
			queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
			if (queueFamilyIndices.compute != queueFamilyIndices.graphics) {
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
		} else {
			// Else we use the same queue
			queueFamilyIndices.compute = queueFamilyIndices.graphics;
		}

		// Dedicated transfer queue
		if (createInfo.requestedQueueTypes & VK_QUEUE_TRANSFER_BIT) {
			queueFamilyIndices.transfer = getQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
			if ((queueFamilyIndices.transfer != queueFamilyIndices.graphics) && (queueFamilyIndices.transfer != queueFamilyIndices.compute))
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.transfer;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
		} else {
			// Else we use the same queue
			queueFamilyIndices.transfer = queueFamilyIndices.graphics;
		}

		// Create the logical device representation
		std::vector<const char*> deviceExtensions(createInfo.enabledExtensions);
		if (createInfo.useSwapChain) {
			// If the device will be used for presenting to a display via a swapchain we need to request the swapchain extension
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}

		VkDeviceCreateInfo deviceCreateInfo = {};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.pEnabledFeatures = &Device::enabledFeatures;

		Device::enabledFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		Device::enabledFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		Device::enabledFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		Device::enabledFeatures11.pNext = &Device::enabledFeatures12;
		Device::enabledFeatures12.pNext = &Device::enabledFeatures13;
		deviceCreateInfo.pNext = &Device::enabledFeatures11;

		// Enable debug utils extension if available
		if (extensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
			deviceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			hasDebugUtils = true;
		}

		if (deviceExtensions.size() > 0) {
			deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
			deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
		}

		VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);
		if (result != VK_SUCCESS) {
			throw "Could not create Vulkan device : \n" + vks::tools::errorString(result);
		}

		// Create a default command pool for graphics command buffers
		// @todo: remove, shouldn't be part of the device
		commandPool = createCommandPool(queueFamilyIndices.graphics);
		if (queueFamilyIndices.graphics != queueFamilyIndices.transfer) {
			commandPoolTransfer = createCommandPool(queueFamilyIndices.transfer);
		} else {
			commandPoolTransfer = commandPool;
		}
	}

	~Device()
	{
		if (commandPool)
		{
			vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
		}
		if (logicalDevice)
		{
			vkDestroyDevice(logicalDevice, nullptr);
		}
	}

	/**
	* Get the index of a memory type that has all the requested property bits set
	*
	* @param typeBits Bitmask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
	* @param properties Bitmask of properties for the memory type to request
	* @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
	*
	* @return Index of the requested memory type
	*
	* @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
	*/
	uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound = nullptr)
	{
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		{
			if ((typeBits & 1) == 1)
			{
				if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					if (memTypeFound)
					{
						*memTypeFound = true;
					}
					return i;
				}
			}
			typeBits >>= 1;
		}

		if (memTypeFound)
		{
			*memTypeFound = false;
			return 0;
		}
		else
		{
			throw std::runtime_error("Could not find a matching memory type");
		}
	}

	/**
	* Get the index of a queue family that supports the requested queue flags
	*
	* @param queueFlags Queue flags to find a queue family index for
	*
	* @return Index of the queue family index that matches the flags
	*
	* @throw Throws an exception if no queue family index could be found that supports the requested flags
	*/
	uint32_t getQueueFamilyIndex(VkQueueFlagBits queueFlags)
	{
		// Dedicated queue for compute
		// Try to find a queue family index that supports compute but not graphics
		if (queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
				{
					hasDedicatedComputeQueue = true;
					return i;
				}
			}
		}

		// Dedicated queue for transfer
		// Try to find a queue family index that supports transfer but not graphics and compute
		if (queueFlags & VK_QUEUE_TRANSFER_BIT)
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
				{
					hasDedicatedTransferQueue = true;
					return i;
				}
			}
		}

		// For other queue types or if no separate compute queue is present, return the first one to support the requested flags
		for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
		{
			if (queueFamilyProperties[i].queueFlags & queueFlags)
			{
				return i;
			}
		}

		throw std::runtime_error("Could not find a matching queue family index");
	}

	// @todo
	//void copyBuffer(Buffer* src, Buffer* dst, VkQueue queue, VkBufferCopy* copyRegion = nullptr)
	//{
	//	//assert(dst->size <= src->size);
	//	assert(src->buffer);
	//	VkCommandBuffer copyCmd = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	//	VkBufferCopy bufferCopy{};
	//	if (copyRegion == nullptr)
	//	{
	//		bufferCopy.size = src->size;
	//	}
	//	else
	//	{
	//		bufferCopy = *copyRegion;
	//	}

	//	vkCmdCopyBuffer(copyCmd, src->buffer, dst->buffer, 1, &bufferCopy);

	//	flushCommandBuffer(copyCmd, queue);
	//}

	/**
	* Create a command pool for allocation command buffers from
	*
	* @param queueFamilyIndex Family index of the queue to create the command pool for
	* @param createFlags (Optional) Command pool creation flags (Defaults to VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
	*
	* @note Command buffers allocated from the created pool can only be submitted to a queue with the same family index
	*
	* @return A handle to the created command buffer
	*/
	VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
	{
		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
		cmdPoolInfo.flags = createFlags;
		VkCommandPool cmdPool;
		VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
		return cmdPool;
	}

	/**
	* Allocate a command buffer from the command pool
	*
	* @param level Level of the new command buffer (primary or secondary)
	* @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
	*
	* @return A handle to the allocated command buffer
	*/
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin = false, VkQueueFlagBits queueType = VK_QUEUE_GRAPHICS_BIT)
	{
		VkCommandPool cmdPool = commandPool;
		if (queueType == VK_QUEUE_TRANSFER_BIT) {
			cmdPool = commandPoolTransfer;
		}

		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(cmdPool, level, 1);

		VkCommandBuffer cmdBuffer;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

		// If requested, also start recording for the new command buffer
		if (begin)
		{
			VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		}

		return cmdBuffer;
	}

	/**
	* Finish command buffer recording and submit it to a queue
	*
	* @param commandBuffer Command buffer to flush
	* @param queue Queue to submit the command buffer to
	* @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
	*
	* @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
	* @note Uses a fence to ensure command buffer has finished executing
	*/
	void flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true, VkQueueFlagBits queueType = VK_QUEUE_GRAPHICS_BIT)
	{
		if (commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		// Create fence to ensure that the command buffer has finished executing
		VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
		VkFence fence;
		VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));

		// Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		// Wait for the fence to signal that command buffer has finished executing
		VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

		vkDestroyFence(logicalDevice, fence, nullptr);

		if (free)
		{
			VkCommandPool cmdPool = commandPool;
			if (queueType == VK_QUEUE_TRANSFER_BIT) {
				cmdPool = commandPoolTransfer;
			}
			vkFreeCommandBuffers(logicalDevice, cmdPool, 1, &commandBuffer);
		}
	}

	/**
	* Check if an extension is supported by the (physical device)
	*
	* @param extension Name of the extension to check
	*
	* @return True if the extension is supported (present in the list read at device creation time)
	*/
	bool extensionSupported(std::string extension)
	{
		return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
	}

	VkQueue getQueue(QueueType queueType) {
		uint32_t queueFamilyIndex = 0;
		bool validQueueFamilyIndex = false;
		switch (queueType) {
		case QueueType::Graphics:
			queueFamilyIndex = queueFamilyIndices.graphics;
			validQueueFamilyIndex = true;
			break;
		case QueueType::Compute:
			queueFamilyIndex = queueFamilyIndices.compute;
			validQueueFamilyIndex = true;
			break;
		case QueueType::Transfer:
			queueFamilyIndex = queueFamilyIndices.transfer;
			validQueueFamilyIndex = true;
			break;
		}
		// @todo: queue class
		VkQueue queue = VK_NULL_HANDLE;
		assert(validQueueFamilyIndex);
		vkGetDeviceQueue(logicalDevice, queueFamilyIndex, 0, &queue);
		return queue;
	}

	VkFormat getSupportedDepthFormat() {
		// Since all depth formats may be optional, we need to find a suitable depth format to use
		// Start with the highest precision packed format
		std::vector<VkFormat> depthFormats = {
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM
		};

		for (auto& format : depthFormats) {
			VkFormatProperties formatProps;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
			// Format must support depth stencil attachment for optimal tiling
			if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				return format;
			}
		}

		return VK_FORMAT_UNDEFINED;
	}

	void waitIdle() {
		vkDeviceWaitIdle(logicalDevice);
	}

};
