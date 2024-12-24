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
#include <SFML/Audio.hpp>
#include <json.hpp>
#include "object_types/Monsters.hpp"
#include "entities/Entity.hpp"
#include "entities/Monster.hpp"
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

struct Vertex {
	float pos[3];
	float uv[2];
};

struct InstanceData {
	glm::vec3 pos;
	glm::vec2 scale{ 1.0f };
	uint32_t imageIndex{ 0 };
};
uint32_t instanceCount{ 0 };

ActorManager* actorManager{ nullptr };
AssetManager* assetManager{ nullptr };
AudioManager* audioManager{ nullptr };

const float zFar = 1024.0f * 8.0f;

struct PushConstBlock {
	uint32_t spriteIndex;
} pushConstBlock;

// @todo
class _Game {
public:
	ObjectTypes::MonsterTypes monsterTypes{};
	// @todo: Entity manager
	std::vector<Game::Entities::Monster> monsters;
} game;

class Application : public VulkanApplication {
private:
	struct FrameObjects : public VulkanFrameObjects {
		Buffer* uniformBuffer;
		DescriptorSet* descriptorSet;
	};
	// One set for all images
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	std::vector<VkDescriptorImageInfo> samplerDescriptors{};
	std::vector<vks::Texture2D*> textures{};
	Sampler* spriteSampler{ nullptr };

	std::vector<FrameObjects> frameObjects;
	FileWatcher* fileWatcher{ nullptr };
	DescriptorPool* descriptorPool;
	DescriptorSetLayout* descriptorSetLayoutUniforms;
	DescriptorSetLayout* descriptorSetLayoutSamplers;
	DescriptorSetLayout* descriptorSetLayoutTextures;
	DescriptorSet* descriptorSetTextures;
	DescriptorSet* descriptorSetSamplers;
	std::unordered_map<std::string, PipelineLayout*> pipelineLayouts;
	std::unordered_map<std::string, Pipeline*> pipelines;
	sf::Music backgroundMusic;
	float firingTimer;
	int32_t spriteIndex{ 0 };
	Buffer* quadBuffer{ nullptr };
	Buffer* instanceBuffer{ nullptr };
	glm::vec2 screenDim{ 0.0f };
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

