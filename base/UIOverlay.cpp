/*
 * UI overlay class using ImGui
 *
 * Copyright (C) 2017-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "UIOverlay.h"

namespace vks 
{
	UIOverlay::UIOverlay(OverlayCreateInfo createInfo) : DeviceResource("UIOverlay")
	{
#if defined(__ANDROID__)		
		if (vks::android::screenDensity >= ACONFIGURATION_DENSITY_XXHIGH) {
			scale = 3.5f;
		}
		else if (vks::android::screenDensity >= ACONFIGURATION_DENSITY_XHIGH) {
			scale = 2.5f;
		}
		else if (vks::android::screenDensity >= ACONFIGURATION_DENSITY_HIGH) {
			scale = 2.0f;
		};
#endif

		// Init ImGui
		ImGui::CreateContext();
		// Dimensions
		ImGuiIO& io = ImGui::GetIO();
		io.FontGlobalScale = scale;

		ImGui::StyleColorsLight();

		frameObjects.resize(createInfo.frameCount);
		queue = createInfo.queue;
		scale = createInfo.scale;
		assetPath = createInfo.assetPath;
		fontFileName = createInfo.fontFileName;
		rasterizationSamples = createInfo.rasterizationSamples;

		prepareResources();
		preparePipeline(createInfo.pipelineCache, createInfo.colorFormat, createInfo.depthFormat);
	}

	UIOverlay::~UIOverlay()	{
		ImGui::DestroyContext();
		for (auto& frame : frameObjects) {
			if (frame.vertexBuffer) {
				delete frame.vertexBuffer;
			}
			if (frame.indexBuffer) {
				delete frame.indexBuffer;
			}
		}
		vkFreeMemory(VulkanContext::device->logicalDevice, fontMemory, nullptr);
		delete fontImage;
		delete fontView;
		delete sampler;
		delete descriptorPool;
		delete descriptorSetLayout;
		delete descriptorSet;
		delete pipelineLayout;
		delete pipeline;
	}

	/** Prepare all vulkan resources required to render the UI overlay */
	void UIOverlay::prepareResources()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Create font texture
		unsigned char* fontData;
		int texWidth, texHeight;
#if defined(__ANDROID__)
		float scale = (float)vks::android::screenDensity / (float)ACONFIGURATION_DENSITY_MEDIUM;
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, "Roboto-Medium.ttf", AASSET_MODE_STREAMING);
		if (asset) {
			size_t size = AAsset_getLength(asset);
			assert(size > 0);
			char *fontAsset = new char[size];
			AAsset_read(asset, fontAsset, size);
			AAsset_close(asset);
			io.Fonts->AddFontFromMemoryTTF(fontAsset, size, 14.0f * scale);
			delete[] fontAsset;
		}
#else
		assert(fontFileName != "");
		io.Fonts->AddFontFromFileTTF((assetPath + fontFileName).c_str(), 16.0f);
