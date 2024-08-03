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