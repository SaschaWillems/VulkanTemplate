/*
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanContext.h"
#include "ApplicationContext.h"
#include "FileWatcher.hpp"
#include <VulkanApplication.h>
#include "AssetManager.h"
#include "AudioManager.h"
#include "Texture.hpp"
#include "glTF.h"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <random>
#include "time.h"
#include "Frustum.hpp"
#include <SFML/Audio.hpp>
#include <json.hpp>
#include "object_types/Monsters.hpp"
#include "stb_image.h"

// @todo: audio (music and sfx)
// @todo: sync2 everywhere
// @todo: timeline semaphores

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

uint32_t skyboxIndex{ 0 };

ActorManager* actorManager{ nullptr };
AssetManager* assetManager{ nullptr };
AudioManager* audioManager{ nullptr };
Actor* ship{ nullptr };

const float zFar = 1024.0f * 8.0f;

vks::Frustum frustum;
uint32_t visibleObjects{ 0 };

struct PushConstBlock {
	glm::mat4 matrix;
	uint32_t textureIndex;
	uint32_t radianceIndex;
	uint32_t irradianceIndex;
} pushConstBlock;

struct Skybox {
	uint32_t brdfLUT{ 0 };
	uint32_t radianceIndex{ 0 };
	uint32_t irradianceIndex{ 0 };
} skybox;

// @todo
class Game {
public:
	ObjectTypes::MonsterTypes monsterTypes{};
} game;

class Application : public VulkanApplication {
private:
	struct FrameObjects : public VulkanFrameObjects {
		Buffer* uniformBuffer;
		DescriptorSet* descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	PipelineLayout* glTFPipelineLayout;
	PipelineLayout* skyboxPipelineLayout;
	FileWatcher* fileWatcher{ nullptr };
	DescriptorPool* descriptorPool;
	DescriptorSetLayout* descriptorSetLayout;
	DescriptorSetLayout* descriptorSetLayoutTextures;
	DescriptorSet* descriptorSetTextures;
	std::unordered_map<std::string, Pipeline*> pipelines;
	sf::Music backgroundMusic;
	float firingTimer;
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
		actorManager = new ActorManager();
		audioManager = new AudioManager();

		ApplicationContext::assetManager = assetManager;

		dxcCompiler = new Dxc();
	}

	~Application() {		
		vkDeviceWaitIdle(VulkanContext::device->logicalDevice);
		for (FrameObjects& frame : frameObjects) {
			destroyBaseFrameObjects(frame);
		}
		if (fileWatcher) {
			fileWatcher->stop();
			delete fileWatcher;
		}
		for (auto& it : pipelines) {
			delete it.second;
		}
		delete descriptorPool;
		delete descriptorSetLayout;
		delete assetManager;
		delete actorManager;

		// @todo: move to manager class
		if (backgroundMusic.Playing) {
			backgroundMusic.stop();
		}
		delete audioManager;
	}

	void loadAssets() {		
		game.monsterTypes.loadFromFile(getAssetPath() + "data/game/monsters.json");

		// @todo
		// Audio
		const std::map<std::string, std::string> soundFiles = {
			{ "laser", "sounds/laser1.mp3" }
		};

		for (auto& it : soundFiles) {
			audioManager->AddSoundFile(it.first, getAssetPath() + it.second);
		}
	}

	void prepare() {
		VulkanApplication::prepare();

		fileWatcher = new FileWatcher();

		loadAssets();

		// @todo: move camera out of vulkanapplication (so we can have multiple cameras)
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, zFar);
		camera.setPosition({ 0.0f, -30.0f, 80.0f });
//		camera.setPosition({ 0.0f, 0.0f, 60.0f });


		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			frameObjects.resize(getFrameCount());
			frame.uniformBuffer = new Buffer({
				.usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.size = sizeof(ShaderData),
			});
		}

		descriptorPool = new DescriptorPool({
			.name = "Application descriptor pool",
			.maxSets = getFrameCount() + 1,
			.poolSizes = {
				{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1024 /*getFrameCount()*/ },
				{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1024 /*@todo*/},
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
		
		// One large set for all textures

		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChain->colorFormat;
		pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = depthFormat;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = 0xf;

		// Use one large descriptor set for all imgages (aka "bindless")
		std::vector<VkDescriptorImageInfo> textureDescriptors{};
		for (auto i = 0; i < assetManager->textures.size(); i++) {
			// @todo: directly construct from asset manager=?
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.sampler = assetManager->textures[i]->sampler;
			imageInfo.imageView = assetManager->textures[i]->view;
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
				{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PushConstBlock) }
			}
		});

		pipelines["gltf"] = new Pipeline({
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
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
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
			.pipelineRenderingInfo = {
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &swapChain->colorFormat,
				.depthAttachmentFormat = depthFormat,
				.stencilAttachmentFormat = depthFormat
			},
			.enableHotReload = true
		});

		pipelines["playership"] = new Pipeline({
			.shaders = {
				getAssetPath() + "shaders/playership.vert.hlsl",
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
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
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

		//

		descriptorSetTextures = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
			.layouts = { descriptorSetLayoutTextures->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = textureDescriptors.data()}
			}
		});

		// @todo: push consts also used by gltf renderer
		skyboxPipelineLayout = new PipelineLayout({
			.layouts = { descriptorSetLayout->handle, descriptorSetLayoutTextures->handle },
			.pushConstantRanges = {
				{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PushConstBlock)}
			}
		});

		pipelines["skybox"] = new Pipeline({
			.shaders = {
				getAssetPath() + "shaders/skybox.vert.hlsl",
				getAssetPath() + "shaders/skybox.frag.hlsl"
			},
			.cache = pipelineCache,
			.layout = *skyboxPipelineLayout,
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
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
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

		pipelineList.push_back(pipelines["skybox"]);
		pipelineList.push_back(pipelines["playership"]);
		pipelineList.push_back(pipelines["gltf"]);

		for (auto& pipeline : pipelineList) {
			fileWatcher->addPipeline(pipeline);
		}
		fileWatcher->onFileChanged = [=](const std::string filename, const std::vector<void*> userdata) {
			this->onFileChanged(filename, userdata);
		};
		fileWatcher->start();

		// @todo
		if (backgroundMusic.openFromFile(getAssetPath() + "music/singularity_calm.mp3")) {
			backgroundMusic.setVolume(30);
			backgroundMusic.play();
		} else {
			std::cout << "Could not load background music track\n";
		}
		prepared = true;
	}

	void recordCommandBuffer(FrameObjects& frame)
	{
		ZoneScopedN("Command buffer recording");

		const bool multiSampling = (settings.sampleCount > VK_SAMPLE_COUNT_1_BIT);

		CommandBuffer* cb = frame.commandBuffer;
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

		// Backdrop
		PushConstBlock pushConstBlock{};
		pushConstBlock.textureIndex = skyboxIndex;
		cb->bindPipeline(pipelines["skybox"]);
		cb->bindDescriptorSets(skyboxPipelineLayout, { frame.descriptorSet, descriptorSetTextures });
		cb->updatePushConstant(skyboxPipelineLayout, 0, &pushConstBlock);
		assetManager->models["crate"]->draw(cb->handle, glTFPipelineLayout->handle, glm::mat4(1.0f), true, true);

		// @todo
		vkglTF::pushConstBlock.irradianceIndex = skybox.irradianceIndex;
		vkglTF::pushConstBlock.radianceIndex = skybox.radianceIndex;

		cb->bindDescriptorSets(glTFPipelineLayout, { frame.descriptorSet, descriptorSetTextures });
		
		//glm::vec3 currPos = { 0.0f, 8.0f, -30.0f }; //playerShip.localPosition;// +glm::vec3(0.0f, 0.0f, -playerShip.acceleration * 2.0f);
		// glm::mat4 locMatrix = glm::translate(glm::mat4(1.0f), currPos);
		// locMatrix = glm::scale(locMatrix, glm::vec3(0.5f));
		// actorManager->actors["playership"]->position = camera.position * glm::vec3(-1.0f);
		// cb->bindPipeline(pipelines["playership"]);
		// ship->model->draw(cb->handle, glTFPipelineLayout->handle, locMatrix);
		
		cb->bindPipeline(pipelines["gltf"]);
		
		// @todo: instancing
		vkglTF::Model* lastBoundModel{ nullptr };
		visibleObjects = 0;
		auto modelChanges = 0;
		for (auto& it : actorManager->actors) {
			auto actor = it.second;
			if (frustum.checkSphere(actor->position, actor->getRadius() * 2.0f)) {
				if (actor->model != lastBoundModel) {
					lastBoundModel = actor->model;
					actor->model->bindBuffers(cb->handle);
					modelChanges++;
				}
				visibleObjects++;
				glm::mat4 locMatrix = actor->getMatrix();
				lastBoundModel->draw(cb->handle, glTFPipelineLayout->handle, locMatrix);
			}
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
		ZoneScoped;

		camera.viewportSize = glm::uvec2(width, height);

		camera.mouse.buttons.left = mouseButtons.left;
		camera.mouse.cursorPos = mousePos;
		camera.mouse.cursorPosNDC = (mousePos / glm::vec2(float(width), float(height)));

		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];
		VulkanApplication::prepareFrame(currentFrame);
		updateOverlay(getCurrentFrameIndex());
		//shaderData.time = time;
		shaderData.timer = timer;

		shaderData.projection = camera.matrices.perspective;
		shaderData.view = camera.matrices.view;
		memcpy(currentFrame.uniformBuffer->mapped, &shaderData, sizeof(ShaderData)); // @todo: buffer function

		frustum.update(camera.matrices.perspective * camera.matrices.view);

		for (auto& it : actorManager->actors) {
			it.second->update(frameTimer);
		}

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

		// @todo
		if (sf::Mouse::isButtonPressed(sf::Mouse::Left) && firingTimer <= 0.0f) {
			// @todo: test
			actorManager->addActor("bullet" + std::to_string(actorManager->actors.size() + 1), new Actor({
				.position = glm::vec3(camera.position),
				.rotation = glm::vec3(0.0f),
				.scale = glm::vec3(0.5f),
				.model = assetManager->models["bullet"],
				.tag = "bullet",
				// @todo: velocity from player ship
				.constantVelocity = glm::vec3(camera.getForward()) * 100.0f
				}));
			audioManager->PlaySnd("laser");
			firingTimer = 1.0f;
		}
		firingTimer -= frameTimer;

		//time += frameTimer;
	}

	void OnUpdateOverlay(vks::UIOverlay& overlay) {
		overlay.text("visible objects: %d", visibleObjects);
		overlay.text("%.6f", camera.targetAngularVelocity.x - camera.angularVelocity.x);
		overlay.text("%.6f", camera.targetAngularVelocity.y - camera.angularVelocity.y);
		//overlay.text("Cursor NDC: %.2f, %.2f", camera.mouse.cursorPosNDC.x, camera.mouse.cursorPosNDC.y);
	}

	void onFileChanged(const std::string filename, const std::vector<void*> owners) {
		std::cout << filename << " was modified\n";
		for (auto& owner : owners) {
			if (std::find(pipelineList.begin(), pipelineList.end(), owner) != pipelineList.end()) {
				static_cast<Pipeline*>(owner)->wantsReload = true;
			}
			for (auto& it : assetManager->models) {
				if (it.second == owner) {
					static_cast<vkglTF::Model*>(owner)->wantsReload = true;
				}
			}
		}
	}

	virtual void keyPressed(uint32_t key)
	{
		if (key == sf::Keyboard::P) {
			camera.physicsBased = !camera.physicsBased;
		}
		if (key == sf::Keyboard::C) {
			camera.mouse.cursorLock = !camera.mouse.cursorLock;
		}
		if (key == sf::Keyboard::L) {
			camera.mouse.cursorLock = !camera.mouse.cursorLock;
		}
	}

};
Application* vulkanApplication;

// Main entry points

#if defined(_WIN32)
// Windows entry point
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowmd)
{
	for (int32_t i = 0; i < __argc; i++) { 
		VulkanApplication::args.push_back(__argv[i]); 
	};
	vulkanApplication = new Application();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow();
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