/*
 * UI overlay class using ImGui
 *
 * Copyright (C) 2017-2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <glm/glm.hpp>
#include "volk.h"
#include "DeviceResource.h"
#include "VulkanTools.h"
#include "Device.hpp"
#include "Buffer.hpp"
#include "Device.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"
#include "DescriptorSet.hpp"
#include "Pipeline.hpp"
#include "PipelineLayout.hpp"
#include "Sampler.hpp"
#include "Image.hpp"
#include "ImageView.hpp"

#include "../external/imgui/imgui.h"

#if defined(__ANDROID__)
#include "VulkanAndroid.h"
#endif

namespace vks
{
	struct OverlayCreateInfo
	{
		Device& device;
		VkQueue queue;
		VkPipelineCache pipelineCache;
		VkFormat colorFormat;
		VkFormat depthFormat;
		VkSampleCountFlagBits rasterizationSamples{ VK_SAMPLE_COUNT_1_BIT };
		std::string fontFileName{ "" };
		std::string assetPath{ "" };
		float scale{ 1.0f };
		uint32_t frameCount{ 0 };
	};

	class UIOverlay : public DeviceResource
	{
	private:
		VkQueue queue;
		VkSampleCountFlagBits rasterizationSamples{ VK_SAMPLE_COUNT_1_BIT };
		std::string fontFileName{ "" };
		std::string assetPath{ "" };
		DescriptorPool* descriptorPool;
		DescriptorSetLayout* descriptorSetLayout;
		DescriptorSet* descriptorSet;
		PipelineLayout* pipelineLayout{ nullptr };
		Pipeline* pipeline{ nullptr };
		Image* fontImage{ nullptr };
		ImageView* fontView{ nullptr };
		VkDeviceMemory fontMemory{ VK_NULL_HANDLE };
		Sampler* sampler{ nullptr };
		void prepareResources();
		void preparePipeline(const VkPipelineCache pipelineCache, VkFormat colorFormat, VkFormat depthFormat);
	public:
		struct FrameObjects {
			Buffer* vertexBuffer;
			Buffer* indexBuffer;
			int32_t vertexCount{ 0 };
			int32_t indexCount{ 0 };
		};
		std::vector<FrameObjects> frameObjects;

		struct PushConstBlock {
			glm::vec2 scale;
			glm::vec2 translate;
		} pushConstBlock;

		bool visible{ true };
		bool updated{ false };
		float scale{ 1.0f };

		UIOverlay(OverlayCreateInfo createInfo);
		~UIOverlay();

		void draw(CommandBuffer* cb, uint32_t frameIndex);
		void resize(uint32_t width, uint32_t height);

		bool header(const char* caption);
		bool checkBox(const char* caption, bool* value);
		bool checkBox(const char* caption, int32_t* value);
		bool checkBox(const char* caption, uint32_t* value);
		bool inputFloat(const char* caption, float* value, float step, uint32_t precision);
		bool sliderFloat(const char* caption, float* value, float min, float max);
		bool sliderFloat2(const char* caption, float& value0, float& value1, float min, float max);
		bool sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max);
		bool comboBox(const char* caption, int32_t* itemindex, std::vector<std::string> items);
		bool button(const char* caption);
		void text(const char* formatstr, ...);

		// Checks if the vertex and/or index buffers need to be recreated
		bool bufferUpdateRequired(uint32_t frameIndex);
		// (Re)allocate vertex and index buffers
		void allocateBuffers(uint32_t frameIndex);
		// Updates the vertex and index buffers with ImGui's current frame data
		void updateBuffers(uint32_t frameIndex);
	};
}