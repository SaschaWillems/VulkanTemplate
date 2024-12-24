/*
 * Vulkan Application base class
 *
 * Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "VulkanApplication.h"

#undef VMA_DEDICATED_ALLOCATION 
#undef VMA_BIND_MEMORY2
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

std::vector<const char*> VulkanApplication::args;

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	// Select prefix depending on flags passed to the callback
	std::string prefix("");

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
		prefix = "VERBOSE: ";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		prefix = "INFO: ";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		prefix = "WARNING: ";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		prefix = "ERROR: ";
	}

	// Display message to default output (console/logcat)
	std::stringstream debugMessage;
	debugMessage << pCallbackData->pMessage;

#if defined(__ANDROID__)
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		LOGE("%s", debugMessage.str().c_str());
	}
	else {
		LOGD("%s", debugMessage.str().c_str());
	}
#else
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		std::cerr << debugMessage.str() << "\n\n";
	}
	else {
		std::cout << debugMessage.str() << "\n\n";
	}
	fflush(stdout);
#endif


	// The return value of this callback controls wether the Vulkan call that caused the validation message will be aborted or not
	// We return VK_FALSE as we DON'T want Vulkan calls that cause a validation message to abort
	// If you instead want to have calls abort, pass in VK_TRUE and the function will return VK_ERROR_VALIDATION_FAILED_EXT 
	return VK_FALSE;
}

VkResult VulkanApplication::createInstance()
{
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.apiVersion = apiVersion;

	std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Enable surface extensions depending on os
#if defined(_WIN32)
	instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
	instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
	instanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	instanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

	if (enabledInstanceExtensions.size() > 0) {
		for (auto enabledExtension : enabledInstanceExtensions) {
			instanceExtensions.push_back(enabledExtension);
		}
	}

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pNext = NULL;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	if (instanceExtensions.size() > 0)
	{
		if (settings.validation)
		{
			instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}
	if (settings.validation)
	{
		// The VK_LAYER_KHRONOS_validation contains all current validation functionality.
		// Note that on Android this layer requires at least NDK r20 
		const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
		// Check if this layer is available at instance level
		uint32_t instanceLayerCount;
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
		std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());
		bool validationLayerPresent = false;
		for (VkLayerProperties layer : instanceLayerProperties) {
			if (strcmp(layer.layerName, validationLayerName) == 0) {
				validationLayerPresent = true;
				break;
			}
		}
		if (validationLayerPresent) {
			instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
			instanceCreateInfo.enabledLayerCount = 1;
		} else {
			std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled";
		}
	}
	return vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
}

#if !(defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
// iOS & macOS: VulkanApplication::getAssetPath() implemented externally to allow access to Objective-C components
const std::string VulkanApplication::getAssetPath()
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	return "";
#elif defined(DATA_DIR)
	return DATA_DIR;
#else
	return "./../data/";
#endif
}
#endif

void VulkanApplication::prepare()
{
	VulkanContext::graphicsQueue = queue;
	VulkanContext::device = vulkanDevice;
	// We try to get a transfer queue for background uploads
	if (vulkanDevice->hasDedicatedTransferQueue) {
		VulkanContext::copyQueue = vulkanDevice->getQueue(QueueType::Transfer);
	}
	else {
		VulkanContext::copyQueue = queue;
	}

	// VMA
	VmaVulkanFunctions vmaVulkanFns{};
	vmaVulkanFns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vmaVulkanFns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo vmaCI{};
	vmaCI.physicalDevice = vulkanDevice->physicalDevice;
	vmaCI.device = vulkanDevice->logicalDevice;
	vmaCI.instance = instance;
	vmaCI.vulkanApiVersion = apiVersion;
	vmaCI.pVulkanFunctions = &vmaVulkanFns;
	if (vmaCreateAllocator(&vmaCI, &VulkanContext::vmaAllocator) != VK_SUCCESS) {
		throw std::runtime_error("Could not init Vulkan Memory Allocator");
	}

	initSwapchain();
	// Default command Pool
	commandPool = new CommandPool({
		.name = "Shared application command pool",
		.queueFamilyIndex = swapChain->queueNodeIndex, // @todo: from device
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	});
	swapChain->create(&width, &height, settings.vsync);
	setupDepthStencil();
	setupImages();
	// Default pipeline cache
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(*vulkanDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
	// ImGUI based overlay
	overlay = new vks::UIOverlay({
		.device = *vulkanDevice,
		.queue = queue,
		.pipelineCache = pipelineCache,
		.colorFormat = swapChain->colorFormat,
		.depthFormat = depthFormat,
		.rasterizationSamples = settings.sampleCount,
		.fontFileName = "Roboto-Medium.ttf",
		.assetPath = getAssetPath(),
		.scale = 1.0f,
		.frameCount = getFrameCount(),
		});
}

void VulkanApplication::renderFrame()
{
	auto tStart = std::chrono::high_resolution_clock::now();
	if (viewUpdated)
	{
		viewUpdated = false;
		viewChanged();
	}

	render();
	frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	frameTimer = (float)tDiff / 1000.0f;
	camera.mouse.buttons.left = mouseButtons.left;
	camera.mouse.cursorPos = mousePos;
	camera.mouse.cursorPosNDC = mousePos / glm::vec2(float(width), float(height));
	if (GetKeyState(VK_SHIFT) & 0x8000) {
		camera.update(frameTimer * 2.5f);
	} else {
		camera.update(frameTimer);
	}
	if (camera.moving())
	{
		viewUpdated = true;
	}
	// Convert to clamped timer value
	if (!paused)
	{
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
	}
	float fpsTimer = (float)(std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count());
	if (fpsTimer > 1000.0f)
	{
		lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
		frameCounter = 0;
		lastTimestamp = tEnd;
	}
	// TODO: Cap UI overlay update rates
	//updateOverlay();
}

void VulkanApplication::renderLoop()
{
	destWidth = width;
	destHeight = height;
	lastTimestamp = std::chrono::high_resolution_clock::now();
#if defined(_WIN32)
	while (window->isOpen()) {
		sf::Event event;
		while (window->pollEvent(event))
		{
			if (event.type == sf::Event::Closed) {
				window->close();
				return;
			}
			if (event.type == sf::Event::KeyPressed) {
				if (event.key.code == sf::Keyboard::F1) {
					overlay->visible = !overlay->visible;
				}
				keyPressed(event.key.code);
			}
			if (event.type == sf::Event::MouseButtonPressed) {
				switch (event.mouseButton.button) {
					case sf::Mouse::Left:
						camera.mouse.buttons.left = true;
						break;
					case sf::Mouse::Right:
						camera.mouse.buttons.right = true;
						break;
				}
			}
			if (event.type == sf::Event::MouseButtonReleased) {
				switch (event.mouseButton.button) {
				case sf::Mouse::Left:
					camera.mouse.buttons.left = false;
					camera.mouse.dragging = false;
					break;
				case sf::Mouse::Right:
					camera.mouse.buttons.right = false;
					break;
				}
			}
		}
		auto mPos = sf::Mouse::getPosition(*window);
		mousePos = glm::vec2((float)mPos.x, (float)mPos.y);
		// @todo: move to camera func
		if (camera.mouse.buttons.left && !camera.mouse.dragging) {
			camera.mouse.dragCursorPos = mousePos;
			camera.mouse.dragging = true;
		};

		if (prepared) {
			// @todo: minimized
			nextFrame();
		}
	}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	while (1)
	{
		int ident;
		int events;
		struct android_poll_source* source;
		bool destroy = false;

		focused = true;

		while ((ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0)
		{
			if (source != NULL)
			{
				source->process(androidApp, source);
			}
			if (androidApp->destroyRequested != 0)
			{
				LOGD("Android app destroy requested");
				destroy = true;
				break;
			}
		}

		// App destruction requested
		// Exit loop, example will be destroyed in application main
		if (destroy)
		{
			ANativeActivity_finish(androidApp->activity);
			break;
		}

		// Render frame
		if (prepared)
		{
			auto tStart = std::chrono::high_resolution_clock::now();
			render();
			frameCounter++;
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			frameTimer = tDiff / 1000.0f;
			camera.update(frameTimer);
			// Convert to clamped timer value
			if (!paused)
			{
				timer += timerSpeed * frameTimer;
				if (timer > 1.0)
				{
					timer -= 1.0f;
				}
			}
			float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
			if (fpsTimer > 1000.0f)
			{
				lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
				frameCounter = 0;
				lastTimestamp = tEnd;
			}

			// TODO: Cap UI overlay update rates/only issue when update requested
			updateOverlay();

			bool updateView = false;

			// Check touch state (for movement)
			if (touchDown) {
				touchTimer += frameTimer;
			}
			if (touchTimer >= 1.0) {
				camera.keys.up = true;
				viewChanged();
			}

			// Check gamepad state
			const float deadZone = 0.0015f;
			// todo : check if gamepad is present
			// todo : time based and relative axis positions
			if (camera.type != Camera::CameraType::firstperson)
			{
				// Rotate
				if (std::abs(gamePadState.axisLeft.x) > deadZone)
				{
					rotation.y += gamePadState.axisLeft.x * 0.5f * rotationSpeed;
					camera.rotate(glm::vec3(0.0f, gamePadState.axisLeft.x * 0.5f, 0.0f));
					updateView = true;
				}
				if (std::abs(gamePadState.axisLeft.y) > deadZone)
				{
					rotation.x -= gamePadState.axisLeft.y * 0.5f * rotationSpeed;
					camera.rotate(glm::vec3(gamePadState.axisLeft.y * 0.5f, 0.0f, 0.0f));
					updateView = true;
				}
				if (std::abs(gamePadState.axisRight.y) > deadZone)
				{
					camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * zoomSpeed));
					updateView = true;
				}
				if (updateView)
				{
					viewChanged();
				}
			}
			else
			{
				updateView = camera.updatePad(gamePadState.axisLeft, gamePadState.axisRight, frameTimer);
				if (updateView)
				{
					viewChanged();
				}
			}
		}
	}
#elif defined(_DIRECT2DISPLAY)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}

		while (!configured)
			wl_display_dispatch(display);
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		wl_display_read_events(display);
		wl_display_dispatch_pending(display);

		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_flush(connection);
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(connection)))
		{
			handleEvent(event);
			free(event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#endif
	// Flush device to make sure all resources can be freed
	if (*vulkanDevice != VK_NULL_HANDLE) {
		vulkanDevice->waitIdle();
	}
}

void VulkanApplication::updateOverlay(uint32_t frameIndex)
{
	if (!overlay->visible)
		return;

	ImGuiIO& io = ImGui::GetIO();

	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DeltaTime = frameTimer;

	io.MousePos = ImVec2(mousePos.x, mousePos.y);
	io.MouseDown[0] = mouseButtons.left;
	io.MouseDown[1] = mouseButtons.right;

	//io.MouseDrawCursor = true;

	ImGui::NewFrame();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(10, 10));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Application", nullptr);
	ImGui::TextUnformatted(title.c_str());
	ImGui::TextUnformatted(vulkanDevice->properties.deviceName);
	ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f * overlay->scale));
#endif
	ImGui::PushItemWidth(110.0f * overlay->scale);
	OnUpdateOverlay(*overlay);
	ImGui::PopItemWidth();
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PopStyleVar();
#endif

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::Render();

	//Check if the overlay's index and vertex buffers needs to be updated (recreated), e.g. because new elements are visible and indices or vertices require additional buffer space
   //@todo: remove?
	if (overlay->bufferUpdateRequired(frameIndex)) {
		// Ensure all command buffers have finished execution, so we don't change vertex and/or index buffers still in use
		// @todo: wait for fences instead?
		vkQueueWaitIdle(queue);
		overlay->allocateBuffers(frameIndex);
	}
	// @todo: cap update rate
	overlay->updateBuffers(frameIndex);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	if (mouseButtons.left) {
		mouseButtons.left = false;
	}
#endif
}

VulkanApplication::VulkanApplication()
{
#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Check for a valid asset path
	struct stat info;
	if (stat(getAssetPath().c_str(), &info) != 0)
	{
#if defined(_WIN32)
		std::string msg = "Could not locate asset path in \"" + getAssetPath() + "\" !";
		MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_ICONERROR);
#else
		std::cerr << "Error: Could not find asset path in " << getAssetPath() << std::endl;
#endif
		exit(-1);
	}
#endif

	// Command line arguments
	commandLineParser.add("help", { "--help" }, 0, "Show help");
	commandLineParser.add("validation", { "-v", "--validation" }, 0, "Enable validation layers");
	commandLineParser.add("vsync", { "-vs", "--vsync" }, 0, "Enable V-Sync");
	commandLineParser.add("fullscreen", { "-f", "--fullscreen" }, 0, "Start in fullscreen mode");
	commandLineParser.add("width", { "-w", "--width" }, 1, "Set window width");
	commandLineParser.add("height", { "-h", "--height" }, 1, "Set window height");
	commandLineParser.add("gpuselection", { "-g", "--gpu" }, 1, "Select GPU to run on");
	commandLineParser.add("gpulist", { "-gl", "--listgpus" }, 0, "Display a list of available Vulkan devices");

	commandLineParser.parse(args);
	if (commandLineParser.isSet("help")) {
#if defined(_WIN32)
		setupConsole(windowTitle);
#endif
		commandLineParser.printHelp();
		std::cin.get();
		exit(0);
	}
	if (commandLineParser.isSet("validation")) {
		settings.validation = true;
	}
	if (commandLineParser.isSet("vsync")) {
		settings.vsync = true;
	}
	if (commandLineParser.isSet("height")) {
		height = commandLineParser.getValueAsInt("height", width);
	}
	if (commandLineParser.isSet("width")) {
		width = commandLineParser.getValueAsInt("width", width);
	}
	if (commandLineParser.isSet("fullscreen")) {
		settings.fullscreen = true;
	}
	
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Vulkan library is loaded dynamically on Android
	bool libLoaded = vks::android::loadVulkanLibrary();
	assert(libLoaded);
#elif defined(_DIRECT2DISPLAY)

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	initWaylandConnection();
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	initxcbConnection();
#endif

#if defined(_WIN32)
	// Enable console if validation is active, the debug message callback will output to it
	//if (this->settings.validation)
	{
		setupConsole("Vulkan validation output");
	}
	setupDPIAwareness();
#endif
}

VulkanApplication::~VulkanApplication()
{
	delete swapChain;

	vkDestroyImageView(*vulkanDevice, depthStencil.view, nullptr);
	vkDestroyImage(*vulkanDevice, depthStencil.image, nullptr);
	vkFreeMemory(*vulkanDevice, depthStencil.memory, nullptr);

	vkDestroyPipelineCache(*vulkanDevice, pipelineCache, nullptr);

	if (settings.sampleCount > VK_SAMPLE_COUNT_1_BIT) {
		vkDestroyImage(*vulkanDevice, multisampleTarget.color.image, nullptr);
		vkDestroyImageView(*vulkanDevice, multisampleTarget.color.view, nullptr);
		vkFreeMemory(*vulkanDevice, multisampleTarget.color.memory, nullptr);
		vkDestroyImage(*vulkanDevice, multisampleTarget.depth.image, nullptr);
		vkDestroyImageView(*vulkanDevice, multisampleTarget.depth.view, nullptr);
		vkFreeMemory(*vulkanDevice, multisampleTarget.depth.memory, nullptr);
	}

	// @todo: deletion queue

	if (settings.validation)
	{
		if (debugUtilsMessenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
		}
	}
	delete overlay;
	delete commandPool;
	
	vmaDestroyAllocator(VulkanContext::vmaAllocator);
	
	delete vulkanDevice;
	vkDestroyInstance(instance, nullptr);

#if defined(_DIRECT2DISPLAY)

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	if (keyboard)
		wl_keyboard_destroy(keyboard);
	if (pointer)
		wl_pointer_destroy(pointer);
	wl_seat_destroy(seat);
	xdg_wm_base_destroy(shell);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	// todo : android cleanup (if required)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#endif
}

bool VulkanApplication::initVulkan()
{
	VkResult err;

	err = volkInitialize();
	assert(err == VK_SUCCESS);

	// Vulkan instance
	err = createInstance();
	if (err) {
		vks::tools::exitFatal("Could not create Vulkan instance : \n" + vks::tools::errorString(err), err);
		return false;
	}

	volkLoadInstance(instance);

	// Create a debug callback to output validation messages to the console if validation is enabled
	if (settings.validation)
	{
		VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
			.pfnUserCallback = debugUtilsMessengerCallback,
		};
		VkResult result = vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCI, nullptr, &debugUtilsMessenger);
		assert(result == VK_SUCCESS);
	}

	// Physical device
	uint32_t gpuCount = 0;
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
	assert(gpuCount > 0);
	std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
	if (err) {
		vks::tools::exitFatal("Could not enumerate physical devices : \n" + vks::tools::errorString(err), err);
		return false;
	}

	// GPU selection

	// Select physical device to be used for the Vulkan example
	// Defaults to the first device unless specified by command line
	uint32_t selectedDevice = 0;

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)	
	// GPU selection via command line argument
	for (size_t i = 0; i < args.size(); i++)
	{
		// Select GPU
		if ((args[i] == std::string("-g")) || (args[i] == std::string("-gpu")))
		{
			char* endptr;
			uint32_t index = strtol(args[i + 1], &endptr, 10);
			if (endptr != args[i + 1]) 
			{ 
				if (index > gpuCount - 1)
				{
					std::cerr << "Selected device index " << index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << std::endl;
				} 
				else
				{
					std::cout << "Selected Vulkan device " << index << std::endl;
					selectedDevice = index;
				}
			};
			break;
		}
		// List available GPUs
		if (args[i] == std::string("-listgpus"))
		{
			uint32_t gpuCount = 0;
			VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
			if (gpuCount == 0) 
			{
				std::cerr << "No Vulkan devices found!" << std::endl;
			}
			else 
			{
				// Enumerate devices
				std::cout << "Available Vulkan devices" << std::endl;
				std::vector<VkPhysicalDevice> devices(gpuCount);
				VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, devices.data()));
				for (uint32_t i = 0; i < gpuCount; i++) {
					VkPhysicalDeviceProperties deviceProperties;
					vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
					std::cout << "Device [" << i << "] : " << deviceProperties.deviceName << std::endl;
					std::cout << " Type: " << vks::tools::physicalDeviceTypeString(deviceProperties.deviceType) << std::endl;
					std::cout << " API: " << (deviceProperties.apiVersion >> 22) << "." << ((deviceProperties.apiVersion >> 12) & 0x3ff) << "." << (deviceProperties.apiVersion & 0xfff) << std::endl;
				}
			}
		}
	}
#endif
	
	// Find a better way to pass this
	dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
	deviceCreatepNextChain = &dynamicRenderingFeatures;

	// Vulkan device creation
	vulkanDevice = new Device({
		.physicalDevice = physicalDevices[selectedDevice],
		.enabledExtensions = enabledDeviceExtensions,
		.requestedQueueTypes = { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT },
		.pNextChain = deviceCreatepNextChain,
		.useSwapChain = true
	});
	
	queue = vulkanDevice->getQueue(QueueType::Graphics);

	// Find a suitable depth format
	depthFormat = vulkanDevice->getSupportedDepthFormat();
	assert(depthFormat != VK_FORMAT_UNDEFINED);

	swapChain = new SwapChain({
		.instance = instance,
		.device = *vulkanDevice,
	});

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Get Android device name and manufacturer (to display along GPU name)
	androidProduct = "";
	char prop[PROP_VALUE_MAX+1];
	int len = __system_property_get("ro.product.manufacturer", prop);
	if (len > 0) {
		androidProduct += std::string(prop) + " ";
	};
	len = __system_property_get("ro.product.model", prop);
	if (len > 0) {
		androidProduct += std::string(prop);
	};
	LOGD("androidProduct = %s", androidProduct.c_str());
#endif	

	return true;
}

#if defined(_WIN32)
// Win32 : Sets up a console window and redirects standard output to it
void VulkanApplication::setupConsole(std::string title)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE *stream;
	freopen_s(&stream, "CONOUT$", "w+", stdout);
	freopen_s(&stream, "CONOUT$", "w+", stderr);
	SetConsoleTitle(TEXT(title.c_str()));
}

void VulkanApplication::setupDPIAwareness()
{
	typedef HRESULT *(__stdcall *SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);

	HMODULE shCore = LoadLibraryA("Shcore.dll");
	if (shCore)
	{
		SetProcessDpiAwarenessFunc setProcessDpiAwareness =
			(SetProcessDpiAwarenessFunc)GetProcAddress(shCore, "SetProcessDpiAwareness");

		if (setProcessDpiAwareness != nullptr)
		{
			setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		}

		FreeLibrary(shCore);
	}
}

void VulkanApplication::setupWindow()
{
	window = new sf::WindowBase(sf::VideoMode(width, height), "SFML window with Vulkan", settings.fullscreen ? sf::Style::Fullscreen : sf::Style::Default);
	window->setTitle(windowTitle);
}

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
int32_t VulkanApplication::handleAppInput(struct android_app* app, AInputEvent* event)
{
	VulkanExampleBase* vulkanExample = reinterpret_cast<VulkanExampleBase*>(app->userData);
	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
	{
		int32_t eventSource = AInputEvent_getSource(event);
		switch (eventSource) {
			case AINPUT_SOURCE_JOYSTICK: {
				// Left thumbstick
				vulkanExample->gamePadState.axisLeft.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
				vulkanExample->gamePadState.axisLeft.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
				// Right thumbstick
				vulkanExample->gamePadState.axisRight.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
				vulkanExample->gamePadState.axisRight.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
				break;
			}

			case AINPUT_SOURCE_TOUCHSCREEN: {
				int32_t action = AMotionEvent_getAction(event);

				switch (action) {
					case AMOTION_EVENT_ACTION_UP: {
						vulkanExample->lastTapTime = AMotionEvent_getEventTime(event);
						vulkanExample->touchPos.x = AMotionEvent_getX(event, 0);
						vulkanExample->touchPos.y = AMotionEvent_getY(event, 0);
						vulkanExample->touchTimer = 0.0;
						vulkanExample->touchDown = false;
						vulkanExample->camera.keys.up = false;

						// Detect single tap
						int64_t eventTime = AMotionEvent_getEventTime(event);
						int64_t downTime = AMotionEvent_getDownTime(event);
						if (eventTime - downTime <= vks::android::TAP_TIMEOUT) {
							float deadZone = (160.f / vks::android::screenDensity) * vks::android::TAP_SLOP * vks::android::TAP_SLOP;
							float x = AMotionEvent_getX(event, 0) - vulkanExample->touchPos.x;
							float y = AMotionEvent_getY(event, 0) - vulkanExample->touchPos.y;
							if ((x * x + y * y) < deadZone) {
								vulkanExample->mouseButtons.left = true;
							}
						};

						return 1;
						break;
					}
					case AMOTION_EVENT_ACTION_DOWN: {
						// Detect double tap
						int64_t eventTime = AMotionEvent_getEventTime(event);
						if (eventTime - vulkanExample->lastTapTime <= vks::android::DOUBLE_TAP_TIMEOUT) {
							float deadZone = (160.f / vks::android::screenDensity) * vks::android::DOUBLE_TAP_SLOP * vks::android::DOUBLE_TAP_SLOP;
							float x = AMotionEvent_getX(event, 0) - vulkanExample->touchPos.x;
							float y = AMotionEvent_getY(event, 0) - vulkanExample->touchPos.y;
							if ((x * x + y * y) < deadZone) {
								vulkanExample->keyPressed(TOUCH_DOUBLE_TAP);
								vulkanExample->touchDown = false;
							}
						}
						else {
							vulkanExample->touchDown = true;
						}
						vulkanExample->touchPos.x = AMotionEvent_getX(event, 0);
						vulkanExample->touchPos.y = AMotionEvent_getY(event, 0);
						vulkanExample->mousePos.x = AMotionEvent_getX(event, 0);
						vulkanExample->mousePos.y = AMotionEvent_getY(event, 0);
						break;
					}					
					case AMOTION_EVENT_ACTION_MOVE: {
						bool handled = false;
						ImGuiIO& io = ImGui::GetIO();
						handled = io.WantCaptureMouse;
						if (!handled) {
							int32_t eventX = AMotionEvent_getX(event, 0);
							int32_t eventY = AMotionEvent_getY(event, 0);

							float deltaX = (float)(vulkanExample->touchPos.y - eventY) * vulkanExample->rotationSpeed * 0.5f;
							float deltaY = (float)(vulkanExample->touchPos.x - eventX) * vulkanExample->rotationSpeed * 0.5f;

							vulkanExample->camera.rotate(glm::vec3(deltaX, 0.0f, 0.0f));
							vulkanExample->camera.rotate(glm::vec3(0.0f, -deltaY, 0.0f));

							vulkanExample->rotation.x += deltaX;
							vulkanExample->rotation.y -= deltaY;

							vulkanExample->viewChanged();

							vulkanExample->touchPos.x = eventX;
							vulkanExample->touchPos.y = eventY;
						}
						break;
					}
					default:
						return 1;
						break;
				}
			}

			return 1;
		}
	}

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY)
	{
		int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent*)event);
		int32_t action = AKeyEvent_getAction((const AInputEvent*)event);
		int32_t button = 0;

		if (action == AKEY_EVENT_ACTION_UP)
			return 0;

		switch (keyCode)
		{
		case AKEYCODE_BUTTON_A:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_A);
			break;
		case AKEYCODE_BUTTON_B:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_B);
			break;
		case AKEYCODE_BUTTON_X:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_X);
			break;
		case AKEYCODE_BUTTON_Y:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_Y);
			break;
		case AKEYCODE_BUTTON_L1:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_L1);
			break;
		case AKEYCODE_BUTTON_R1:
			vulkanExample->keyPressed(GAMEPAD_BUTTON_R1);
			break;
		case AKEYCODE_BUTTON_START:
			vulkanExample->paused = !vulkanExample->paused;
			break;
		};

		LOGD("Button %d pressed", keyCode);
	}

	return 0;
}

void VulkanApplication::handleAppCommand(android_app * app, int32_t cmd)
{
	assert(app->userData != NULL);
	VulkanExampleBase* vulkanExample = reinterpret_cast<VulkanExampleBase*>(app->userData);
	switch (cmd)
	{
	case APP_CMD_SAVE_STATE:
		LOGD("APP_CMD_SAVE_STATE");
		/*
		vulkanExample->app->savedState = malloc(sizeof(struct saved_state));
		*((struct saved_state*)vulkanExample->app->savedState) = vulkanExample->state;
		vulkanExample->app->savedStateSize = sizeof(struct saved_state);
		*/
		break;
	case APP_CMD_INIT_WINDOW:
		LOGD("APP_CMD_INIT_WINDOW");
		if (androidApp->window != NULL)
		{
			if (vulkanExample->initVulkan()) {
				vulkanExample->prepare();
				assert(vulkanExample->prepared);
			}
			else {
				LOGE("Could not initialize Vulkan, exiting!");
				androidApp->destroyRequested = 1;
			}
		}
		else
		{
			LOGE("No window assigned!");
		}
		break;
	case APP_CMD_LOST_FOCUS:
		LOGD("APP_CMD_LOST_FOCUS");
		vulkanExample->focused = false;
		break;
	case APP_CMD_GAINED_FOCUS:
		LOGD("APP_CMD_GAINED_FOCUS");
		vulkanExample->focused = true;
		break;
	case APP_CMD_TERM_WINDOW:
		// Window is hidden or closed, clean up resources
		LOGD("APP_CMD_TERM_WINDOW");
		if (vulkanExample->prepared) {
			vulkanExample->swapChain->cleanup();
		}
		break;
	}
}
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
void* VulkanApplication::setupWindow(void* view)
{
	this->view = view;
	return view;
}
#elif defined(_DIRECT2DISPLAY)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
/*static*/void VulkanApplication::registryGlobalCb(void *data,
		wl_registry *registry, uint32_t name, const char *interface,
		uint32_t version)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->registryGlobal(registry, name, interface, version);
}

