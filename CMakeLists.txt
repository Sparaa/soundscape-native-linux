cmake_minimum_required(VERSION 3.22)
project(soundscape-native-linux LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Vulkan REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Threads REQUIRED)
find_program(GLSLC glslc REQUIRED)

include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9 GIT_SHALLOW TRUE)
# GLFW 3.4 built in-tree with BOTH backends: the distro libglfw3 is X11-only, and under GNOME Wayland an Xwayland
# surface can only be presented from the GPU Xwayland is bound to (a cross-GPU copy capped this app at ~35 fps).
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.25 GIT_SHALLOW TRUE SOURCE_SUBDIR no-cmake-here)   # header only; skip its CMakeLists
FetchContent_MakeAvailable(glfw imgui miniaudio)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
target_compile_options(imgui PRIVATE -w)

# miniaudio: one translation unit with the implementation
add_library(ma_impl STATIC src/miniaudio_impl.c)
target_include_directories(ma_impl PUBLIC ${miniaudio_SOURCE_DIR})
target_link_libraries(ma_impl PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
target_compile_options(ma_impl PRIVATE -w)

# ---- shaders: GLSL -> SPIR-V -> embedded byte arrays --------------------------------------------------------------
set(SHADERS scene_inst.vert scene_inst.frag scene_static.vert scene_static.frag post.vert post.frag)
set(SPV_FILES)
foreach(s IN LISTS SHADERS)
  set(out ${CMAKE_BINARY_DIR}/shaders/${s}.spv)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${GLSLC} -O --target-env=vulkan1.1 -o ${out} ${CMAKE_SOURCE_DIR}/shaders/${s}
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${s} COMMENT "glslc ${s}" VERBATIM)
  list(APPEND SPV_FILES ${out})
endforeach()
set(EMBED_CPP ${CMAKE_BINARY_DIR}/embedded_shaders.cpp)
add_custom_command(OUTPUT ${EMBED_CPP}
  COMMAND ${CMAKE_COMMAND} -DOUT=${EMBED_CPP} -DSPV_DIR=${CMAKE_BINARY_DIR}/shaders "-DNAMES=${SHADERS}" -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${SPV_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake COMMENT "embedding SPIR-V" VERBATIM)

add_executable(soundscape-native
  src/main.cpp src/app.cpp src/api.cpp src/audio.cpp src/analysis.cpp src/abc.cpp src/player.cpp
  src/vk_context.cpp src/renderer.cpp src/ui.cpp ${EMBED_CPP})
target_include_directories(soundscape-native PRIVATE src)
target_link_libraries(soundscape-native PRIVATE imgui ma_impl Vulkan::Vulkan glfw CURL::libcurl nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(soundscape-native PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_definitions(soundscape-native PRIVATE GLFW_INCLUDE_NONE GLFW_INCLUDE_VULKAN)

install(TARGETS soundscape-native RUNTIME DESTINATION bin)
install(FILES packaging/soundscape-native.desktop DESTINATION share/applications)

# ---- tests (pure modules: analysis, abc, player timing) --------------------------------------------------------------
enable_testing()
add_executable(soundscape-tests tests/test_main.cpp src/analysis.cpp src/abc.cpp)
target_include_directories(soundscape-tests PRIVATE src)
target_link_libraries(soundscape-tests PRIVATE Threads::Threads)
add_test(NAME unit COMMAND soundscape-tests)
