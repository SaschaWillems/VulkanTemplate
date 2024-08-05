/*
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanContext.h"
#include "FileWatcher.hpp"
#include <VulkanApplication.h>
#include "AssetManager.h"
#include "glTF.h"

// @todo: audio (music and sfx)
#ifdef TRACY_ENABLE
void* operator new(size_t count)
{
	auto ptr = malloc(count);
	TracyAlloc(ptr, count);
	return ptr;
}

void operator delete(void* ptr) noexcept
{
	TracyFree(ptr);
	free(ptr);
}
#endif

std::vector<Pipeline*> pipelineList{};

struct ShaderData {
	glm::mat4 projection;
	glm::mat4 view;
	float time{ 0.0f };
	float timer{ 0.0f };
} shaderData;

AssetManager* assetManager{ nullptr };

class Application : public VulkanApplication {
private:
	struct FrameObjects : public VulkanFrameObjects {
		Buffer* uniformBuffer;
		DescriptorSet* descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	Pipeline* testPipeline;
	Pipeline* glTFPipeline;
	PipelineLayout* glTFPipelineLayout;
	PipelineLayout* testPipelineLayout;
	FileWatcher* fileWatcher{ nullptr };
	DescriptorPool* descriptorPool;
	DescriptorSetLayout* descriptorSetLayout;
	float time{ 0.0f };
public:	
	Application() : VulkanApplication() {
		apiVersion = VK_API_VERSION_1_3;

		Device::enabledFeatures.shaderClipDistance = VK_TRUE;
		Device::enabledFeatures.samplerAnisotropy = VK_TRUE;
		Device::enabledFeatures.depthClamp = VK_TRUE;
		Device::enabledFeatures.fillModeNonSolid = VK_TRUE;

		Device::enabledFeatures11.multiview = VK_TRUE;
		Device::enabledFeatures12.descriptorIndexing = VK_TRUE;
		Device::enabledFeatures12.runtimeDescriptorArray = VK_TRUE;
		Device::enabledFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
		Device::enabledFeatures13.dynamicRendering = VK_TRUE;

		settings.sampleCount = VK_SAMPLE_COUNT_4_BIT;

		assetManager = new AssetManager();
	}

	~Application() {
		for (FrameObjects& frame : frameObjects) {
			destroyBaseFrameObjects(frame);
		}
		if (fileWatcher) {
			fileWatcher->stop();
			delete fileWatcher;
		}
		delete testPipeline;
		delete glTFPipeline;
		delete descriptorPool;
		delete descriptorSetLayout;
		delete assetManager;
	}

	void prepare() {
		VulkanApplication::prepare();

		// @todo: move camera out of vulkanapplication (so we can have multiple cameras)
		camera.type = Camera::CameraType::lookat;
		//camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 1024.0f);
		camera.rotate(-90.0f, 0.0f);
		camera.setPosition({ 0.0f, 0.0f, -12.0f });

		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			frameObjects.resize(getFrameCount());
			frame.uniformBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				.size = sizeof(ShaderData),
			});
		}

		descriptorPool = new DescriptorPool({
			.name = "Test descriptor pool",
			.maxSets = getFrameCount(),
			.poolSizes = {
				{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = getFrameCount() },
			}
		});

		descriptorSetLayout = new DescriptorSetLayout({
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
			}
		});

		for (FrameObjects& frame : frameObjects) {
			frame.descriptorSet = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayout->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &frame.uniformBuffer->descriptor }
				}
			});
		}

		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChain->colorFormat;
		pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = depthFormat;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = 0xf;

		testPipelineLayout = new PipelineLayout({
			.layouts = { descriptorSetLayout->handle },
		});

		testPipeline = new Pipeline({
			.name = "Fullscreen pass pipeline",
			.shaders = {
				getAssetPath() + "shaders/fullscreen.vert.hlsl",
				getAssetPath() + "shaders/fullscreen.frag.hlsl"
			},
			.cache = pipelineCache,
			.layout = *testPipelineLayout,
			.inputAssemblyState = {
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			},
			.viewportState = {
				.viewportCount = 1,
				.scissorCount = 1
			},
			.rasterizationState = {
				.polygonMode = VK_POLYGON_MODE_FILL,
				.cullMode = VK_CULL_MODE_NONE,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = pipelineRenderingCreateInfo,
			.enableHotReload = true
			});

		glTFPipelineLayout = new PipelineLayout({
			.layouts = { descriptorSetLayout->handle },
			.pushConstantRanges = {
				{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(glm::mat4) }
			}
		});

		assetManager->add("crate", new vkglTF::Model({
			.pipelineLayout = glTFPipelineLayout->handle,
			.filename = getAssetPath() + "models/crate.gltf",
			.enableHotReload = true
		}));

		assetManager->add("text", new vkglTF::Model({
			.pipelineLayout = glTFPipelineLayout->handle,
			.filename = getAssetPath() + "models/text.gltf",
			.enableHotReload = true
		}));
			.enableHotReload = true
		}));

		// Use one large descriptor set for all imgages (aka "bindless")
		std::vector<VkDescriptorImageInfo> textureDescriptors{};
		for (auto i = 0; i < ApplicationContext::assetManager->textures.size(); i++) {
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.sampler = ApplicationContext::assetManager->textures[i]->sampler;
			imageInfo.imageView = ApplicationContext::assetManager->textures[i]->view;
			textureDescriptors.push_back(imageInfo);
		};
		descriptorSetLayoutTextures = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()), .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
			}
		});

		glTFPipelineLayout = new PipelineLayout({
			.layouts = { descriptorSetLayout->handle, descriptorSetLayoutTextures->handle },
			.pushConstantRanges = {
				// @todo
				{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(vkglTF::PushConstBlock) }
			}
		});
		glTFPipeline = new Pipeline({
			.shaders = {
				getAssetPath() + "shaders/gltf.vert.hlsl",
				getAssetPath() + "shaders/gltf.frag.hlsl"
			},
			.cache = pipelineCache,
			.layout = *glTFPipelineLayout,
			.vertexInput = vkglTF::vertexInput,
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
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
				.lineWidth = 1.0f
			},
			.multisampleState = {
				.rasterizationSamples = settings.sampleCount,
			},
			.depthStencilState = {
				.depthTestEnable = VK_TRUE,
				.depthWriteEnable = VK_TRUE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			},
			.blending = {
				.attachments = { blendAttachmentState }
			},
			.dynamicState = {
				DynamicState::Scissor,
				DynamicState::Viewport
			},
			.pipelineRenderingInfo = pipelineRenderingCreateInfo,
			.enableHotReload = true
			});
		descriptorSetTextures = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
			.layouts = { descriptorSetLayoutTextures->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = textureDescriptors.data()}
			}
		});

		pipelineList.push_back(testPipeline);
		pipelineList.push_back(glTFPipeline);

		fileWatcher = new FileWatcher();
		for (auto& pipeline : pipelineList) {
			fileWatcher->addPipeline(pipeline);
		}
		for (auto& it : assetManager->models) {
			fileWatcher->addFile(it.second->initialCreateInfo->filename, it.second);
		}
		fileWatcher->onFileChanged = [=](const std::string filename, const std::vector<void*> userdata) {
			this->onFileChanged(filename, userdata);
		};
		fileWatcher->start();


		prepared = true;
	}

	void recordCommandBuffer(FrameObjects& frame)
	{
		ZoneScopedN("Command buffer recording");

		CommandBuffer* commandBuffer = frame.commandBuffer;

		const bool multiSampling = (settings.sampleCount > VK_SAMPLE_COUNT_1_BIT);

		CommandBuffer* cb = commandBuffer;
		cb->begin();

		// New structures are used to define the attachments used in dynamic rendering
		VkRenderingAttachmentInfo colorAttachment{};
		VkRenderingAttachmentInfo depthStencilAttachment{};		

		// Transition color and depth images for drawing
		cb->insertImageMemoryBarrier(
			swapChain->buffers[swapChain->currentImageIndex].image,
			0,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		cb->insertImageMemoryBarrier(
			depthStencil.image,
			0,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

		// New structures are used to define the attachments used in dynamic rendering
		colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		colorAttachment.imageView = multiSampling ? multisampleTarget.color.view : swapChain->buffers[swapChain->currentImageIndex].view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (multiSampling) {
			colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			colorAttachment.resolveImageView = swapChain->buffers[swapChain->currentImageIndex].view;
			colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		}

		// A single depth stencil attachment info can be used, but they can also be specified separately.
		// When both are specified separately, the only requirement is that the image view is identical.			
		depthStencilAttachment = {};
		depthStencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		depthStencilAttachment.imageView = multiSampling ? multisampleTarget.depth.view : depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue.depthStencil = { 1.0f,  0 };
		if (multiSampling) {
			depthStencilAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			depthStencilAttachment.resolveImageView = depthStencil.view;
			depthStencilAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
		}

		VkRenderingInfo renderingInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
			.renderArea = { 0, 0, width, height },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachment,
			.pDepthAttachment = &depthStencilAttachment,
			.pStencilAttachment = &depthStencilAttachment
		};

		cb->beginRendering(renderingInfo);
		cb->setViewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);
		cb->setScissor(0, 0, width, height);

		cb->bindPipeline(testPipeline);
		cb->bindDescriptorSets(testPipelineLayout, { frame.descriptorSet });
		cb->draw(3, 1, 0, 0);

		// @todo
		cb->bindPipeline(glTFPipeline);
		for (auto& it : assetManager->models) {
			it.second->draw(cb->handle);
		}

		if (overlay->visible) {
			overlay->draw(cb, getCurrentFrameIndex());
		}
		cb->endRendering();

		// Transition color image for presentation
		cb->insertImageMemoryBarrier(
			swapChain->buffers[swapChain->currentImageIndex].image,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		cb->end();
	}

	void render() {
		ZoneScopedN("render");

		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];
		VulkanApplication::prepareFrame(currentFrame);
		updateOverlay(getCurrentFrameIndex());
		shaderData.time = time;
		shaderData.timer = timer;
		shaderData.projection = camera.matrices.perspective;
		shaderData.view = camera.matrices.view;
		memcpy(currentFrame.uniformBuffer->mapped, &shaderData, sizeof(ShaderData)); // @todo: buffer function
		recordCommandBuffer(currentFrame);
		VulkanApplication::submitFrame(currentFrame);

		for (auto& pipeline : pipelineList) {
			if (pipeline->wantsReload) {
				pipeline->reload();
			}
		}

		// @todo: work in progress
		for (auto& it : assetManager->models) {
			if (it.second->wantsReload) {
				vkglTF::Model* newModel = new vkglTF::Model(*it.second->initialCreateInfo);
				// @todo: check if this works
				delete it.second;
				it.second = newModel;
			}
		}

		time += frameTimer;
	}

	void OnUpdateOverlay(vks::UIOverlay& overlay) {
		overlay.text("Time: %f", time);
		overlay.text("Timer: %f", timer);
	}

	void onFileChanged(const std::string filename, const std::vector<void*> owners) {
		std::cout << filename << " was modified\n";
		for (auto& owner : owners) {
			if (std::find(pipelineList.begin(), pipelineList.end(), owner) != pipelineList.end()) {
				static_cast<Pipeline*>(owner)->wantsReload = true;
			}
			// @todo
			for (auto& it : assetManager->models) {
				if (it.second == owner) {
					static_cast<vkglTF::Model*>(owner)->wantsReload = true;
				}
			}
		}
	}

};
Application* vulkanApplication;

// Main entry points

#if defined(_WIN32)

// Windows entry point
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanApplication != NULL)
	{
		vulkanApplication->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowmd)
{
	for (int32_t i = 0; i < __argc; i++) { 
		VulkanApplication::args.push_back(__argv[i]); 
	};
	vulkanApplication = new Application();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow(hInstance, WndProc);
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point

VulkanApplication *vulkanApplication;																
void android_main(android_app* state)																
{																									
	vulkanApplication = new VulkanApplication();													
	state->userData = vulkanApplication;															
	state->onAppCmd = vulkanApplication::handleAppCommand;											
	state->onInputEvent = vulkanApplication::handleAppInput;										
	androidApp = state;																				
	vks::android::getDeviceConfig();																
	vulkanApplication->renderLoop();																
	delete(vulkanApplication);																		
}

#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi

VulkanApplication *vulkanApplication;																
static void handleEvent()                                											
{																									
}																									

int main(const int argc, const char *argv[])													    
{																									
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  			
	vulkanApplication = new VulkanApplication();													
	vulkanApplication->initVulkan();																
	vulkanApplication->prepare();																	
	vulkanApplication->renderLoop();																
	delete(vulkanApplication);																		
	return 0;																						
}

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)

	int main(const int argc, const char *argv[])												
{																								
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  		
	vulkanApplication = new VulkanApplication();												
	vulkanApplication->initVulkan();															
	vulkanApplication->setupWindow();					 										
	vulkanApplication->prepare();																
	vulkanApplication->renderLoop();															
	delete(vulkanApplication);																	
	return 0;																					
}

#elif defined(VK_USE_PLATFORM_XCB_KHR)

static void handleEvent(const xcb_generic_event_t *event)										
{																								
	if (vulkanApplication != NULL)																
	{																							
		vulkanApplication->handleEvent(event);													
	}																							
}				
	\
int main(const int argc, const char *argv[])													
{																								
	for (size_t i = 0; i < argc; i++) { vulkanApplication::args.push_back(argv[i]); };  		
	vulkanApplication = new VulkanApplication();												
	vulkanApplication->initVulkan();															
	vulkanApplication->setupWindow();					 										
	vulkanApplication->prepare();																
	vulkanApplication->renderLoop();															
	delete(vulkanApplication);																	
	return 0;																					
}

#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
#endif