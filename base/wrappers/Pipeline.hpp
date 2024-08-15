/*
 * Vulkan pipeline abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include "volk.h"
#include <stdexcept>
#if defined(__ANDROID__)
#include "Android.h"
#endif
#include "DeviceResource.h"
#include "Device.hpp"
#include "Initializers.hpp"
#include "VulkanTools.h"
#include "PipelineLayout.hpp"
#include "dxc.hpp"

enum class DynamicState { Viewport, Scissor };

struct PipelineVertexInput {
	std::vector<VkVertexInputBindingDescription> bindings{};
	std::vector<VkVertexInputAttributeDescription> attributes{};
};

// @todo: add name (to all kind of wrapped objects)
struct PipelineCreateInfo {
	const std::string name{ "" };
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };
	std::vector<std::string> shaders{};
	VkPipelineCache cache{ VK_NULL_HANDLE };
	VkPipelineLayout layout;
	VkPipelineCreateFlags flags;
	PipelineVertexInput vertexInput{};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	VkPipelineTessellationStateCreateInfo tessellationState{};
	VkPipelineViewportStateCreateInfo viewportState{};
	VkPipelineRasterizationStateCreateInfo rasterizationState{};
	VkPipelineMultisampleStateCreateInfo multisampleState{};
	VkPipelineDepthStencilStateCreateInfo depthStencilState{};
	struct {
		std::vector<VkPipelineColorBlendAttachmentState> attachments{};
	} blending;
	std::vector<DynamicState> dynamicState{};
	VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
	bool enableHotReload{ false };
};

class Pipeline : public DeviceResource {
private:
	VkPipeline handle{ VK_NULL_HANDLE };
	std::vector<VkShaderModule> shaderModules{};
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};

	void addShader(const std::string filename) {
		// @todo: also support GLSL? Or jut drop it? And what about Android?
		try {
			assert(dxcCompiler);
			VkShaderModule shaderModule = dxcCompiler->compileShader(filename);
			VkShaderStageFlagBits shaderStage = dxcCompiler->getShaderStage(filename);
			shaderModules.push_back(shaderModule);
			VkPipelineShaderStageCreateInfo shaderStageCI{};
			shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStageCI.stage = shaderStage;
			shaderStageCI.module = shaderModule;
			shaderStageCI.pName = "main";
			shaderStages.push_back(shaderStageCI);
		} catch (...) {
			throw;
		}
	}
	
	void createPipelineObject(PipelineCreateInfo createInfo) {
		shaderStages.clear();
		shaderModules.clear();
		try {
			for (auto& filename : createInfo.shaders) {
				addShader(filename);
			}
		}
		catch (...) {
			throw;
		}

		createInfo.inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		createInfo.viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		createInfo.rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		createInfo.multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		createInfo.depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		createInfo.pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;

		std::vector<VkDynamicState> dstates{};
		for (auto& s : createInfo.dynamicState) {
			switch (s) {
			case DynamicState::Scissor:
				dstates.push_back(VK_DYNAMIC_STATE_SCISSOR);
				break;
			case DynamicState::Viewport:
				dstates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
				break;
			}
		}
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = (uint32_t)dstates.size();
		dynamicState.pDynamicStates = dstates.data();

		VkPipelineVertexInputStateCreateInfo vertexInputState{};
		vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(createInfo.vertexInput.bindings.size());
		vertexInputState.pVertexBindingDescriptions = createInfo.vertexInput.bindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(createInfo.vertexInput.attributes.size());
		vertexInputState.pVertexAttributeDescriptions = createInfo.vertexInput.attributes.data();

		VkPipelineColorBlendStateCreateInfo colorBlendState{};
		colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendState.attachmentCount = static_cast<uint32_t>(createInfo.blending.attachments.size());
		colorBlendState.pAttachments = createInfo.blending.attachments.data();

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.layout = createInfo.layout;
		pipelineCI.pVertexInputState = &vertexInputState;
		pipelineCI.pInputAssemblyState = &createInfo.inputAssemblyState;
		pipelineCI.pTessellationState = &createInfo.tessellationState;
		pipelineCI.pViewportState = &createInfo.viewportState;
		pipelineCI.pRasterizationState = &createInfo.rasterizationState;
		pipelineCI.pMultisampleState = &createInfo.multisampleState;
		pipelineCI.pDepthStencilState = &createInfo.depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.pNext = &createInfo.pipelineRenderingInfo; // createInfo.pNext;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(VulkanContext::device->logicalDevice, createInfo.cache, 1, &pipelineCI, nullptr, &handle));
	
		// Shader modules can be safely destroyed after pipeline creation
		for (auto& shaderModule : shaderModules) {
			vkDestroyShaderModule(VulkanContext::device->logicalDevice, shaderModule, nullptr);
		}

		bindPoint = createInfo.bindPoint;
	}

public:
	// Store the createInfo for hot reload
	PipelineCreateInfo* initialCreateInfo{ nullptr };
	VkPipelineBindPoint bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS };
	bool wantsReload = false;

	Pipeline(PipelineCreateInfo createInfo) : DeviceResource(createInfo.name) {
		createPipelineObject(createInfo);

		// Store a copy of the createInfo for hot reload		
		if (createInfo.enableHotReload) {
			initialCreateInfo = new PipelineCreateInfo(createInfo);
		}

		setDebugName((uint64_t)handle, VK_OBJECT_TYPE_PIPELINE);
	};

	~Pipeline() {
		vkDestroyPipeline(VulkanContext::device->logicalDevice, handle, nullptr);
	}

	void reload() {
		wantsReload = false;
		assert(initialCreateInfo);
		// @todo: move to calling function to avoid multiple wait idles
		VulkanContext::device->waitIdle();
		// For hot reloads create a temp handle, so if pipeline creation fails the application will continue with the old pipeline
		VkPipeline oldHandle = handle;
		try {
			createPipelineObject(*initialCreateInfo);
			vkDestroyPipeline(VulkanContext::device->logicalDevice, oldHandle, nullptr);
			std::cout << "Pipeline recreated\n";
		} catch (...) {
			std::cerr << "Could not recreate pipeline, using last version\n";
		}
	}

	operator VkPipeline() { return handle; };
};
