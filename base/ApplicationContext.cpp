/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

// @todo: do not use, better inject

#include "ApplicationContext.h"

ApplicationContext applicationContext{ };

AssetManager* ApplicationContext::assetManager{ nullptr };
