file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "./utilities/*.cpp" "./utilities/*.hpp" "./utilities/*.h" "./loaders/*.h" "./loaders/*.cpp" "./compilers/*.hpp" "./compilers/*.cpp" "./interfaces/*.cpp" "./interfaces/*.h" "./platforms/*.cpp" "./wrappers/*.cpp" "./wrappers/*.hpp" "../external/imgui/*.cpp" "../external/volk/volk.c" "../external/tracy/TracyClient.cpp" "./simulation/*.cpp" "./simulation/*.hpp")
file(GLOB BASE_HEADERS "*.hpp" "./interfaces/*.h")

add_library(base STATIC ${BASE_SRC})
if(WIN32)
    target_link_libraries(base ${WINLIBS} ktx)
 else(WIN32)
    target_link_libraries(base ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ktx)
endif(WIN32)