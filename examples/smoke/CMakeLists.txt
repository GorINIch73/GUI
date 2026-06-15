add_executable(vlk_be_gui_smoke
    main.cpp
    triangle_pipeline.cpp
)

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc is required to build vlk_be_gui_smoke shaders")
endif()

set(VLK_BE_GUI_SMOKE_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VLK_BE_GUI_SMOKE_VERT_SPV "${VLK_BE_GUI_SMOKE_SHADER_DIR}/triangle.vert.spv")
set(VLK_BE_GUI_SMOKE_FRAG_SPV "${VLK_BE_GUI_SMOKE_SHADER_DIR}/triangle.frag.spv")
set(VLK_BE_GUI_SMOKE_GUI_VERT_SPV "${VLK_BE_GUI_SMOKE_SHADER_DIR}/gui.vert.spv")
set(VLK_BE_GUI_SMOKE_GUI_FRAG_SPV "${VLK_BE_GUI_SMOKE_SHADER_DIR}/gui.frag.spv")

add_custom_command(
    OUTPUT "${VLK_BE_GUI_SMOKE_VERT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VLK_BE_GUI_SMOKE_SHADER_DIR}"
    COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/shaders/triangle.vert" -o "${VLK_BE_GUI_SMOKE_VERT_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/triangle.vert"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VLK_BE_GUI_SMOKE_FRAG_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VLK_BE_GUI_SMOKE_SHADER_DIR}"
    COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/shaders/triangle.frag" -o "${VLK_BE_GUI_SMOKE_FRAG_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/triangle.frag"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VLK_BE_GUI_SMOKE_GUI_VERT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VLK_BE_GUI_SMOKE_SHADER_DIR}"
    COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/shaders/gui.vert" -o "${VLK_BE_GUI_SMOKE_GUI_VERT_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/gui.vert"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VLK_BE_GUI_SMOKE_GUI_FRAG_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VLK_BE_GUI_SMOKE_SHADER_DIR}"
    COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/shaders/gui.frag" -o "${VLK_BE_GUI_SMOKE_GUI_FRAG_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/gui.frag"
    VERBATIM
)

add_custom_target(vlk_be_gui_smoke_shaders
    DEPENDS
        "${VLK_BE_GUI_SMOKE_VERT_SPV}"
        "${VLK_BE_GUI_SMOKE_FRAG_SPV}"
        "${VLK_BE_GUI_SMOKE_GUI_VERT_SPV}"
        "${VLK_BE_GUI_SMOKE_GUI_FRAG_SPV}"
)

add_dependencies(vlk_be_gui_smoke vlk_be_gui_smoke_shaders)

target_link_libraries(vlk_be_gui_smoke
    PRIVATE
        vlk_be_gui::vlk_be_gui
)

target_compile_definitions(vlk_be_gui_smoke
    PRIVATE
        VLK_BE_GUI_SMOKE_VERT_SPV="${VLK_BE_GUI_SMOKE_VERT_SPV}"
        VLK_BE_GUI_SMOKE_FRAG_SPV="${VLK_BE_GUI_SMOKE_FRAG_SPV}"
        VLK_BE_GUI_SMOKE_GUI_VERT_SPV="${VLK_BE_GUI_SMOKE_GUI_VERT_SPV}"
        VLK_BE_GUI_SMOKE_GUI_FRAG_SPV="${VLK_BE_GUI_SMOKE_GUI_FRAG_SPV}"
)

if(MSVC)
    target_compile_options(vlk_be_gui_smoke PRIVATE /W4 /permissive-)
else()
    target_compile_options(vlk_be_gui_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()
