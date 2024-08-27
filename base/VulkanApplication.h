/*
* Vulkan Application base class
*
* Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <ShellScalingAPI.h>
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
#include "VulkanAndroid.h"
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#elif defined(_DIRECT2DISPLAY)
//
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#endif

#include <iostream>
#include <chrono>
#include <sys/stat.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <string>
#include <array>
#include <numeric>

#include "volk.h"

#include "Keycodes.hpp"

#include "VulkanTools.h"
#include "UIOverlay.h"
#include "Initializers.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "Camera.hpp"

#include "CommandBuffer.hpp"
#include "CommandPool.hpp"

#include "CommandLineParser.hpp"

#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"

#include <SFML/Window.hpp>

struct VulkanFrameObjects
{
	CommandBuffer* commandBuffer;
	VkFence renderCompleteFence;
	VkSemaphore renderCompleteSemaphore;
	VkSemaphore presentCompleteSemaphore;
};

struct ImageAttachment {
	VkImage image = VK_NULL_HANDLE;
	VkImageView view;
	VkDeviceMemory memory;
};

class VulkanApplication
{
private:	
	bool viewUpdated = false;
	uint32_t destWidth;
	uint32_t destHeight;
	bool resizing = false;
	void windowResize();
	void handleMouseMove(int32_t x, int32_t y);
	VkDebugUtilsMessengerEXT debugUtilsMessenger;
	VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
	CommandLineParser commandLineParser;
protected:
	struct MultisampleTarget {
		ImageAttachment color;
		ImageAttachment depth;
	} multisampleTarget;
	ImageAttachment depthStencil;
	uint32_t frameCounter = 0;
	uint32_t lastFPS = 0;
	std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp;
	VkInstance instance; // @todo: abstract
	std::vector<const char*> enabledDeviceExtensions;
	std::vector<const char*> enabledInstanceExtensions;
	void* deviceCreatepNextChain = nullptr;
	VkQueue queue; // Use from device
	VkFormat depthFormat;
	CommandPool* commandPool;
	uint32_t currentBuffer = 0;
	VkPipelineCache pipelineCache;
	SwapChain* swapChain;
	uint32_t frameIndex = 0;
	uint32_t renderAhead = 2;
public: 
	bool prepared = false;
	uint32_t width = 1280;
	uint32_t height = 720;

	vks::UIOverlay* overlay{ nullptr };

	float frameTimer = 1.0f;
	const std::string getAssetPath();

	Device* vulkanDevice;

	struct Settings {
		bool validation = false;
		bool fullscreen = false;
		bool vsync = false;
		VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
	} settings;

	static std::vector<const char*> args;

	float timer = 0.0f;
	float timerSpeed = 0.25f;
	
	bool paused = false;

	float rotationSpeed = 1.0f;
	float zoomSpeed = 1.0f;

	Camera camera;

	glm::vec3 rotation = glm::vec3();
	glm::vec3 cameraPos = glm::vec3();
	glm::vec2 mousePos;

	std::string title = "Vulkan Template";
	std::string name = "VulkanTemplate";
	std::string windowTitle = "Vulkan Template";
	uint32_t apiVersion = VK_API_VERSION_1_0;

	struct {
		glm::vec2 axisLeft = glm::vec2(0.0f);
		glm::vec2 axisRight = glm::vec2(0.0f);
	} gamePadState;

	struct {
		bool left = false;
		bool right = false;
		bool middle = false;
	} mouseButtons;

	sf::WindowBase* window{ nullptr };

	// OS specific 
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// true if application has focused, false if moved to background
	bool focused = false;
	struct TouchPos {
		int32_t x;
		int32_t y;
	} touchPos;
	bool touchDown = false;
	double touchTimer = 0.0;
	int64_t lastTapTime = 0;
	/** @brief Product model and manufacturer of the Android device (via android.Product*) */
	std::string androidProduct;
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	void* view;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	struct xdg_wm_base *shell = nullptr;
	wl_seat *seat = nullptr;
	wl_pointer *pointer = nullptr;
	wl_keyboard *keyboard = nullptr;
	wl_surface *surface = nullptr;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	bool quit = false;
	bool configured = false;

#elif defined(_DIRECT2DISPLAY)
	bool quit = false;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	bool quit = false;
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif

	VulkanApplication();
	virtual ~VulkanApplication();

	// Setup the vulkan instance, enable required extensions and connect to the physical device (GPU)
	bool initVulkan();