/*static*/void VulkanApplication::seatCapabilitiesCb(void *data, wl_seat *seat,
		uint32_t caps)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->seatCapabilities(seat, caps);
}

/*static*/void VulkanApplication::pointerEnterCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface,
		wl_fixed_t sx, wl_fixed_t sy)
{
}

/*static*/void VulkanApplication::pointerLeaveCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface)
{
}

/*static*/void VulkanApplication::pointerMotionCb(void *data,
		wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerMotion(pointer, time, sx, sy);
}
void VulkanApplication::pointerMotion(wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	handleMouseMove(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

/*static*/void VulkanApplication::pointerButtonCb(void *data,
		wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerButton(pointer, serial, time, button, state);
}

void VulkanApplication::pointerButton(struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	switch (button)
	{
	case BTN_LEFT:
		mouseButtons.left = !!state;
		break;
	case BTN_MIDDLE:
		mouseButtons.middle = !!state;
		break;
	case BTN_RIGHT:
		mouseButtons.right = !!state;
		break;
	default:
		break;
	}
}

/*static*/void VulkanApplication::pointerAxisCb(void *data,
		wl_pointer *pointer, uint32_t time, uint32_t axis,
		wl_fixed_t value)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->pointerAxis(pointer, time, axis, value);
}

void VulkanApplication::pointerAxis(wl_pointer *pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value)
{
	double d = wl_fixed_to_double(value);
	switch (axis)
	{
	case REL_X:
		camera.translate(glm::vec3(0.0f, 0.0f, d * 0.005f * zoomSpeed));
		viewUpdated = true;
		break;
	default:
		break;
	}
}

/*static*/void VulkanApplication::keyboardKeymapCb(void *data,
		struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size)
{
}

/*static*/void VulkanApplication::keyboardEnterCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys)
{
}

