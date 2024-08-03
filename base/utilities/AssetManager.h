/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <unordered_map>
#include <string>
#include "glTF.h"

class AssetManager {
public:
	std::unordered_map<std::string, vkglTF::Model*> models{};
	~AssetManager();
	void add(const std::string name, vkglTF::Model* model);
};