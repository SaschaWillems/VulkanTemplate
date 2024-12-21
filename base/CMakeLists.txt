
file(GLOB_RECURSE UTILITIES_SRC "./utilities/*.cpp")
file(GLOB_RECURSE UTILITIES_HEADERS "./utilities/*.hpp" "./utilities/*.h")

file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "./loaders/*.h" "./loaders/*.cpp" "./compilers/*.hpp" "./compilers/*.cpp" "./interfaces/*.cpp" "./interfaces/*.h" "./platforms/*.cpp" "./wrappers/*.cpp" "./wrappers/*.hpp" "../external/imgui/*.cpp" "../external/volk/volk.c" "../external/tracy/TracyClient.cpp" ${UTILITIES_SRC})
file(GLOB BASE_HEADERS "*.hpp" "../external/vma/vk_mem_alloc.h" "./interfaces/*.h" ${UTILITIES_HEADERS})

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

source_group("ktx" FILES ${KTX_SOURCES})
source_group("utilities" FILES ${UTILITIES_SRC})

# SFML
set(SFML_VERSION "2.6.1")

FetchContent_Declare(
    sfml
    GIT_REPOSITORY "https://github.com/SFML/SFML.git"
    GIT_TAG        "${SFML_VERSION}"
)

FetchContent_GetProperties(sfml)
if(NOT sfml_POPULATED)
    FetchContent_Populate(sfml)
    add_subdirectory(${sfml_SOURCE_DIR} ${sfml_BINARY_DIR})
endif()

add_library(base STATIC ${BASE_SRC} ${KTX_SOURCES} ${UTILITIES_SRC})
if(WIN32)
    target_link_libraries(base ${WINLIBS} sfml-audio sfml-system sfml-window)
 else(WIN32)
    target_link_libraries(base ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} sfml-audio sfml-system sfml-window)
endif(WIN32)