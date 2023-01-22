/*
 * Android utilities
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#ifndef VULKANANDROID_H
#define VULKANANDROID_H

#pragma once

#ifndef VULKANANDROID_HPP
#define VULKANANDROID_HPP

#include "volk.h"

#if defined(__ANDROID__)

#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/configuration.h>
#include <memory>
#include <string>

 // Missing from the NDK
namespace std
{
	template<typename T, typename... Args>
	std::unique_ptr<T> make_unique(Args&&... args)
	{
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}
}

// Global reference to android application object
extern android_app* androidApp;

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "vulkanExample", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "vulkanExample", __VA_ARGS__))
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, "vulkanExample", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "vulkanExample", __VA_ARGS__))

namespace vks
{
	namespace android
	{
		/* @brief Touch control thresholds from Android NDK samples */
		const int32_t DOUBLE_TAP_TIMEOUT = 300 * 1000000;
		const int32_t TAP_TIMEOUT = 180 * 1000000;
		const int32_t DOUBLE_TAP_SLOP = 100;
		const int32_t TAP_SLOP = 8;

		/** @brief Density of the device screen (in DPI) */
		extern int32_t screenDensity;

		bool loadVulkanLibrary();
		void freeVulkanLibrary();
		void getDeviceConfig();
		void showAlert(const char* message);
	}
}

#endif

#endif // VULKANANDROID_HPP


#endif // VULKANANDROID_H