/*static*/void VulkanApplication::keyboardLeaveCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface)
{
}

/*static*/void VulkanApplication::keyboardKeyCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state)
{
	VulkanExampleBase *self = reinterpret_cast<VulkanExampleBase *>(data);
	self->keyboardKey(keyboard, serial, time, key, state);
}

void VulkanApplication::keyboardKey(struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	switch (key)
	{
	case KEY_W:
		camera.keys.up = !!state;
		break;
	case KEY_S:
		camera.keys.down = !!state;
		break;
	case KEY_A:
		camera.keys.left = !!state;
		break;
	case KEY_D:
		camera.keys.right = !!state;
		break;
	case KEY_P:
		if (state)
			paused = !paused;
		break;
	case KEY_F1:
		if (state)
			overlay->visible = !overlay->visible;
		break;
	case KEY_ESC:
		quit = true;
		break;
	}

	if (state)
		keyPressed(key);
}

/*static*/void VulkanApplication::keyboardModifiersCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
}

void VulkanApplication::seatCapabilities(wl_seat *seat, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer)
	{
		pointer = wl_seat_get_pointer(seat);
		static const struct wl_pointer_listener pointer_listener =
		{ pointerEnterCb, pointerLeaveCb, pointerMotionCb, pointerButtonCb,
				pointerAxisCb, };
		wl_pointer_add_listener(pointer, &pointer_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer)
	{
		wl_pointer_destroy(pointer);
		pointer = nullptr;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard)
	{
		keyboard = wl_seat_get_keyboard(seat);
		static const struct wl_keyboard_listener keyboard_listener =
		{ keyboardKeymapCb, keyboardEnterCb, keyboardLeaveCb, keyboardKeyCb,
				keyboardModifiersCb, };
		wl_keyboard_add_listener(keyboard, &keyboard_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard)
	{
		wl_keyboard_destroy(keyboard);
		keyboard = nullptr;
	}
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

void VulkanApplication::registryGlobal(wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0)
	{
		compositor = (wl_compositor *) wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
	}
	else if (strcmp(interface, "xdg_wm_base") == 0)
	{
		shell = (xdg_wm_base *) wl_registry_bind(registry, name,
				&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(shell, &xdg_wm_base_listener, nullptr);
	}
	else if (strcmp(interface, "wl_seat") == 0)
	{
		seat = (wl_seat *) wl_registry_bind(registry, name, &wl_seat_interface,
				1);

		static const struct wl_seat_listener seat_listener =
		{ seatCapabilitiesCb, };
		wl_seat_add_listener(seat, &seat_listener, this);
	}
}

/*static*/void VulkanApplication::registryGlobalRemoveCb(void *data,
		struct wl_registry *registry, uint32_t name)
{
}

void VulkanApplication::initWaylandConnection()
{
	display = wl_display_connect(NULL);
	if (!display)
	{
		std::cout << "Could not connect to Wayland display!\n";
		fflush(stdout);
		exit(1);
	}

	registry = wl_display_get_registry(display);
	if (!registry)
	{
		std::cout << "Could not get Wayland registry!\n";
		fflush(stdout);
		exit(1);
	}

	static const struct wl_registry_listener registry_listener =
	{ registryGlobalCb, registryGlobalRemoveCb };
	wl_registry_add_listener(registry, &registry_listener, this);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (!compositor || !shell || !seat)
	{
		std::cout << "Could not bind Wayland protocols!\n";
		fflush(stdout);
		exit(1);
	}
}

void VulkanApplication::setSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	destWidth = width;
	destHeight = height;

	windowResize();
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	VulkanExampleBase *base = (VulkanExampleBase *) data;

	xdg_surface_ack_configure(surface, serial);
	base->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_handle_configure,
};


static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	VulkanExampleBase *base = (VulkanExampleBase *) data;

	base->setSize(width, height);
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	VulkanExampleBase *base = (VulkanExampleBase *) data;

	base->quit = true;
}


