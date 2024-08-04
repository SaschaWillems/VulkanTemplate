/*
 * DXC HLSL Compiler abstraction class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "dxc.hpp"

Dxc* dxcCompiler{ nullptr };

std::string Dxc::fileExtension(const std::string filename) {
	std::string fname = filename;
	if (filename.find(".hlsl") != std::string::npos) {
		fname = fname.substr(0, fname.length() - 5);
	}
	std::string fext = fname.substr(filename.find('.'));
	return fext;
}

Dxc::Dxc()
{
	HRESULT hres;

	// Initialize DXC library
	hres = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
	if (FAILED(hres)) {
		throw std::runtime_error("Could not init DXC Library");
	}

	// Initialize DXC compiler
	hres = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
	if (FAILED(hres)) {
		throw std::runtime_error("Could not init DXC Compiler");
	}

	// Initialize DXC utility
	hres = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
	if (FAILED(hres)) {
		throw std::runtime_error("Could not init DXC Utiliy");
	}
}

VkShaderStageFlagBits Dxc::getShaderStage(const std::string filename) {
	std::string ext = fileExtension(filename);
	assert(shaderStages.find(ext) != shaderStages.end());
	return shaderStages[ext];
}

VkShaderModule Dxc::compileShader(const std::string filename) {
	HRESULT hres;

	// @todo
	std::wstring stemp = std::wstring(filename.begin(), filename.end());
	LPCWSTR shaderfile = stemp.c_str();

	// Load the HLSL text shader from disk
	uint32_t codePage = DXC_CP_ACP;
	CComPtr<IDxcBlobEncoding> sourceBlob;
	hres = utils->LoadFile(shaderfile, &codePage, &sourceBlob);
	if (FAILED(hres)) {
		throw std::runtime_error("Could not load shader file");
	}

	// Select target profile based on shader file extension
	std::string extension = fileExtension(filename);
	assert(targetProfiles.find(extension) != targetProfiles.end());
	LPCWSTR targetProfile = targetProfiles[extension];

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
	vkCreateShaderModule(VulkanContext::device->logicalDevice, &shaderModuleCI, nullptr, &shaderModule);

	return shaderModule;
};
