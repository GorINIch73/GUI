cmake_minimum_required(VERSION 3.24)

project(vlk_be_gui
    VERSION 0.1.0
    DESCRIPTION "Vulkan backend for desktop GUI applications"
    LANGUAGES CXX
)

option(VLK_BE_GUI_BUILD_EXAMPLES "Build examples" ON)
option(VLK_BE_GUI_BUILD_TESTS "Build tests" ON)
option(VLK_BE_GUI_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.4 REQUIRED)
find_program(GLSLC_EXECUTABLE glslc)

add_library(vlk_be_gui
    src/gui/layout.cpp
    src/platform/glfw_window.cpp
    src/vulkan/frame_renderer.cpp
    src/vulkan/gui_renderer.cpp
    src/vulkan/swapchain.cpp
    src/vulkan/vulkan_context.cpp
)

add_library(vlk_be_gui::vlk_be_gui ALIAS vlk_be_gui)

target_include_directories(vlk_be_gui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vlk_be_gui
    PUBLIC
        Vulkan::Vulkan
    PRIVATE
        glfw
)

target_compile_definitions(vlk_be_gui
    PRIVATE
        $<$<BOOL:${VLK_BE_GUI_ENABLE_VALIDATION}>:VLK_BE_GUI_ENABLE_VALIDATION=1>
)

if(MSVC)
    target_compile_options(vlk_be_gui PRIVATE /W4 /permissive-)
else()
    target_compile_options(vlk_be_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VLK_BE_GUI_BUILD_EXAMPLES)
    add_subdirectory(examples/smoke)
endif()

if(VLK_BE_GUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