static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	xdg_toplevel_handle_configure,
	xdg_toplevel_handle_close,
};


struct xdg_surface *VulkanApplication::setupWindow()
{
	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(shell, surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, this);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, this);

	xdg_toplevel_set_title(xdg_toplevel, windowTitle.c_str());
	wl_surface_commit(surface);
	return xdg_surface;
}

#elif defined(VK_USE_PLATFORM_XCB_KHR)

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

// Set up a window using XCB and request event types
xcb_window_t VulkanApplication::setupWindow()
{
	uint32_t value_mask, value_list[32];

	window = xcb_generate_id(connection);

	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	value_list[0] = screen->black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

	if (settings.fullscreen)
	{
		width = destWidth = screen->width_in_pixels;
		height = destHeight = screen->height_in_pixels;
	}

	xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);

	/* Magic code that will send notification when window is destroyed */
	xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

	free(reply);

	if (settings.fullscreen)
	{
		xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
		xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
		xcb_change_property(connection,
				XCB_PROP_MODE_REPLACE,
				window, atom_wm_state->atom,
				XCB_ATOM_ATOM, 32, 1,
				&(atom_wm_fullscreen->atom));
		free(atom_wm_fullscreen);
		free(atom_wm_state);
	}	

	xcb_map_window(connection, window);

	return(window);
}

