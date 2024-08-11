/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "AssetManager.h"
#include "ActorManager.h"

#pragma once

class ApplicationContext {
public:
	static AssetManager* assetManager;
};

extern ApplicationContext applicationContext;