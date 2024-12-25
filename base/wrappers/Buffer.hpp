/*
 * Vulkan buffer
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>

#include "volk.h"
#include "DeviceResource.h"
#include "Device.hpp"
#include "VulkanContext.h"

struct BufferCreateInfo {
	const std::string name{ "" };
	VkBufferUsageFlags usageFlags;
	VkDeviceSize size;
	bool map{ false };
	void* data{ nullptr };
};

// @todo: rework to class based on resource
class Buffer : public DeviceResource
{
public:
	VmaAllocation bufferAllocation;
	VkBuffer buffer{ VK_NULL_HANDLE }; // @todo: rename to handle
	VkDescriptorBufferInfo descriptor;
	VkDeviceSize size = 0;
	VkDeviceSize alignment = 0;
	void* mapped = nullptr;

	Buffer(BufferCreateInfo createInfo) : DeviceResource(createInfo.name) {
		size = createInfo.size;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(createInfo.usageFlags, createInfo.size);
		VmaAllocationCreateInfo bufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
		if ((createInfo.data != nullptr) || (createInfo.map)) {
			bufferAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		else {
			bufferAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
			bufferAllocInfo.priority = 1.0f;
		}
		VK_CHECK_RESULT(vmaCreateBuffer(VulkanContext::vmaAllocator, &bufferCreateInfo, &bufferAllocInfo, &buffer, &bufferAllocation, nullptr));

		// If a pointer to the buffer data has been passed, map the buffer and copy over the data from host memory
		if (createInfo.data != nullptr) {
			void* mapped;
			VK_CHECK_RESULT(vmaMapMemory(VulkanContext::vmaAllocator, bufferAllocation, &mapped))
			memcpy(mapped, createInfo.data, createInfo.size);
			vmaFlushAllocation(VulkanContext::vmaAllocator, bufferAllocation, 0, createInfo.size);
			vmaUnmapMemory(VulkanContext::vmaAllocator, bufferAllocation);
		}
		if (createInfo.map) {
			VK_CHECK_RESULT(vmaMapMemory(VulkanContext::vmaAllocator, bufferAllocation, &mapped))
		}
		descriptor = {
			.buffer = buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE
		};
		setDebugName((uint64_t)buffer, VK_OBJECT_TYPE_BUFFER);
	}

	~Buffer() {
		if (mapped) {
			vmaUnmapMemory(VulkanContext::vmaAllocator, bufferAllocation);
		}
		vmaDestroyBuffer(VulkanContext::vmaAllocator, buffer, bufferAllocation);
	}

	operator VkBuffer() const {
		return buffer;
	}

	VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		return vmaMapMemory(VulkanContext::vmaAllocator, bufferAllocation, &mapped);
	}

	void unmap()
	{
		if (mapped)
		{
			vmaUnmapMemory(VulkanContext::vmaAllocator, bufferAllocation);
			mapped = nullptr;
		}
	}

	void copyTo(void* data, VkDeviceSize size)
	{
		assert(mapped);
		memcpy(mapped, data, size);
	}

	VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		return vmaFlushAllocation(VulkanContext::vmaAllocator, bufferAllocation, offset, size);
	}

	VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		return vmaInvalidateAllocation(VulkanContext::vmaAllocator, bufferAllocation, offset, size);
	}
};
