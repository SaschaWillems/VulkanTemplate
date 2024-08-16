/*
 * Vulkan command buffer abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "volk.h"
#include "Initializers.hpp"
#include "DescriptorSet.hpp"
#include "Pipeline.hpp"
#include "PipelineLayout.hpp"
#include "Device.hpp"
#include "CommandPool.hpp"

struct CommandBufferCreateInfo {
	Device& device;
	CommandPool* pool;
};

class CommandBuffer {
private:
	Device& device;
	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
public:
	CommandPool *pool = nullptr;
	VkCommandBuffer handle = VK_NULL_HANDLE;
	CommandBuffer(Device& device) : device(device) {
		this->device = device;
	}
	CommandBuffer(CommandBufferCreateInfo createInfo) : device(createInfo.device) {
		device = createInfo.device;
		pool = createInfo.pool;
		VkCommandBufferAllocateInfo AI = vks::initializers::commandBufferAllocateInfo(pool->handle, level, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &AI, &handle));
	}
	~CommandBuffer() {
		vkFreeCommandBuffers(device, pool->handle, 1, &handle);
	}
	void setLevel(VkCommandBufferLevel level) {
		this->level = level;
	}
	void begin() {
		VkCommandBufferBeginInfo beginInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(handle, &beginInfo));
	}
	void end() {
		VK_CHECK_RESULT(vkEndCommandBuffer(handle));
	}
	void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
		VkViewport viewport = { x, y, width, height, minDepth, maxDepth };
		vkCmdSetViewport(handle, 0, 1, &viewport);
	}
	void setScissor(int32_t offsetx, int32_t offsety, uint32_t width, uint32_t height) {
		VkRect2D scissor = { offsetx, offsety, width, height };
		vkCmdSetScissor(handle, 0, 1, &scissor);
	}
	void bindDescriptorSets(PipelineLayout* layout, std::vector<DescriptorSet*> sets, uint32_t firstSet = 0) {
		std::vector<VkDescriptorSet> descSets;
		for (auto set : sets) {
			descSets.push_back(set->handle);
		}
		vkCmdBindDescriptorSets(handle, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle, firstSet, static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);
	}
	void bindPipeline(Pipeline* pipeline) {
		vkCmdBindPipeline(handle, pipeline->bindPoint, *pipeline);
	}
	void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
		vkCmdDraw(handle, vertexCount, instanceCount, firstVertex, firstInstance);
	}
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
		vkCmdDrawIndexed(handle, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}
	void updatePushConstant(PipelineLayout *layout, uint32_t index, const void* values) {
		VkPushConstantRange pushConstantRange = layout->getPushConstantRange(index);
		vkCmdPushConstants(handle, layout->handle, pushConstantRange.stageFlags, pushConstantRange.offset, pushConstantRange.size, values);
	}
	void insertImageMemoryBarrier(VkImage image, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkImageSubresourceRange subresourceRange)
	{
		VkImageMemoryBarrier imageMemoryBarrier = vks::initializers::imageMemoryBarrier();
		imageMemoryBarrier.srcAccessMask = srcAccessMask;
		imageMemoryBarrier.dstAccessMask = dstAccessMask;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(this->handle, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}
	void insertImageMemoryBarrier(VkImageMemoryBarrier barrier, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		vkCmdPipelineBarrier(this->handle, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}
	void beginRendering(VkRenderingInfo& renderingInfo)
	{
		vkCmdBeginRendering(this->handle, &renderingInfo);
	}
	void endRendering()
	{
		vkCmdEndRendering(this->handle);
	}
	void bindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, std::vector<VkBuffer> buffers, std::vector<VkDeviceSize> offsets = { 0 })
	{
		vkCmdBindVertexBuffers(this->handle, firstBinding, bindingCount, buffers.data(), offsets.data());
	}
	void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset = 0, VkIndexType indexType = VK_INDEX_TYPE_UINT32)
	{
		vkCmdBindIndexBuffer(this->handle, buffer, offset, indexType);
	}
	void oneTimeSubmit(VkQueue queue)
	{
		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &handle;

		VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
		VkFence fence;
		VK_CHECK_RESULT(vkCreateFence(device.logicalDevice, &fenceInfo, nullptr, &fence));
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		VK_CHECK_RESULT(vkWaitForFences(device.logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
		vkDestroyFence(device.logicalDevice, fence, nullptr);
		vkResetCommandBuffer(handle, 0);
	}
};