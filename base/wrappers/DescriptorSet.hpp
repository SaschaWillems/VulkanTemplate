/*
 * Vulkan descriptor set abstraction class
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
#include "DescriptorSetLayout.hpp"
#include "DescriptorPool.hpp"

struct DescriptorSetCreateInfo {
	vks::VulkanDevice& device;
	DescriptorPool* pool = nullptr;
	std::vector<VkDescriptorSetLayout> layouts;
	std::vector<VkWriteDescriptorSet> descriptors;
};

class DescriptorSet {
private:
	VkDevice device = VK_NULL_HANDLE;
	std::vector<VkWriteDescriptorSet> descriptors;
public:
	VkDescriptorSet handle;

	DescriptorSet(DescriptorSetCreateInfo createInfo) : device(createInfo.device) {
		descriptors = createInfo.descriptors;
		VkDescriptorSetAllocateInfo descriptorSetAI = vks::initializers::descriptorSetAllocateInfo(createInfo.pool->handle, createInfo.layouts.data(), static_cast<uint32_t>(createInfo.layouts.size()));
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAI, &handle));
		for (auto& descriptor : createInfo.descriptors) {
			descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor.dstSet = handle;
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(createInfo.descriptors.size()), createInfo.descriptors.data(), 0, nullptr);
	}

	~DescriptorSet() {
		// @todo: nothing to do here
	}

	operator VkDescriptorSet() const { 
		return handle; 
	}

	void addDescriptor(VkWriteDescriptorSet descriptor) {
		descriptors.push_back(descriptor);
	}

	void addDescriptor(uint32_t binding, VkDescriptorType type, VkDescriptorBufferInfo* bufferInfo, uint32_t descriptorCount = 1) {
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = type;
		writeDescriptorSet.dstBinding = binding;
		writeDescriptorSet.pBufferInfo = bufferInfo;
		writeDescriptorSet.descriptorCount = descriptorCount;
		descriptors.push_back(writeDescriptorSet);
	}

	void addDescriptor(uint32_t binding, VkDescriptorType type, VkDescriptorImageInfo* imageInfo, uint32_t descriptorCount = 1) {
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = type;
		writeDescriptorSet.dstBinding = binding;
		writeDescriptorSet.pImageInfo = imageInfo;
		writeDescriptorSet.descriptorCount = descriptorCount;
		descriptors.push_back(writeDescriptorSet);
	}

	void updateDescriptor(uint32_t binding, VkDescriptorType type, VkDescriptorImageInfo* imageInfo, uint32_t descriptorCount = 1) {
		VkWriteDescriptorSet writeDescriptorSet{};
		for (auto &descriptor : descriptors) {
			if (descriptor.dstBinding == binding) {
				descriptor.descriptorType = type;
				descriptor.pImageInfo = imageInfo;
				descriptor.descriptorCount = descriptorCount;
				vkUpdateDescriptorSets(device, 1, &descriptor, 0, nullptr);
				break;
			}
		}
	}
};