		// @todo: absolute or relative?
		const float aspectRatio = (float)width / (float)height;
		screenDim = glm::vec2(25.0f/* * aspectRatio*/, 25.0f);
		shaderData.projection = glm::ortho(-screenDim.x, screenDim.x, -screenDim.x, screenDim.x);
	}

	~Application() {		
		vkDeviceWaitIdle(VulkanContext::device->logicalDevice);
		for (FrameObjects& frame : frameObjects) {
			destroyBaseFrameObjects(frame);
			delete frame.uniformBuffer;
		}
		if (fileWatcher) {
			fileWatcher->stop();
			delete fileWatcher;
		}
		for (auto& it : pipelines) {
			delete it.second;
		}
		for (auto& texture : textures) {
			delete texture;
		}
		delete descriptorPool;
		delete descriptorSetLayoutUniforms;
		delete assetManager;
		delete actorManager;

		// @todo: move to manager class
		if (backgroundMusic.Playing) {
			backgroundMusic.stop();
		}
		delete audioManager;
		delete quadBuffer;
		delete instanceBuffer;
	}

	void loadAssets() {		
		game.monsterTypes.loadFromFile(getAssetPath() + "game/monsters.json");
		// @todo
		for (auto& set : game.monsterTypes.sets) {
			for (auto& type : set.types) {
				int width, height, channels;
				const std::string fileName = getAssetPath() + "game/monsters/" + type.image;
				unsigned char* img = stbi_load(fileName.c_str(), &width, &height, &channels, 0);
				size_t imgSize = static_cast<uint32_t>(width * height * channels);
				assert(img != nullptr);	

				vks::TextureFromBufferCreateInfo texCI = {
					.buffer = img,
					.bufferSize = imgSize,
					.texWidth = static_cast<uint32_t>(width),
					.texHeight = static_cast<uint32_t>(height),
					.format = VK_FORMAT_R8G8B8A8_SRGB,
					.createSampler = false,
				};
				vks::Texture2D* tex = new vks::Texture2D(texCI);
				textures.push_back(tex);

				stbi_image_free(img);
			}
		}

		SamplerCreateInfo samplerCI {
			.name = "Sprite sampler",
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		};
		spriteSampler = new Sampler(samplerCI);

		// @todo
		// Audio
		const std::map<std::string, std::string> soundFiles = {
			{ "laser", "sounds/laser1.mp3" }
		};

		for (auto& it : soundFiles) {
			audioManager->AddSoundFile(it.first, getAssetPath() + it.second);
		}
	}

	void updateTextureDescriptor() {
		// @todo: actual update logic

		// Use one large descriptor set for all imgages
		textureDescriptors.clear();
		for (auto& tex : textures) {
			textureDescriptors.push_back(tex->descriptor);
		}

		const uint32_t textureCount = static_cast<uint32_t>(textureDescriptors.size());
		descriptorSetLayoutTextures = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = textureCount, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
			}
		});

		descriptorSetTextures = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = textureCount,
			.layouts = { descriptorSetLayoutTextures->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = textureCount, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = textureDescriptors.data()}
			}
		});

		// Samplers
		// @todo: only one sampler right mow
		samplerDescriptors.clear();
		samplerDescriptors.push_back(spriteSampler->descriptor);

		const uint32_t samplerCount = static_cast<uint32_t>(samplerDescriptors.size());
		descriptorSetLayoutSamplers = new DescriptorSetLayout({
			.descriptorIndexing = true,
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = samplerCount, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
			}
		});

		descriptorSetSamplers = new DescriptorSet({
			.pool = descriptorPool,
			.variableDescriptorCount = samplerCount,
			.layouts = { descriptorSetLayoutSamplers->handle },
			.descriptors = {
				{.dstBinding = 0, .descriptorCount = samplerCount, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = samplerDescriptors.data()},
			}
		});
	}

	void generateQuad()
	{
		std::vector<Vertex> vertices =
		{
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },

			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
			{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } },
		};

		const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);

		// Stage to device
		Buffer* stagingBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.size = vertexBufferSize,
			.data = vertices.data()
		});

		quadBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.size = vertexBufferSize,
		});

		CommandBuffer* cb = new CommandBuffer({
			.device = *vulkanDevice,
			.pool = commandPool
		});

		cb->begin();
		VkBufferCopy bufferCopy = { .size = vertexBufferSize };
		vkCmdCopyBuffer(cb->handle, stagingBuffer->buffer, quadBuffer->buffer, 1, &bufferCopy);
		cb->end();
		cb->oneTimeSubmit(queue);
		delete cb;
		
		delete stagingBuffer;
	}

	// @todo
	void spawnMonsters(uint32_t count) {
		// @todo: properl sync, instance buffer per frame in flight
		//vkDeviceWaitIdle(vulkanDevice->logicalDevice);

		// @todo: Just testing
		std::default_random_engine rndGenerator((unsigned)time(nullptr));
		std::uniform_real_distribution<float> posDistX(-screenDim.x, screenDim.x);
		std::uniform_real_distribution<float> posDistY(-screenDim.y, screenDim.y);
		std::uniform_int_distribution<uint32_t> rndTextureIndex(0, static_cast<uint32_t>(textures.size() - 1));

		for (auto i = 0; i < count; i++) {
			Game::Entities::Monster m;
			m.position = glm::vec2(posDistX(rndGenerator), posDistY(rndGenerator));
			m.imageIndex = rndTextureIndex(rndGenerator);
			game.monsters.push_back(m);
		}

		updateInstanceBuffer();
	}

	void updateInstanceBuffer() {

		// @todo: one per frame in flight
		if (instanceBuffer) {
			delete instanceBuffer;
		}

		// Only updated if changed, not needed on every frame
		std::vector<InstanceData> instances{};
		for (auto& monster : game.monsters) {
			InstanceData instance{};
			instance.imageIndex = monster.imageIndex;
			instance.pos = glm::vec3(monster.position, 0.0f);
			instances.push_back(instance);
		}
		instanceCount = static_cast<uint32_t>(instances.size());
		assert(instanceCount > 0);

		const size_t instanceBufferSize = instances.size() * sizeof(InstanceData);

		// @todo: keep a global staging buffer that's reused (and large enough
		Buffer* stagingBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.size = instanceBufferSize,
			.data = instances.data()
		});

		instanceBuffer = new Buffer({
			.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.size = instanceBufferSize
		});

		// @todo: global copy command buffer
		CommandBuffer* cb = new CommandBuffer({ .device = *vulkanDevice, .pool = commandPool });
		cb->begin();
		VkBufferCopy bufferCopy = { .size = instanceBufferSize };
		vkCmdCopyBuffer(cb->handle, stagingBuffer->buffer, instanceBuffer->buffer, 1, &bufferCopy);
		cb->end();
		cb->oneTimeSubmit(queue);
		delete cb;

		delete stagingBuffer;
	}

	void prepare() {
		VulkanApplication::prepare();

		fileWatcher = new FileWatcher();

		loadAssets();

		generateQuad();

		// @todo: Update every frame
		spawnMonsters(256);
		//updateInstanceBuffer();

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
			// @todo
			.maxSets = 32,
//			.maxSets = getFrameCount() + 2,
			.poolSizes = {
				{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 8 /*getFrameCount()*/ },
				{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 4096 /*@todo*/},
				{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 256 /*@todo*/},
			}
		});

		descriptorSetLayoutUniforms = new DescriptorSetLayout({
			.bindings = {
				{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
			}
		});

		for (FrameObjects& frame : frameObjects) {
			frame.descriptorSet = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayoutUniforms->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &frame.uniformBuffer->descriptor }
				}
			});
		}
		
		updateTextureDescriptor();

		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChain->colorFormat;
		pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = depthFormat;

		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = 0xf;

		pipelineLayouts["sprite"] = new PipelineLayout({
			.layouts = { descriptorSetLayoutTextures->handle, descriptorSetLayoutSamplers->handle, descriptorSetLayoutUniforms->handle },
			.pushConstantRanges = {
				// @todo
				{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PushConstBlock) }
			}
		});

		PipelineVertexInput vertexInput = {
			.bindings = {
				{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
				{ .binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
			},
			.attributes = {
				{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
				{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
				// Instanced
				{ .location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(InstanceData, pos) },
				{ .location = 3, .binding = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(InstanceData, scale) },
				{ .location = 4, .binding = 1, .format = VK_FORMAT_R32_SINT, .offset = offsetof(InstanceData, imageIndex) },
			}
		};

		pipelines["sprite"] = new Pipeline({
			.shaders = {
				getAssetPath() + "shaders/sprite.vert.hlsl",
				getAssetPath() + "shaders/sprite.frag.hlsl"
			},
			.cache = pipelineCache,
			.layout = *pipelineLayouts["sprite"],
			.vertexInput = vertexInput,
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

 		pipelineList.push_back(pipelines["sprite"]);

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
		//PushConstBlock pushConstBlock{};
		//pushConstBlock.textureIndex = skyboxIndex;
		//cb->bindPipeline(pipelines["skybox"]);
		//cb->bindDescriptorSets(skyboxPipelineLayout, { frame.descriptorSet, descriptorSetTextures });
		//cb->updatePushConstant(skyboxPipelineLayout, 0, &pushConstBlock);
		//assetManager->models["crate"]->draw(cb->handle, glTFPipelineLayout->handle, glm::mat4(1.0f), true, true);

		PushConstBlock pushConstBlock{};
		pushConstBlock.spriteIndex = spriteIndex;

		// Draw sprites using instancing
		// Instancing buffer stores sprite index, position, scale, direction (to flip/rotate) uv, maybe color for health state

		cb->bindVertexBuffers(0, 1, { quadBuffer->buffer });
		cb->bindVertexBuffers(1, 1, { instanceBuffer->buffer });
		cb->bindDescriptorSets(pipelineLayouts["sprite"], { descriptorSetTextures, descriptorSetSamplers, frame.descriptorSet });
		cb->bindPipeline(pipelines["sprite"]);
		cb->updatePushConstant(pipelineLayouts["sprite"], 0, &pushConstBlock);
		cb->draw(6, instanceCount, 0, 0);
		
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
		shaderData.timer = timer;

		shaderData.view = glm::mat4(1.0f);
		memcpy(currentFrame.uniformBuffer->mapped, &shaderData, sizeof(ShaderData)); // @todo: buffer function

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
		//if (sf::Mouse::isButtonPressed(sf::Mouse::Left) && firingTimer <= 0.0f) {
		//	// @todo: test
		//	audioManager->PlaySnd("laser");
		//	firingTimer = 1.0f;
		//}
		firingTimer -= frameTimer;

		//time += frameTimer;
	}

	void OnUpdateOverlay(vks::UIOverlay& overlay) {
		overlay.text("%d monsters", game.monsters.size());
		//overlay.sliderInt("Spirte index", &spriteIndex, 0, textureDescriptors.size());
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
		if (key == sf::Keyboard::Add) {
			spawnMonsters(4096);
			//spriteIndex++;
			//if (spriteIndex > textureDescriptors.size()) {
			//	spriteIndex = 0;
			//}
		}
		if (key == sf::Keyboard::Subtract) {
			spriteIndex--;
			if (spriteIndex < 0) {
				spriteIndex = textureDescriptors.size() - 1;
			}
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