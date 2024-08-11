/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "AssetManager.h"

AssetManager::~AssetManager() {
	for (auto& it : models) {
		delete it.second;
	}
}

vkglTF::Model* AssetManager::add(const std::string name, vkglTF::Model* model) {
	models[name] = model;
	return model;
}

uint32_t AssetManager::add(const std::string name, vks::Texture2D* texture)
{
	textures.push_back(texture);
	return static_cast<uint32_t>(textures.size() - 1);
}

uint32_t AssetManager::add(const std::string name, vks::TextureCubeMap* cubemap)
{
	textures.push_back(cubemap);
	return static_cast<uint32_t>(textures.size() - 1);
}