// Initialize XCB connection
void VulkanApplication::initxcbConnection()
{
	const xcb_setup_t *setup;
	xcb_screen_iterator_t iter;
	int scr;

	connection = xcb_connect(NULL, &scr);
	if (connection == NULL) {
		printf("Could not find a compatible Vulkan ICD!\n");
		fflush(stdout);
		exit(1);
	}

	setup = xcb_get_setup(connection);
	iter = xcb_setup_roots_iterator(setup);
	while (scr-- > 0)
		xcb_screen_next(&iter);
	screen = iter.data;
}

void VulkanApplication::handleEvent(const xcb_generic_event_t *event)
{
	switch (event->response_type & 0x7f)
	{
	case XCB_CLIENT_MESSAGE:
		if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
			(*atom_wm_delete_window).atom) {
			quit = true;
		}
		break;
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
		handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
		break;
	}
	break;
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseButtons.left = true;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseButtons.middle = true;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseButtons.right = true;
	}
	break;
	case XCB_BUTTON_RELEASE:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseButtons.left = false;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseButtons.middle = false;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseButtons.right = false;
	}
	break;
	case XCB_KEY_PRESS:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			case KEY_P:
				paused = !paused;
				break;
			case KEY_F1:
				overlay->visible = !overlay->visible;
				break;				
		}
	}
	break;	
	case XCB_KEY_RELEASE:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;			
			case KEY_ESCAPE:
				quit = true;
				break;
		}
		keyPressed(keyEvent->detail);
	}
	break;
	case XCB_DESTROY_NOTIFY:
		quit = true;
		break;
	case XCB_CONFIGURE_NOTIFY:
	{
		const xcb_configure_notify_event_t *cfgEvent = (const xcb_configure_notify_event_t *)event;
		if ((prepared) && ((cfgEvent->width != width) || (cfgEvent->height != height)))
		{
				destWidth = cfgEvent->width;
				destHeight = cfgEvent->height;
				if ((destWidth > 0) && (destHeight > 0))
				{
					windowResize();
				}
		}
	}
	break;
	default:
		break;
	}
}
#endif

