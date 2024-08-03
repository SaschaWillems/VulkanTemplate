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

void AssetManager::add(const std::string name, vkglTF::Model* model) {
	models[name] = model;
}

void AssetManager::add(const std::string name, vks::Texture2D* texture)
{
	textures[name] = texture;
}

void AssetManager::add(const std::string name, vks::TextureCubeMap* cubemap)
{
	textures[name] = cubemap;
}
