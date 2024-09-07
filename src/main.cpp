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
#include "simulation/RigidBody.hpp"
#include <random>
#include "time.h"
#include "Frustum.hpp"
#include <SFML/Audio.hpp>

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
		const std::map<std::string, std::string> files = {
			{ "crate", "models/crate_up.glb" },
			{ "asteroid", "models/asteroid.glb" },
			{ "moon", "models/moon.gltf" },
			{ "spaceship", "models/spaceship/scene_ktx.gltf" },
			{ "bullet", "models/bullet.glb" }
		};

		// @todo: from JSON?
		const bool hotReload = true;
		for (auto& it : files) {
			const std::string filename = getAssetPath() + it.second;
			auto model = assetManager->add(it.first, new vkglTF::Model({
				.filename = filename,
				.enableHotReload = hotReload
			}));
			fileWatcher->addFile(filename, model);
		}

		// Additional textures
		// @todo
		skyboxIndex = assetManager->add("skybox", new vks::TextureCubeMap({
			.filename = getAssetPath() + "textures/space01.ktx",
			//.filename = getAssetPath() + "textures/cubemap01.ktx",
			//.format = VK_FORMAT_R8G8B8A8_SRGB,
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		}));

		skybox.brdfLUT = assetManager->add("brdflut", new vks::Texture2D({
			.filename = getAssetPath() + "textures/brdflut.ktx",
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		}));

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

		generateCubemaps(static_cast<vks::TextureCubeMap*>(assetManager->textures[skyboxIndex]));

		// @todo: move camera out of vulkanapplication (so we can have multiple cameras)
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, zFar);
		camera.setPosition({ 0.0f, -30.0f, 80.0f });
//		camera.setPosition({ 0.0f, 0.0f, 60.0f });

		//playerShip.localPosition = { 0.0f, 8.0f, -30.0f };
		//playerShip.localPosition = { 0.0f, 0.0f, 0.0f };
		//playerShip.localRotation = { 0.0f, 0.0f, 0.0f };

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
			.pipelineRenderingInfo = pipelineRenderingCreateInfo,
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

		//ship = actorManager->addActor("playership", new Actor({
		//	.position = glm::vec3(0.0f),
		//	.rotation = glm::vec3(0.0f),
		//	.scale = glm::vec3(0.5f),
		//	.model = assetManager->models["spaceship"]
		//}));

		//actorManager->addActor("orientation_crate", new Actor({
		//	.position = glm::vec3(0.0f, 0.0f, -15.0f),
		//	.rotation = glm::vec3(0.0f),
		//	.scale = glm::vec3(0.5f),
		//	.model = assetManager->models["crate"],
		//}));

		// Set up a grid of asteroids for testing purposes
		std::default_random_engine rndGenerator((unsigned)time(nullptr));
		//std::uniform_real_distribution<float> uniformDist(-1.0f, 1.0f);
		//const int r = 8;
		//const float s = 8.0f;
		uint32_t a_idx = 0;
		//for (int32_t x = -r; x < r; x++) {
		//	for (int32_t y = -r; y < r; y++) {
		//		for (int32_t z = -r; z < r; z++) {
		//			glm::vec3 rndOffset = glm::vec3(uniformDist(rndGenerator), uniformDist(rndGenerator), uniformDist(rndGenerator)) * 5.0f;
		//			actorManager->addActor("asteroid" + std::to_string(a_idx), new Actor({
		//				.position = (glm::vec3(x, y, z) + rndOffset) * s,
		//				.rotation = glm::vec3(360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator)),
		//				.scale = glm::vec3(5.0f + uniformDist(rndGenerator) * 2.5f - uniformDist(rndGenerator) * 2.5f),
		//				.model = assetManager->models["asteroid"],
		//				.tag = "asteroid"
		//			}));
		//			a_idx++;
		//		}
		//	}
		//}

		const uint32_t asteroidCount = 8192;

		std::uniform_real_distribution<float> uniformDist(0.0, 1.0);

		// Distribute rocks randomly on two different rings
		for (auto i = 0; i < asteroidCount / 2; i++) {
			glm::vec2 ring0{ 7.0f, 16.0f };
			glm::vec2 ring1{ 14.0f, 24.0f };

			ring0 *= 10.0f;
			ring1 *= 15.0f;

			float rho, theta;


			// Inner ring
			rho = sqrt((pow(ring0[1], 2.0f) - pow(ring0[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring0[0], 2.0f));
			theta = static_cast<float>(2.0f * M_PI * uniformDist(rndGenerator));
			actorManager->addActor("asteroid" + std::to_string(a_idx), new Actor({
				.position = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 16.0f, rho * sin(theta)),
				.rotation = glm::vec3(360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator)),
				.scale = glm::vec3(5.0f + uniformDist(rndGenerator) * 2.5f - uniformDist(rndGenerator) * 2.5f),
				.model = assetManager->models["asteroid"],
				.tag = "asteroid"
			}));
			a_idx++;

			// Outer ring
			rho = sqrt((pow(ring1[1], 2.0f) - pow(ring1[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring1[0], 2.0f));
			theta = static_cast<float>(2.0f * M_PI * uniformDist(rndGenerator));
			actorManager->addActor("asteroid" + std::to_string(a_idx), new Actor({
				.position = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 16.0f, rho * sin(theta)),
				.rotation = glm::vec3(360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator), 360.0f * uniformDist(rndGenerator)),
				.scale = glm::vec3(5.0f + uniformDist(rndGenerator) * 2.5f - uniformDist(rndGenerator) * 2.5f),
				.model = assetManager->models["asteroid"],
				.tag = "asteroid"
				}));
			a_idx++;
		}

		actorManager->addActor("moon", new Actor({
			.position = glm::vec3(0.0f, 0.0f, 0.0f),
			.rotation = glm::vec3(0.0f),
			.scale = glm::vec3(5.0f),
			.model = assetManager->models["moon"],
			.tag = "moon"
		}));

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

