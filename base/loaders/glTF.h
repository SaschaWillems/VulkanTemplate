/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <iostream>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "Device.hpp"
#include "Pipeline.hpp"
#include "Buffer.hpp"
#include "VulkanContext.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/quaternion.hpp>

 // ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO) 
#undef ERROR
#pragma message ("ERROR constant already defined, undefining")
#endif

#define TINYGLTF_NO_STB_IMAGE_WRITE

#if defined(__ANDROID__)
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif

#include "tiny_gltf.h"

// Changing this value here also requires changing it in the vertex shader
#define MAX_NUM_JOINTS 128u

namespace vkglTF
{
	// @todo: no fixed struct, make it dynamic (buffer doesn't care anyway)
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv0;
		glm::vec4 joint0;
		glm::vec4 weight0;
		glm::vec4 color;
	};

	const PipelineVertexInput vertexInput = {
		.bindings = {
			{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
		},
		.attributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) },
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv0) },
			{ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) },
			{ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) },
			{ 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
		}
	};

	// @todo: add additional material parameters and image indices
	struct PushConstBlock {
		glm::mat4 mat;
		uint32_t textureIndex;
	};

	struct Node;

	struct BoundingBox {
		glm::vec3 min;
		glm::vec3 max;
		bool valid = false;
		BoundingBox();
		BoundingBox(glm::vec3 min, glm::vec3 max);
		BoundingBox getAABB(glm::mat4 m);
	};

	struct TextureSampler {
		VkFilter magFilter;
		VkFilter minFilter;
		VkSamplerAddressMode addressModeU;
		VkSamplerAddressMode addressModeV;
		VkSamplerAddressMode addressModeW;
	};

	struct Texture {
		uint32_t assetIndex{ 0 };
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;
		void destroy();
		void fromglTfImage(tinygltf::Image& gltfimage, TextureSampler textureSampler);
	};

	struct Material {
		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		glm::vec4 emissiveFactor = glm::vec4(0.0f);
		vkglTF::Texture* baseColorTexture;
		vkglTF::Texture* metallicRoughnessTexture;
		vkglTF::Texture* normalTexture;
		vkglTF::Texture* occlusionTexture;
		vkglTF::Texture* emissiveTexture;
		bool doubleSided = false;
		struct TexCoordSets {
			uint8_t baseColor = 0;
			uint8_t metallicRoughness = 0;
			uint8_t specularGlossiness = 0;
			uint8_t normal = 0;
			uint8_t occlusion = 0;
			uint8_t emissive = 0;
		} texCoordSets;
		struct Extension {
			vkglTF::Texture* specularGlossinessTexture;
			vkglTF::Texture* diffuseTexture;
			glm::vec4 diffuseFactor = glm::vec4(1.0f);
			glm::vec3 specularFactor = glm::vec3(0.0f);
		} extension;
		struct PbrWorkflows {
			bool metallicRoughness = true;
			bool specularGlossiness = false;
		} pbrWorkflows;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		int index = 0;
		bool unlit = false;
		float emissiveStrength = 1.0f;
	};

	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t vertexCount;
		Material& material;
		bool hasIndices;
		BoundingBox bb;
		Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, Material& material);
		void setBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Mesh {
		std::vector<Primitive*> primitives;
		BoundingBox bb;
		BoundingBox aabb;
		// @todo: global ssbo for joint matrices
		//struct UniformBlock {
		//	glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
		//	float jointcount{ 0 };
		//} uniformBlock;
		Mesh(glm::mat4 matrix);
		~Mesh();
		void setBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Skin {
		std::string name;
		Node* skeletonRoot = nullptr;
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
	};

	struct Node {
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};
		BoundingBox bvh;
		BoundingBox aabb;
		glm::mat4 localMatrix();
		glm::mat4 getMatrix();
		void update();
		~Node();
	};

	struct AnimationChannel {
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType path;
		Node* node;
		uint32_t samplerIndex;
	};

	struct AnimationSampler {
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType interpolation;
		std::vector<float> inputs;
		std::vector<glm::vec4> outputsVec4;
	};

	struct Animation {
		std::string name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float start = std::numeric_limits<float>::max();
		float end = std::numeric_limits<float>::min();
	};

	struct ModelCreateInfo {
		const std::string filename;
		float scale{ 1.0f };
		bool enableHotReload{ false };
	};

	class Model {
	private:
		struct LoaderInfo {
			uint32_t* indexBuffer;
			Vertex* vertexBuffer;
			size_t indexPos = 0;
			size_t vertexPos = 0;
		};
		void freeResources();
		void loadNode(vkglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale);
		void getNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount);
		void loadSkins(tinygltf::Model& gltfModel);
		void loadTextures(tinygltf::Model& gltfModel);
		VkSamplerAddressMode getVkWrapMode(int32_t wrapMode);
		VkFilter getVkFilterMode(int32_t filterMode);
		void loadTextureSamplers(tinygltf::Model& gltfModel);
		void loadMaterials(tinygltf::Model& gltfModel);
		void loadAnimations(tinygltf::Model& gltfModel);
		void calculateBoundingBox(Node* node, Node* parent);
	public:
		// Store the createInfo for hot reload
		ModelCreateInfo* initialCreateInfo{ nullptr };

		Buffer* vertices;
		Buffer* indices;

		glm::mat4 aabb;

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;

		std::vector<Skin*> skins;

		std::vector<Texture> textures;
		std::vector<TextureSampler> textureSamplers;
		std::vector<Material> materials;
		std::vector<Animation> animations;
		std::vector<std::string> extensions;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
		} dimensions;

		bool wantsReload = false;

		Model(ModelCreateInfo createInfo);
		~Model();

		void drawNode(Node* node, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, bool skipMaterials = false);
		void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, bool skipMaterials = false);
		void getSceneDimensions();
		void updateAnimation(uint32_t index, float time);
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);
	};
}
