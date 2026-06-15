add_executable(vlk_be_gui_draw_data_tests
    draw_data_tests.cpp
)

target_link_libraries(vlk_be_gui_draw_data_tests
    PRIVATE
        vlk_be_gui::vlk_be_gui
)

add_test(NAME vlk_be_gui_draw_data_tests COMMAND vlk_be_gui_draw_data_tests)

add_executable(vlk_be_gui_layout_tests
    layout_tests.cpp
)

target_link_libraries(vlk_be_gui_layout_tests
    PRIVATE
        vlk_be_gui::vlk_be_gui
)

add_test(NAME vlk_be_gui_layout_tests COMMAND vlk_be_gui_layout_tests)