#endif		
		io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
		VkDeviceSize uploadSize = texWidth*texHeight * 4 * sizeof(char);

		// Create target image for copy
		fontImage = new Image({
			.name = "UI Overlay font image",
			.type = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.extent = { .width = (uint32_t)texWidth, .height = (uint32_t)texHeight, .depth = 1 },
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		});

		fontView = new ImageView(fontImage);

		// Staging buffers for font data upload
		Buffer* stagingBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.size = uploadSize,
			.data = fontData
		});

		// Copy buffer data to font image
		VkCommandBuffer copyCmd = VulkanContext::device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Prepare for transfer
		vks::tools::setImageLayout(
			copyCmd,
			*fontImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT);

		// Copy
		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = texWidth;
		bufferCopyRegion.imageExtent.height = texHeight;
		bufferCopyRegion.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(
			copyCmd,
			stagingBuffer->buffer,
			*fontImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&bufferCopyRegion
		);

		// Prepare for shader read
		vks::tools::setImageLayout(
			copyCmd,
			*fontImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		VulkanContext::device->flushCommandBuffer(copyCmd, queue, true);

		delete stagingBuffer;

		// @todo: replace VK_DESC* constants?

		sampler = new Sampler({
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		});

		VkDescriptorImageInfo fontDescriptor = {
			.sampler = *sampler,
			.imageView = *fontView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		descriptorPool = new DescriptorPool({
			.name = "UI Overlay descriptor pool",
			.maxSets = 1,
			.poolSizes = {
				{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
			}
		});

		descriptorSetLayout = new DescriptorSetLayout({
			.bindings = {
				{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT }
			}
		});

		descriptorSet = new DescriptorSet({
			.pool = descriptorPool,
			.layouts = { descriptorSetLayout->handle },
			.descriptors = {
				{ .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &fontDescriptor }
			}
		});
	}

	/** Prepare a separate pipeline for the UI overlay rendering decoupled from the main application */
	void UIOverlay::preparePipeline(const VkPipelineCache pipelineCache, VkFormat colorFormat, VkFormat depthFormat)
	{
		// New create info to define color, depth and stencil attachments at pipeline create time
		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
		pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = depthFormat;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		pipelineLayout = new PipelineLayout({
			.layouts = { descriptorSetLayout->handle },
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(PushConstBlock) }
			}
		});

		// @todo: thin wrappers for all vulkan structs so you can initialize the whole pipeline

		pipeline = new Pipeline({
			.shaders = {
				assetPath + "shaders/base/overlay.vert.hlsl",
				assetPath + "shaders/base/overlay.frag.hlsl"
			},
			.cache = pipelineCache,
			.layout = *pipelineLayout,
			.vertexInput = {
				.bindings = {
					{ 0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX }
				},
				.attributes = {
					{ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos) },
					{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv) },
					{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col) },
				}
			},
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = rasterizationSamples,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.front = {
					.compareOp = VK_COMPARE_OP_ALWAYS,
				},
				.back = {
					.compareOp = VK_COMPARE_OP_ALWAYS,
				}
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = { 
				DynamicState::Scissor, 
				DynamicState::Viewport 
			},
			.pipelineRenderingInfo = pipelineRenderingCreateInfo
		});
	}

	void UIOverlay::draw(CommandBuffer* cb, uint32_t frameIndex)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		int32_t vertexOffset = 0;
		int32_t indexOffset = 0;

		if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
			return;
		}

		ImGuiIO& io = ImGui::GetIO();

		pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
		pushConstBlock.translate = glm::vec2(-1.0f);

		cb->setViewport(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, 1.0f);
		cb->setScissor(0, 0, (int32_t)io.DisplaySize.x, (int32_t)io.DisplaySize.y);
		cb->bindPipeline(pipeline);
		cb->bindDescriptorSets(pipelineLayout, { descriptorSet });
		cb->updatePushConstant(pipelineLayout, 0, &pushConstBlock);
		// @bind functions for Buffer class
		cb->bindIndexBuffer(frameObjects[frameIndex].indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
		cb->bindVertexBuffers(0, 1, { frameObjects[frameIndex].vertexBuffer->buffer });

		for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
		{
			const ImDrawList* cmd_list = imDrawData->CmdLists[i];
			for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
			{
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
				cb->drawIndexed(pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
				indexOffset += pcmd->ElemCount;
			}
			vertexOffset += cmd_list->VtxBuffer.Size;
		}
	}

	void UIOverlay::resize(uint32_t width, uint32_t height)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(width), (float)(height));
	}

	bool UIOverlay::header(const char *caption)
	{
		return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
	}

	bool UIOverlay::checkBox(const char *caption, bool *value)
	{
		bool res = ImGui::Checkbox(caption, value);
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::checkBox(const char *caption, int32_t *value)
	{
		bool val = (*value == 1);
		bool res = ImGui::Checkbox(caption, &val);
		*value = val;
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::checkBox(const char* caption, uint32_t* value)
	{
		bool val = (*value == 1);
		bool res = ImGui::Checkbox(caption, &val);
		*value = val;
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::inputFloat(const char *caption, float *value, float step, uint32_t precision)
	{
		bool res = ImGui::InputFloat(caption, value, step, step * 10.0f, precision);
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::sliderFloat(const char* caption, float* value, float min, float max)
	{
		bool res = ImGui::SliderFloat(caption, value, min, max);
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::sliderFloat2(const char* caption, float &value0, float &value1, float min, float max)
	{
		float values[2] = { value0, value1 };
		bool res = ImGui::SliderFloat2(caption, values ,min, max);
		if (res) { 
			updated = true; 
			value0 = values[0];
			value1 = values[1];
		};
		return res;
	}

	bool UIOverlay::sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max)
	{
		bool res = ImGui::SliderInt(caption, value, min, max);
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::comboBox(const char *caption, int32_t *itemindex, std::vector<std::string> items)
	{
		if (items.empty()) {
			return false;
		}
		std::vector<const char*> charitems;
		charitems.reserve(items.size());
		for (size_t i = 0; i < items.size(); i++) {
			charitems.push_back(items[i].c_str());
		}
		uint32_t itemCount = static_cast<uint32_t>(charitems.size());
		bool res = ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
		if (res) { updated = true; };
		return res;
	}

	bool UIOverlay::button(const char *caption)
	{
		bool res = ImGui::Button(caption);
		if (res) { updated = true; };
		return res;
	}

	void UIOverlay::text(const char *formatstr, ...)
	{
		va_list args;
		va_start(args, formatstr);
		ImGui::TextV(formatstr, args);
		va_end(args);
	}

	bool UIOverlay::bufferUpdateRequired(uint32_t frameIndex)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		if (!imDrawData) { 
			return false; 
		};

		// Note: Alignment is done inside buffer creation
		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
			return false;
		}

		// We only check if the buffers are too small, so we don't resize if all vertices and indices fit in the already allocated buffer space
		if ((frameObjects[frameIndex].vertexCount < imDrawData->TotalVtxCount) || (frameObjects[frameIndex].indexCount < imDrawData->TotalIdxCount)) {
			return true;
		}

		return false;
	}

	void UIOverlay::allocateBuffers(uint32_t frameIndex)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		if (!imDrawData) {
			return;
		};

		// Vertex buffer
		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		if ((!frameObjects[frameIndex].vertexBuffer) || (imDrawData->TotalVtxCount > frameObjects[frameIndex].vertexCount)) {
			if (frameObjects[frameIndex].vertexBuffer) {
				delete frameObjects[frameIndex].vertexBuffer;
			}
			frameObjects[frameIndex].vertexBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				.size = vertexBufferSize,
				.map = true,
			});
			frameObjects[frameIndex].vertexCount = imDrawData->TotalVtxCount;
		}

		// Index buffer
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
		if ((!frameObjects[frameIndex].indexBuffer) || (imDrawData->TotalIdxCount > frameObjects[frameIndex].indexCount)) {
			if (frameObjects[frameIndex].indexBuffer) {
				delete frameObjects[frameIndex].indexBuffer;
			}
			frameObjects[frameIndex].indexBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				.size = indexBufferSize,
				.map = true,
			});
			frameObjects[frameIndex].indexCount = imDrawData->TotalIdxCount;
		}
	}

	void UIOverlay::updateBuffers(uint32_t frameIndex)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		if (!imDrawData) { 
			return; 
		};

		// Upload current frame data to vertex and index buffer
		if (imDrawData->CmdListsCount > 0) {
			ImDrawVert* vtxDst = (ImDrawVert*)frameObjects[frameIndex].vertexBuffer->mapped;
			ImDrawIdx* idxDst = (ImDrawIdx*)frameObjects[frameIndex].indexBuffer->mapped;

			for (int n = 0; n < imDrawData->CmdListsCount; n++) {
				const ImDrawList* cmd_list = imDrawData->CmdLists[n];
				memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
				vtxDst += cmd_list->VtxBuffer.Size;
				idxDst += cmd_list->IdxBuffer.Size;
			}

			// Flush to make buffer writes visible to GPU
			frameObjects[frameIndex].vertexBuffer->flush();
			frameObjects[frameIndex].indexBuffer->flush();
		}
	}
}