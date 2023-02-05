/*
 * Vulkan buffer
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>

#include "volk.h"
#include "DeviceResource.h"
#include "Device.hpp"

struct BufferCreateInfo {
	const std::string name{ "" };
	vks::VulkanDevice& device;
	// @todo: replace with own enumes and use cases (e.g. like VMA)
	VkBufferUsageFlags usageFlags;
	VkMemoryPropertyFlags memoryPropertyFlags;
	VkDeviceSize size;
	bool map{ true };
	void* data{ nullptr };
};

// @todo: rework to class based on resource
class Buffer : public DeviceResource
{
private:
public:
	// @todo: move to private after rework
	VkDeviceMemory memory{ VK_NULL_HANDLE };
	VkBuffer buffer{ VK_NULL_HANDLE }; // @todo: rename to handle
	VkDescriptorBufferInfo descriptor;
	VkDeviceSize size = 0;
	VkDeviceSize alignment = 0;
	void* mapped = nullptr;

	Buffer(BufferCreateInfo createInfo) : DeviceResource(createInfo.device, createInfo.name) {
		size = createInfo.size;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(createInfo.usageFlags, createInfo.size);
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(createInfo.device, &bufferCreateInfo, nullptr, &buffer));

		// Create the memory backing up the buffer handle
		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		vkGetBufferMemoryRequirements(createInfo.device, buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		// Find a memory type index that fits the properties of the buffer
		memAlloc.memoryTypeIndex = device.getMemoryType(memReqs.memoryTypeBits, createInfo.memoryPropertyFlags);
		VK_CHECK_RESULT(vkAllocateMemory(createInfo.device, &memAlloc, nullptr, &memory));
		VK_CHECK_RESULT(vkBindBufferMemory(createInfo.device, buffer, memory, 0));

		// If a pointer to the buffer data has been passed, map the buffer and copy over the data from host memory
		if (createInfo.data != nullptr) {
			void* mapped;
			VK_CHECK_RESULT(vkMapMemory(createInfo.device, memory, 0, size, 0, &mapped));
			memcpy(mapped, createInfo.data, createInfo.size);
			// If host coherency hasn't been requested, do a manual flush to make writes visible
			if ((createInfo.memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
				VkMappedMemoryRange mappedRange{
					.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
					.memory = memory,
					.offset = 0,
					.size = createInfo.size
				};
				vkFlushMappedMemoryRanges(createInfo.device, 1, &mappedRange);
			}
			vkUnmapMemory(createInfo.device, memory);
		}
		if (createInfo.map) {
			vkMapMemory(device, memory, 0, createInfo.size, 0, &mapped);
		}
		descriptor = {
			.buffer = buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE
		};
		setDebugName((uint64_t)buffer, VK_OBJECT_TYPE_BUFFER);
		setDebugName((uint64_t)memory, VK_OBJECT_TYPE_DEVICE_MEMORY);
	}

	~Buffer() {
		if (buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, buffer, nullptr);
		}
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, memory, nullptr);
		}
	}

	operator VkBuffer() const {
		return buffer;
	}

	VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		return vkMapMemory(device, memory, offset, size, 0, &mapped);
	}

	void unmap()
	{
		if (mapped)
		{
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
	}

	VkResult bind(VkDeviceSize offset = 0)
	{
		return vkBindBufferMemory(device, buffer, memory, offset);
	}

	void copyTo(void* data, VkDeviceSize size)
	{
		assert(mapped);
		memcpy(mapped, data, size);
	}

	VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = memory;
		mappedRange.offset = offset;
		mappedRange.size = size;
		return vkFlushMappedMemoryRanges(device, 1, &mappedRange);
	}

	VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
	{
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = memory;
		mappedRange.offset = offset;
		mappedRange.size = size;
		return vkInvalidateMappedMemoryRanges(device, 1, &mappedRange);
	}

	void destroy()
	{
		if (buffer)
		{
			vkDestroyBuffer(device, buffer, nullptr);
		}
		if (memory)
		{
			vkFreeMemory(device, memory, nullptr);
		}
	}

};
