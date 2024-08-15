add_custom_target(shaders)
file(GLOB_RECURSE SHADER_FILES "*.hlsl" "includes/*.hlsl")
source_group("hlsl" FILES ${SHADER_FILES})
target_sources(shaders PRIVATE ${SHADER_FILES})