#pragma region PBR
	void generateCubemaps(vks::TextureCubeMap* source)
	{
		enum Target { IRRADIANCE = 0, RADIANCE = 1 };

		for (uint32_t target = 0; target < RADIANCE + 1; target++) {

			vks::TextureCubeMap* cubemap = new vks::TextureCubeMap();

			auto tStart = std::chrono::high_resolution_clock::now();

			VkFormat format;
			uint32_t dim;

			switch (target) {
			case IRRADIANCE:
				format = VK_FORMAT_R32G32B32A32_SFLOAT;
				dim = 64;
				break;
			case RADIANCE:
				format = VK_FORMAT_R16G16B16A16_SFLOAT;
				dim = 512;
				break;
			};

			const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

			Device* device = VulkanContext::device;

			// Create target cubemap
			// Image
			VkImageCreateInfo imageCI = vks::initializers::imageCreateInfo();
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = format;
			imageCI.extent.width = dim;
			imageCI.extent.height = dim;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = numMips;
			imageCI.arrayLayers = 6;
			imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCI, nullptr, &cubemap->image));
			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device->logicalDevice, cubemap->image, &memReqs);
			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &cubemap->deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, cubemap->image, cubemap->deviceMemory, 0));

			// View
			VkImageViewCreateInfo viewCI = vks::initializers::imageViewCreateInfo();
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			viewCI.format = format;
			viewCI.subresourceRange = {};
			viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCI.subresourceRange.levelCount = numMips;
			viewCI.subresourceRange.layerCount = 6;
			viewCI.image = cubemap->image;
			VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCI, nullptr, &cubemap->view));

			VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
			samplerCI.magFilter = VK_FILTER_LINEAR;
			samplerCI.minFilter = VK_FILTER_LINEAR;
			samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCI.minLod = 0.0f;
			samplerCI.maxLod = static_cast<float>(numMips);
			samplerCI.maxAnisotropy = 1.0f;
			samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCI, nullptr, &cubemap->sampler));

			// Create offscreen framebuffer
			Image* offscreen = new Image({
				.name = "Offscreen cubemap generation image",
				.type = VK_IMAGE_TYPE_2D,
				.format = format,
				.extent = {
					.width = dim,
					.height = dim,
					.depth = 1 
				},
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			});

			ImageView* offscreenView = new ImageView(offscreen);

			// Descriptors
			DescriptorPool* descriptorPool = new DescriptorPool({
				.maxSets = 1,
				.poolSizes = {
					{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
				}
			});

			DescriptorSetLayout* descriptorSetLayout = new DescriptorSetLayout({
				.bindings = {
					{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT }
				}
			});

			DescriptorSet* descriptorSet = new DescriptorSet({
				.pool = descriptorPool,
				.layouts = { descriptorSetLayout->handle },
				.descriptors = {
					{.dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &source->descriptor }
				}
			});

			struct PushBlockIrradiance {
				glm::mat4 mvp;
				float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
				float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
			} pushBlockIrradiance;

			struct PushBlockPrefilterEnv {
				glm::mat4 mvp;
				float roughness = 0.0f;
				uint32_t numSamples = 32u;
			} pushBlockPrefilterEnv;

			const uint32_t pushConstSize = static_cast<uint32_t>((target == IRRADIANCE ? sizeof(PushBlockIrradiance) : sizeof(PushBlockPrefilterEnv)));
			PipelineLayout* pipelineLayout = new PipelineLayout({
				.layouts = { descriptorSetLayout->handle },
				.pushConstantRanges = {
					{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = pushConstSize }
				}
			});

			std::string vertexShader = "filtercube.vert.hlsl";
			std::string fragmentShader = (target == IRRADIANCE ? "filtercube_irradiance.frag.hlsl" : "filtercube_radiance.frag.hlsl");

			// Pipeline
			Pipeline* pipeline = new Pipeline({
				.shaders = {
					getAssetPath() + "shaders/" + vertexShader,
					getAssetPath() + "shaders/" + fragmentShader
				},
				.cache = pipelineCache,
				.layout = pipelineLayout->handle,
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
					.cullMode = VK_CULL_MODE_NONE,
					.frontFace = VK_FRONT_FACE_CLOCKWISE,
					.lineWidth = 1.0f
				},
				.multisampleState = {
					.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
				},
				.depthStencilState = {
					.depthTestEnable = VK_FALSE,
					.depthWriteEnable = VK_FALSE,
					.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
				},
				.blending = {
					.attachments = { 
						{.blendEnable = VK_FALSE, .colorWriteMask = 0xF }
					}
				},
				.dynamicState = {
					DynamicState::Scissor,
					DynamicState::Viewport
				},
				.pipelineRenderingInfo = {
					.colorAttachmentCount = 1,
					.pColorAttachmentFormats = &format,
				},
				.enableHotReload = false
			});

			VkRenderingAttachmentInfo colorAttachment = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
				.imageView = offscreenView->handle,
				.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { .color = { 0.0f, 0.0f, 0.0f, 0.0f } }
			};

			VkRenderingInfo renderingInfo = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
				.renderArea = { 0, 0, static_cast<uint32_t>(dim), static_cast<uint32_t>(dim) },
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &colorAttachment,
			};

			// Render cubemap
			const std::vector<glm::mat4> matrices = {
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			};

			CommandBuffer* cb = new CommandBuffer({ .device = *vulkanDevice, .pool = commandPool });

			cb->begin();
			// Initial transition for offscreen image
			cb->insertImageMemoryBarrier({
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.image = offscreen->handle,
				.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
				}, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			// Change image layout for all cubemap faces to transfer destination
			cb->insertImageMemoryBarrier({
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image = cubemap->image,
				.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount =  numMips, .layerCount = 6 }
				}, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

			for (uint32_t m = 0; m < numMips; m++) {
				for (uint32_t f = 0; f < 6; f++) {
					glm::vec2 viewport = glm::vec2(static_cast<float>(dim * std::pow(0.5f, m)), static_cast<float>(dim * std::pow(0.5f, m)));

					cb->insertImageMemoryBarrier({
						.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						.image = offscreen->handle,
						.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
						}, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
					cb->beginRendering(renderingInfo);
					cb->setViewport(0, 0, viewport.x, viewport.y, 0.0f, 1.0f);
					cb->setScissor(0, 0, static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
					
					// Pass parameters for current pass using a push constant block
					switch (target) {
					case IRRADIANCE:
						pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
						cb->updatePushConstant(pipelineLayout, 0, &pushBlockIrradiance);
						break;
					case RADIANCE:
						pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
						pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
						cb->updatePushConstant(pipelineLayout, 0, &pushBlockPrefilterEnv);
						break;
					};

					cb->bindPipeline(pipeline);
					cb->bindDescriptorSets(pipelineLayout, { descriptorSet });
					assetManager->models["crate"]->draw(cb->handle, pipelineLayout->handle, glm::mat4(1.0f), true, true);

					cb->endRendering();

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion = {
						.srcSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = 0,
							.baseArrayLayer = 0,
							.layerCount = 1
						},
						.srcOffset = { 0, 0, 0 },
						.dstSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = m,
							.baseArrayLayer = f,
							.layerCount = 1
						},
						.dstOffset = { 0, 0, 0 },
						.extent = {
							.width = static_cast<uint32_t>(viewport.x),
							.height = static_cast<uint32_t>(viewport.y),
							.depth = 1
}
					};
					vkCmdCopyImage(cb->handle, offscreen->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubemap->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

					cb->insertImageMemoryBarrier({
						.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
						.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						.image = offscreen->handle,
						.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
						}, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
				}
			}

			cb->insertImageMemoryBarrier({
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = cubemap->image,
				.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = numMips, .layerCount = 6 }
				}, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			cb->end();
			cb->oneTimeSubmit(queue);

			cubemap->descriptor.imageView = cubemap->view;
			cubemap->descriptor.sampler = cubemap->sampler;
			cubemap->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			switch (target) {
			case IRRADIANCE:
				skybox.irradianceIndex = assetManager->add("skybox_irradiance", cubemap);
				break;
			case RADIANCE:
				skybox.radianceIndex = assetManager->add("skybox_radiance", cubemap);
				break;
			};

			delete cb;
			delete pipeline;
			delete pipelineLayout;
			delete descriptorPool;
			delete descriptorSetLayout;
			delete descriptorSet;
			delete offscreen;
			delete offscreenView;

			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
		}
	}

#pragma endregion PBR

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
		overlay.text("Angular velocity: %.6f, %.6f", camera.angularVelocity.x, camera.angularVelocity.y);
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