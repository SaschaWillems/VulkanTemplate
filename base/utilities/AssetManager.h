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
	std::unordered_map<std::string, vks::Texture*> textures{};
	~AssetManager();
	void add(const std::string name, vkglTF::Model* model);
	void add(const std::string name, vks::Texture2D* texture);
	void add(const std::string name, vks::TextureCubeMap* cubemap);
};