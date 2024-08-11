/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <unordered_map>
#include <string>
#include "glTF.h"
#include "Texture.hpp"

class AssetManager {
public:
	std::unordered_map<std::string, vkglTF::Model*> models{};
	std::vector<vks::Texture*> textures{};
	~AssetManager();
	vkglTF::Model* add(const std::string name, vkglTF::Model* model);
	uint32_t add(const std::string name, vks::Texture2D* texture);
	uint32_t add(const std::string name, vks::TextureCubeMap* cubemap);
};