void VulkanApplication::viewChanged() {}

void VulkanApplication::keyPressed(uint32_t) {}

void VulkanApplication::mouseMoved(double x, double y, bool & handled) {}

void VulkanApplication::setupDepthStencil()
{
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
	imageCI.extent = { width, height, 1 };
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	VK_CHECK_RESULT(vkCreateImage(*vulkanDevice, &imageCI, nullptr, &depthStencil.image));
	VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(*vulkanDevice, depthStencil.image, &memReqs);

	VkMemoryAllocateInfo memAllloc{};
	memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAllloc.allocationSize = memReqs.size;
	memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(*vulkanDevice, &memAllloc, nullptr, &depthStencil.memory));
	VK_CHECK_RESULT(vkBindImageMemory(*vulkanDevice, depthStencil.image, depthStencil.memory, 0));

	VkImageViewCreateInfo imageViewCI{};
	imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCI.image = depthStencil.image;
	imageViewCI.format = depthFormat;
	imageViewCI.subresourceRange.baseMipLevel = 0;
	imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	// Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	VK_CHECK_RESULT(vkCreateImageView(*vulkanDevice, &imageViewCI, nullptr, &depthStencil.view));
}

void VulkanApplication::setupImages()
{
	if (settings.sampleCount > VK_SAMPLE_COUNT_1_BIT) {
		// Check if device supports requested sample count for color and depth frame buffer
		//assert((deviceProperties.limits.framebufferColorSampleCounts >= sampleCount) && (deviceProperties.limits.framebufferDepthSampleCounts >= sampleCount));

		if (multisampleTarget.color.image != VK_NULL_HANDLE) {
			vkDestroyImage(*vulkanDevice, multisampleTarget.color.image, nullptr);
			vkDestroyImageView(*vulkanDevice, multisampleTarget.color.view, nullptr);
			vkFreeMemory(*vulkanDevice, multisampleTarget.color.memory, nullptr);
			vkDestroyImage(*vulkanDevice, multisampleTarget.depth.image, nullptr);
			vkDestroyImageView(*vulkanDevice, multisampleTarget.depth.view, nullptr);
			vkFreeMemory(*vulkanDevice, multisampleTarget.depth.memory, nullptr);
		}

		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = swapChain->colorFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.samples = settings.sampleCount;
		imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(*vulkanDevice, &imageCI, nullptr, &multisampleTarget.color.image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(*vulkanDevice, multisampleTarget.color.image, &memReqs);
		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		VkBool32 lazyMemTypePresent;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent) {
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		VK_CHECK_RESULT(vkAllocateMemory(*vulkanDevice, &memAllocInfo, nullptr, &multisampleTarget.color.memory));
		vkBindImageMemory(*vulkanDevice, multisampleTarget.color.image, multisampleTarget.color.memory, 0);

		// Create image view for the MSAA target
		VkImageViewCreateInfo imageViewCI{};
		imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCI.image = multisampleTarget.color.image;
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = swapChain->colorFormat;
		imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
		imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
		imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
		imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
		imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(*vulkanDevice, &imageViewCI, nullptr, &multisampleTarget.color.view));

		// Depth target
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.samples = settings.sampleCount;
		imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(*vulkanDevice, &imageCI, nullptr, &multisampleTarget.depth.image));

		vkGetImageMemoryRequirements(*vulkanDevice, multisampleTarget.depth.image, &memReqs);
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent) {
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		VK_CHECK_RESULT(vkAllocateMemory(*vulkanDevice, &memAllocInfo, nullptr, &multisampleTarget.depth.memory));
		vkBindImageMemory(*vulkanDevice, multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

		// Create image view for the MSAA target
		imageViewCI.image = multisampleTarget.depth.image;
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = depthFormat;
		imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
		imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
		imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
		imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
		imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(*vulkanDevice, &imageViewCI, nullptr, &multisampleTarget.depth.view));
	}
}

