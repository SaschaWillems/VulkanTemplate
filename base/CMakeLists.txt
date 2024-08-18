file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "./utilities/*.cpp" "./utilities/*.hpp" "./utilities/*.h" "./loaders/*.h" "./loaders/*.cpp" "./compilers/*.hpp" "./compilers/*.cpp" "./interfaces/*.cpp" "./interfaces/*.h" "./platforms/*.cpp" "./wrappers/*.cpp" "./wrappers/*.hpp" "../external/imgui/*.cpp" "../external/volk/volk.c" "../external/tracy/TracyClient.cpp" "./simulation/*.cpp" "./simulation/*.hpp")
file(GLOB BASE_HEADERS "*.hpp" "./interfaces/*.h")

# libktx
set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
)

add_library(base STATIC ${BASE_SRC} ${KTX_SOURCES})
if(WIN32)
    target_link_libraries(base ${WINLIBS})
 else(WIN32)
    target_link_libraries(base ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)