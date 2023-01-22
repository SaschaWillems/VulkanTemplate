/*
 * Android utilities
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "Android.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <android/native_window_jni.h>

android_app* androidApp;

int32_t vks::android::screenDensity;

void* libVulkan;

namespace vks
{
	namespace android
	{
		// Dynamically load Vulkan library and base function pointers
		bool loadVulkanLibrary()
		{
			__android_log_print(ANDROID_LOG_INFO, "vulkanandroid", "Loading libvulkan.so...\n");

			// Load vulkan library
			libVulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
			if (!libVulkan)
			{
				__android_log_print(ANDROID_LOG_INFO, "vulkanandroid", "Could not load vulkan library : %s!\n", dlerror());
				return false;
			}

			return true;
		}

		void freeVulkanLibrary()
		{
			dlclose(libVulkan);
		}

		void getDeviceConfig()
		{
			// Screen density
			AConfiguration* config = AConfiguration_new();
			AConfiguration_fromAssetManager(config, androidApp->activity->assetManager);
			vks::android::screenDensity = AConfiguration_getDensity(config);
			AConfiguration_delete(config);
		}

		// Displays a native alert dialog using JNI
		void showAlert(const char* message) {
			JNIEnv* jni;
			androidApp->activity->vm->AttachCurrentThread(&jni, NULL);

			jstring jmessage = jni->NewStringUTF(message);

			jclass clazz = jni->GetObjectClass(androidApp->activity->clazz);
			// Signature has to match java implementation (arguments)
			jmethodID methodID = jni->GetMethodID(clazz, "showAlert", "(Ljava/lang/String;)V");
			jni->CallVoidMethod(androidApp->activity->clazz, methodID, jmessage);
			jni->DeleteLocalRef(jmessage);

			androidApp->activity->vm->DetachCurrentThread();
			return;
		}
	}
}

#endif