void VulkanApplication::windowResize()
{
	if (!prepared)
	{
		return;
	}
	prepared = false;

	// Ensure all operations on the device have been finished before destroying resources
	vulkanDevice->waitIdle();

	// Recreate swap chain
	width = destWidth;
	height = destHeight;
	swapChain->create(&width, &height, settings.vsync);

	// Recreate the frame buffers
	vkDestroyImageView(*vulkanDevice, depthStencil.view, nullptr);
	vkDestroyImage(*vulkanDevice, depthStencil.image, nullptr);
	vkFreeMemory(*vulkanDevice, depthStencil.memory, nullptr);
	setupDepthStencil();	
	setupImages();

	if ((width > 0.0f) && (height > 0.0f)) {
		overlay->resize(width, height);
	}

	vulkanDevice->waitIdle();

	if ((width > 0.0f) && (height > 0.0f)) {
		camera.updateAspectRatio((float)width / (float)height);
	}

	// Notify derived class
	windowResized();
	viewChanged();

	prepared = true;
}

void VulkanApplication::handleMouseMove(int32_t x, int32_t y)
{
	int32_t dx = (int32_t)x - mousePos.x;
	int32_t dy = (int32_t)y - mousePos.y;

	bool handled = false;

	if (overlay->visible) {
		ImGuiIO& io = ImGui::GetIO();
		handled = io.WantCaptureMouse;
	}
	mouseMoved((float)x, (float)y, handled);

	if (handled) {
		mousePos = glm::vec2((float)x, (float)y);
		return;
	}

	//if (mouseButtons.left) {
	//	camera.rotate(glm::vec3(dy * camera.rotationSpeed, dx * camera.rotationSpeed, 0.0f));
	//	viewUpdated = true;
	//}
	if (mouseButtons.right) {
		//camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f));
		viewUpdated = true;
	}
	if (mouseButtons.middle) {
		//camera.translate(glm::vec3(-dx * 0.005f, -dy * 0.005f, 0.0f));
		viewUpdated = true;
	}
	mousePos = glm::vec2((float)x, (float)y);
}

