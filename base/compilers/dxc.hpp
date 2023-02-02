/*
 * DXC hLSL Compiler abstraction class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>
#include <atlbase.h>
#include <stdexcept>
#include <vector>
#include <iostream>
#include "volk.h"
#include "dxcapi.h"

class Dxc {
public:
	VkShaderStageFlagBits getShaderStage(const std::string filename) {
		size_t extpos = filename.find('.');
		size_t extend = filename.find('.', extpos + 1);
		assert(extpos != std::string::npos);
		std::string ext = filename.substr(extpos + 1, extend - extpos - 1);
		if (ext == "vert") {
			return VK_SHADER_STAGE_VERTEX_BIT;
		}
		if (ext == "frag") {
			return VK_SHADER_STAGE_FRAGMENT_BIT; 
		}
		return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	}

	VkShaderModule compileShader(const std::string filename, VkDevice device) {
		HRESULT hres;

		// @todo
		std::wstring stemp = std::wstring(filename.begin(), filename.end());
		LPCWSTR shaderfile = stemp.c_str();

		// Initialize DXC library
		CComPtr<IDxcLibrary> library;
		hres = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
		if (FAILED(hres)) {
			throw std::runtime_error("Could not init DXC Library");
		}

		// Initialize DXC compiler
		CComPtr<IDxcCompiler3> compiler;
		hres = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		if (FAILED(hres)) {
			throw std::runtime_error("Could not init DXC Compiler");
		}

		// Initialize DXC utility
		CComPtr<IDxcUtils> utils;
		hres = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		if (FAILED(hres)) {
			throw std::runtime_error("Could not init DXC Utiliy");
		}

		// Load the HLSL text shader from disk
		uint32_t codePage = DXC_CP_ACP;
		CComPtr<IDxcBlobEncoding> sourceBlob;
		hres = utils->LoadFile(shaderfile, &codePage, &sourceBlob);
		if (FAILED(hres)) {
			throw std::runtime_error("Could not load shader file");
		}

		// Select target profile based on shader file extension
		LPCWSTR targetProfile{};
		size_t idx = filename.rfind('.');
		if (idx != std::string::npos) {
			std::string extension = filename.substr(idx + 1);
			if (extension == "vert") {
				targetProfile = L"vs_6_1";
			}
			if (extension == "frag") {
				targetProfile = L"ps_6_1";
			}
			// Mapping for other file types go here (cs_x_y, lib_x_y, etc.)
		}

		// Configure the compiler arguments for compiling the HLSL shader to SPIR-V
		std::vector<LPCWSTR> arguments = {
			// (Optional) name of the shader file to be displayed e.g. in an error message
			shaderfile,
			// Shader main entry point
			L"-E", L"main",
			// Shader target profile
			L"-T", targetProfile,
			// Compile to SPIRV
			L"-spirv"
		};

		// Compile shader
		DxcBuffer buffer{};
		buffer.Encoding = DXC_CP_ACP;
		buffer.Ptr = sourceBlob->GetBufferPointer();
		buffer.Size = sourceBlob->GetBufferSize();

		CComPtr<IDxcResult> result{ nullptr };
		hres = compiler->Compile(
			&buffer,
			arguments.data(),
			(uint32_t)arguments.size(),
			nullptr,
			IID_PPV_ARGS(&result));

		if (SUCCEEDED(hres)) {
			result->GetStatus(&hres);
		}

		// Output error if compilation failed
		if (FAILED(hres) && (result)) {
			CComPtr<IDxcBlobEncoding> errorBlob;
			hres = result->GetErrorBuffer(&errorBlob);
			if (SUCCEEDED(hres) && errorBlob) {
				std::cerr << "Shader compilation failed :\n\n" << (const char*)errorBlob->GetBufferPointer();
				throw "Compilation failed";
			}
		}

		// Get compilation result
		CComPtr<IDxcBlob> code;
		result->GetResult(&code);

		// Create a Vulkan shader module from the compilation result
		VkShaderModuleCreateInfo shaderModuleCI{};
		shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderModuleCI.codeSize = code->GetBufferSize();
		shaderModuleCI.pCode = (uint32_t*)code->GetBufferPointer();
		VkShaderModule shaderModule;
		vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule);

		return shaderModule;
	};

};