#if defined(_WIN32)
	void setupConsole(std::string title);
	void setupDPIAwareness();
	void setupWindow();
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	static int32_t handleAppInput(struct android_app* app, AInputEvent* event);
	static void handleAppCommand(android_app* app, int32_t cmd);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	void* setupWindow(void* view);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	struct xdg_surface *setupWindow();
	void initWaylandConnection();
	void setSize(int width, int height);
	static void registryGlobalCb(void *data, struct wl_registry *registry,
			uint32_t name, const char *interface, uint32_t version);
	void registryGlobal(struct wl_registry *registry, uint32_t name,
			const char *interface, uint32_t version);
	static void registryGlobalRemoveCb(void *data, struct wl_registry *registry,
			uint32_t name);
	static void seatCapabilitiesCb(void *data, wl_seat *seat, uint32_t caps);
	void seatCapabilities(wl_seat *seat, uint32_t caps);
	static void pointerEnterCb(void *data, struct wl_pointer *pointer,
			uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
			wl_fixed_t sy);
	static void pointerLeaveCb(void *data, struct wl_pointer *pointer,
			uint32_t serial, struct wl_surface *surface);
	static void pointerMotionCb(void *data, struct wl_pointer *pointer,
			uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
	void pointerMotion(struct wl_pointer *pointer,
			uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
	static void pointerButtonCb(void *data, struct wl_pointer *wl_pointer,
			uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
	void pointerButton(struct wl_pointer *wl_pointer,
			uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
	static void pointerAxisCb(void *data, struct wl_pointer *wl_pointer,
			uint32_t time, uint32_t axis, wl_fixed_t value);
	void pointerAxis(struct wl_pointer *wl_pointer,
			uint32_t time, uint32_t axis, wl_fixed_t value);
	static void keyboardKeymapCb(void *data, struct wl_keyboard *keyboard,
			uint32_t format, int fd, uint32_t size);
	static void keyboardEnterCb(void *data, struct wl_keyboard *keyboard,
			uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
	static void keyboardLeaveCb(void *data, struct wl_keyboard *keyboard,
			uint32_t serial, struct wl_surface *surface);
	static void keyboardKeyCb(void *data, struct wl_keyboard *keyboard,
			uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
	void keyboardKey(struct wl_keyboard *keyboard,
			uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
	static void keyboardModifiersCb(void *data, struct wl_keyboard *keyboard,
			uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
			uint32_t mods_locked, uint32_t group);

#elif defined(_DIRECT2DISPLAY)
//
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_window_t setupWindow();
	void initxcbConnection();
	void handleEvent(const xcb_generic_event_t *event);
#endif
	virtual VkResult createInstance();

	// Pure virtual render function (override in derived class)
	virtual void render() = 0;
	// Called when view change occurs
	// Can be overriden in derived class to e.g. update uniform buffers 
	// Containing view dependant matrices
	virtual void viewChanged();
	/** @brief (Virtual) Called after a key was pressed, can be used to do custom key handling */
	virtual void keyPressed(uint32_t);
	/** @brief (Virtual) Called after th mouse cursor moved and before internal events (like camera rotation) is handled */
	virtual void mouseMoved(double x, double y, bool &handled);
	// Called when the window has been resized
	// Can be overriden in derived class to recreate or rebuild resources attached to the frame buffer / swapchain
	virtual void windowResized();

	// Setup default depth and stencil views
	virtual void setupDepthStencil();
	// Create framebuffers for all requested swap chain images
	// Can be overriden in derived class to setup a custom framebuffer (e.g. for MSAA)
	virtual void setupImages();

	// Connect and prepare the swap chain
	void initSwapchain();

	// Prepare commonly used Vulkan functions
	virtual void prepare();

	// Start the main render loop
	void renderLoop();

	// Render one frame of a render loop on platforms that sync rendering
	void renderFrame();

	void updateOverlay(uint32_t frameIndex);

	void nextFrame();

	/** @brief (Virtual) Called when the UI overlay is updating, can be used to add custom elements to the overlay */
	virtual void OnUpdateOverlay(vks::UIOverlay& overlay);

	// @todo: Functions for reworked proper sync and per-frame resources
	void prepareFrame(VulkanFrameObjects& frame);
	void submitFrame(VulkanFrameObjects& frame);
	uint32_t getFrameCount();
	uint32_t getCurrentFrameIndex();

	void createBaseFrameObjects(VulkanFrameObjects& frame);
	void destroyBaseFrameObjects(VulkanFrameObjects& frame);
};