void VulkanApplication::windowResized()
{
	// Can be overriden in derived class
}

void VulkanApplication::initSwapchain()
{
#if defined(_WIN32)
	swapChain->initSurface(window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)	
	swapChain->initSurface(androidApp->window);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	swapChain->initSurface(view);
#elif defined(_DIRECT2DISPLAY)
	swapChain->initSurface(width, height);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	swapChain->initSurface(display, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	swapChain->initSurface(connection, window);
#endif
}

void VulkanApplication::OnUpdateOverlay(vks::UIOverlay &overlay) {}

void VulkanApplication::prepareFrame(VulkanFrameObjects& frame)
{
	// Ensure command buffer execution has finished
	VK_CHECK_RESULT(vkWaitForFences(*vulkanDevice, 1, &frame.renderCompleteFence, VK_TRUE, UINT64_MAX));
	VK_CHECK_RESULT(vkResetFences(*vulkanDevice, 1, &frame.renderCompleteFence));
	// Acquire the next image from the swap chain
	VkResult result = swapChain->acquireNextImage(frame.presentCompleteSemaphore, &currentBuffer);
	// @todo: rework after removing currentBuffer
	swapChain->currentImageIndex = currentBuffer;
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
		windowResize();
	}
	else {
		VK_CHECK_RESULT(result);
	}
}

void VulkanApplication::submitFrame(VulkanFrameObjects& frame)
{
	// Submit command buffer to queue
	VkPipelineStageFlags submitWaitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.pWaitDstStageMask = &submitWaitStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &frame.presentCompleteSemaphore;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &frame.renderCompleteSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame.commandBuffer->handle;
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, frame.renderCompleteFence));

	// Present image to queue
	VkResult result = swapChain->queuePresent(queue, currentBuffer, frame.renderCompleteSemaphore);
	if (!((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// Swap chain is no longer compatible with the surface and needs to be recreated
			windowResize();
			return;
		}
		else {
			VK_CHECK_RESULT(result);
		}
	}

	frameIndex++;
	if (frameIndex >= renderAhead) {
		frameIndex = 0;
	}
}

uint32_t VulkanApplication::getFrameCount()
{
	return renderAhead;
}

uint32_t VulkanApplication::getCurrentFrameIndex()
{
	return frameIndex;
}

void VulkanApplication::createBaseFrameObjects(VulkanFrameObjects& frame)
{
	frame.commandBuffer = new CommandBuffer({
		.device = *vulkanDevice,
		.pool = commandPool
	});
	VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VK_CHECK_RESULT(vkCreateFence(*vulkanDevice, &fenceCreateInfo, nullptr, &frame.renderCompleteFence));
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
	VK_CHECK_RESULT(vkCreateSemaphore(*vulkanDevice, &semaphoreCreateInfo, nullptr, &frame.presentCompleteSemaphore));
	VK_CHECK_RESULT(vkCreateSemaphore(*vulkanDevice, &semaphoreCreateInfo, nullptr, &frame.renderCompleteSemaphore));
}

void VulkanApplication::destroyBaseFrameObjects(VulkanFrameObjects& frame)
{
	vkDestroyFence(*vulkanDevice, frame.renderCompleteFence, nullptr);
	vkDestroySemaphore(*vulkanDevice, frame.presentCompleteSemaphore, nullptr);
	vkDestroySemaphore(*vulkanDevice, frame.renderCompleteSemaphore, nullptr);
}

void VulkanApplication::nextFrame()
{
	auto tStart = std::chrono::high_resolution_clock::now();

	// Check if the overlay's index and vertex buffers needs to be updated (recreated), e.g. because new elements are visible and indices or vertices require additional buffer space
	// TODO: Cap UI overlay update rates
	//updateOverlay();

	if (viewUpdated)
	{
		viewUpdated = false;
		viewChanged();
	}

	if (prepared) {
		render();
	}
	// @todo
	// resized = false;
	frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	frameTimer = (float)tDiff / 1000.0f;
	camera.update(frameTimer);
	if (camera.moving())
	{
		viewUpdated = true;
	}
	// Convert to clamped timer value
	if (!paused)
	{
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
	}
	float fpsTimer = (float)(std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count());
	if (fpsTimer > 1000.0f)
	{
		lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
		frameCounter = 0;
		lastTimestamp = tEnd;
	}
	overlay->updated = false;
}