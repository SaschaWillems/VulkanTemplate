/*
 * Vulkan pipeline layout abstraction class
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

struct PipelineLayoutCreateInfo {
	vks::VulkanDevice& device;
	// @todo: Use DescriptorSetLayout
	std::vector<VkDescriptorSetLayout> layouts;
	std::vector<VkPushConstantRange> pushConstantRanges;
};

class PipelineLayout {
private:
	vks::VulkanDevice& device;
	std::vector<VkDescriptorSetLayout> layouts;
	std::vector<VkPushConstantRange> pushConstantRanges;
public:
	VkPipelineLayout handle = VK_NULL_HANDLE;

	PipelineLayout(PipelineLayoutCreateInfo createInfo) : device(createInfo.device) {
		layouts = createInfo.layouts;
		pushConstantRanges = createInfo.pushConstantRanges;
		VkPipelineLayoutCreateInfo CI = vks::initializers::pipelineLayoutCreateInfo(layouts.data(), static_cast<uint32_t>(layouts.size()));
		CI.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
		CI.pPushConstantRanges = pushConstantRanges.data();
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &CI, nullptr, &handle));
	}

	~PipelineLayout() {
		vkDestroyPipelineLayout(device, handle, nullptr);
	}

	void addPushConstantRange(uint32_t size, uint32_t offset, VkShaderStageFlags stageFlags) {
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = stageFlags;
		pushConstantRange.offset = offset;
		pushConstantRange.size = size;
		pushConstantRanges.push_back(pushConstantRange);
	}

	VkPushConstantRange getPushConstantRange(uint32_t index) {
		assert(index < pushConstantRanges.size());
		return pushConstantRanges[index];
	}

	operator VkPipelineLayout() const {
		return handle;
	}
};