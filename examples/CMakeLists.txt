# Examples — each is a standalone executable linking against spectra

function(add_spectra_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE spectra)
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

add_spectra_example(basic_line)
add_spectra_example(next_level_scientific)
add_spectra_example(topic_publisher)

# ROS2 UI preview — mock of new spectra-ros panel features (no ROS2 required)
if(SPECTRA_USE_IMGUI)
    add_executable(ros2_ui_preview ros2_ui_preview.cpp)
    target_link_libraries(ros2_ui_preview PRIVATE spectra)
    target_compile_features(ros2_ui_preview PRIVATE cxx_std_20)
    target_include_directories(ros2_ui_preview PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${imgui_SOURCE_DIR}
    )
endif()
add_spectra_example(live_stream)
add_spectra_example(animated_scatter)
add_spectra_example(shapes_demo)
add_spectra_example(shapes3d_demo)
add_spectra_example(multi_subplot)
add_spectra_example(multi_figure_demo)
add_spectra_example(multi_window_tabs_demo)
add_spectra_example(offscreen_export)
add_spectra_example(video_record)
add_spectra_example(window_resize_test)
add_spectra_example(logger_example)
add_spectra_example(test_menubar)
add_spectra_example(plot_styles_demo)
add_spectra_example(timeline_curve_demo)
add_spectra_example(advanced_animation_demo)
add_spectra_example(shortcut_usage_demo)
add_spectra_example(shortcut_config_demo)
add_spectra_example(plugin_api_demo)
add_spectra_example(timeline_animation_demo)
add_spectra_example(easy_api_demo)
add_spectra_example(easy_realtime_demo)
add_spectra_example(easy_embed_demo)
add_spectra_example(embed_cpp_demo)
add_spectra_example(axes_menu_demo)
add_spectra_example(legend_panel_test)
add_spectra_example(empty_launch_csv)
add_spectra_example(comprehensive_subplot_demo)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/knob_demo.cpp)
    add_spectra_example(knob_demo)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/interactive_ref_demo.cpp)
    add_spectra_example(interactive_ref_demo)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stats_demo.cpp)
    add_spectra_example(stats_demo)
endif()

# 3D examples (conditional on 3D infrastructure being available)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/simple_3d_scatter.cpp)
    add_spectra_example(simple_3d_scatter)
    add_spectra_example(surface_3d)
    add_spectra_example(parametric_3d_line)
    add_spectra_example(demo_3d)
    add_spectra_example(axes3d_demo)
    add_spectra_example(camera_animation_3d)
    add_spectra_example(camera_animator_demo)
    add_spectra_example(mind_blowing_3d)
    add_spectra_example(lit_surface_demo)
    add_spectra_example(transparency_demo)
    add_spectra_example(mode_transition_demo)
    add_spectra_example(realtime_3d_stream)
endif()

# Multi-window demo (conditional — scaffolding available before full multi-window)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/multi_window_demo.cpp)
    add_spectra_example(multi_window_demo)
endif()

# ROS2 adapter demo — exercises all completed missions (A1-A6, B1-B3, C1, C2, C4)
# Requires: -DSPECTRA_USE_ROS2=ON and a sourced ROS2 Humble+ workspace
if(SPECTRA_USE_ROS2 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ros2_demo.cpp)
    add_executable(ros2_demo ros2_demo.cpp)
    target_link_libraries(ros2_demo PRIVATE spectra_ros2_adapter)
    target_compile_features(ros2_demo PRIVATE cxx_std_20)
    target_include_directories(ros2_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    if(SPECTRA_USE_IMGUI AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(ros2_demo PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()
endif()

# Eigen demo (requires -DSPECTRA_USE_EIGEN=ON)
if(SPECTRA_USE_EIGEN AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/eigen_demo.cpp)
    add_spectra_example(eigen_demo)
    target_link_libraries(eigen_demo PRIVATE Eigen3::Eigen)
endif()

# Qt embed demo (requires Qt adapter + Qt6 Widgets)
if(SPECTRA_BUILD_QT_EXAMPLE)
    if(NOT SPECTRA_USE_QT)
        message(FATAL_ERROR "SPECTRA_BUILD_QT_EXAMPLE=ON requires SPECTRA_USE_QT=ON")
    endif()

    find_package(Qt6 COMPONENTS Widgets REQUIRED)
    add_spectra_example(qt_embed_demo)
    target_link_libraries(qt_embed_demo PRIVATE spectra_qt_adapter Qt6::Widgets)
    target_include_directories(qt_embed_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_definitions(qt_embed_demo PRIVATE SPECTRA_HAS_QT6)

    # Dedicated multi-canvas binary for Phase 2 validation; reuses qt_embed_demo.cpp.
    add_executable(qt_multi_canvas_demo qt_embed_demo.cpp)
    target_link_libraries(qt_multi_canvas_demo PRIVATE spectra_qt_adapter Qt6::Widgets)
    target_compile_features(qt_multi_canvas_demo PRIVATE cxx_std_20)
    target_include_directories(qt_multi_canvas_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_definitions(qt_multi_canvas_demo PRIVATE
        SPECTRA_HAS_QT6
        SPECTRA_QT_FORCE_MULTI_CANVAS
    )

    # Minimal smoke test for Phase 2 validation.
    add_spectra_example(qt_smoke)
    target_link_libraries(qt_smoke PRIVATE spectra_qt_adapter Qt6::Widgets)
    target_include_directories(qt_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_definitions(qt_smoke PRIVATE SPECTRA_HAS_QT6)
endif()

# WebGPU demo — works with both Dawn (native) and Emscripten (wasm) targets.
# Requires -DSPECTRA_USE_WEBGPU=ON.  On Emscripten, produces an HTML+JS+WASM
# bundle using the webgpu_shell.html template.
if(SPECTRA_USE_WEBGPU AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/webgpu_demo.cpp)
    add_spectra_example(webgpu_demo)
    if(EMSCRIPTEN)
        target_link_options(webgpu_demo PRIVATE
            "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/webgpu_shell.html"
            "SHELL:-s USE_WEBGPU=1"
            "SHELL:-s USE_GLFW=3"
        )
        set_target_properties(webgpu_demo PROPERTIES SUFFIX ".html")
    endif()
endif()

# Plugin examples — shared libraries
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/CMakeLists.txt)
    add_subdirectory(plugins)
endif()
