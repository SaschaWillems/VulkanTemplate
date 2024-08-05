/*
 * DXC hLSL Compiler abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>
#include <atlbase.h>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <map>
#include "volk.h"
#include "dxcapi.h"
#include "VulkanContext.h"

class Dxc {
private:
	CComPtr<IDxcLibrary> library{ nullptr };
	CComPtr<IDxcCompiler3> compiler{ nullptr };
	CComPtr<IDxcUtils> utils{ nullptr };

	std::map<std::string, VkShaderStageFlagBits> shaderStages{
		{ ".vert", VK_SHADER_STAGE_VERTEX_BIT },
		{ ".frag", VK_SHADER_STAGE_FRAGMENT_BIT }
	};

	std::map<std::string, LPCWSTR> targetProfiles{
		{ ".vert", L"vs_6_1" },
		{ ".frag", L"ps_6_1" }
	};

	std::string fileExtension(const std::string filename);
public:
	Dxc();
	VkShaderStageFlagBits getShaderStage(const std::string filename);
	VkShaderModule compileShader(const std::string filename);
};

extern Dxc* dxcCompiler;