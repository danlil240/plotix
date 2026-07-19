# Tests — unit tests, golden image tests, benchmarks

include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Treat Google Test headers as system includes so third-party warnings
# are not promoted to errors by -Werror.
foreach(_gt gtest gtest_main gmock gmock_main)
    if(TARGET ${_gt})
        get_target_property(_gt_inc ${_gt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_gt_inc)
            set_target_properties(${_gt} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_gt_inc}")
        endif()
    endif()
endforeach()

# Google Benchmark (optional)
option(SPECTRA_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(SPECTRA_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

# Helper function
function(add_spectra_test subdir name)
    string(REPLACE "/" "_" target_name "${subdir}_${name}")
    add_executable(${target_name} ${subdir}/${name}.cpp)
    target_link_libraries(${target_name} PRIVATE spectra GTest::gtest_main)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    # Tests need access to internal src/ headers (e.g. core/transform.hpp)
    # and shared test utilities (tests/util/)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/util
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}/generated/flatbuffers
    )
    target_include_directories(${target_name} SYSTEM PRIVATE
        ${flatbuffers_SOURCE_DIR}/include
    )
    # Propagate feature flags to tests for conditional compilation
    target_compile_definitions(${target_name} PRIVATE
        $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
        $<$<BOOL:${SPECTRA_USE_WEBGPU}>:SPECTRA_USE_WEBGPU>
        SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# Unit tests — guarded by if(EXISTS) so parallel agents don't break each other's builds.
# When adding a new test, just append the name to this list.
set(SPECTRA_UNIT_TESTS
    test_transform
    test_layout
    test_easing
    test_ring_buffer
    test_input
    test_series_data
    test_command_queue
    test_decimation
    test_filters
    test_tick_generation
    test_series_visibility
    test_ui_icons
    test_svg_export
    test_resize_layout
    test_layout_manager
    test_inspector
    test_animation_controller
    test_gesture_recognizer
    test_data_interaction
    test_transition_engine
    test_theme
    test_command_registry
    test_undo_manager
    test_command_palette_registry
    test_shortcut_manager
    test_undo_redo
    test_workspace
    test_figure_serializer
    test_native_dialog_policy
    test_clipboard_image
    test_region_select
    test_legend_interaction
    test_figure_manager
    test_inspector_stats
    test_box_zoom_overlay
    test_undo_property
    test_workspace_v2
    test_timeline_editor
    test_recording_export
    test_theme_colorblind
    test_phase2_integration
    test_split_view
    test_dock_system
    test_plot_style
    test_axis_link
    test_data_transform
    test_shared_cursor
    test_keyframe_interpolator
    test_camera_animator
    test_shortcut_config
    test_shell_panel
    test_shell_panel_registry
    test_shell_menu_bar
    test_shell_nav_rail
    test_shell_app_shell
    test_plugin_api
    test_workspace_v3
    test_phase3_integration
    test_math3d
    test_3d_pipelines
    test_camera
    # test_axes3d  # Not a proper GTEST file, uses plain functions with assert
    test_series3d
    test_depth_buffer
    test_3d_integration
    test_transparency
    test_mode_transition
    test_workspace_3d
    test_3d_regression
    test_multi_window
    test_window_manager
    test_glfw_platform_policy
    test_window_ui_context_builder
    test_automation_dispatch
    test_figure_registry
    test_event_bus
    test_ipc
    test_ipc_flatbuffers
    test_session_graph
    test_process_manager
    test_animation_tick_gate
    test_easy_api
    test_plot_annotations
    test_knob_manager
    test_python_ipc
    test_cross_codec
    test_text_renderer
    test_eigen_support
    test_csv_loader
    test_series_stats
    test_series_clipboard
    test_clipboard_export
    test_series_reorder
    test_data_editor
    test_embed_surface
    test_easy_embed
    test_ultra_zoom_precision
    test_figure_view_model
    test_axes_view_model
    test_series_view_model
    test_plugin_transforms
    test_plugin_overlays
    test_plugin_version_negotiation
    test_plugin_export
    test_plugin_data_source
    test_plugin_integration
    test_plugin_series_type
    test_plugin_guard
    test_plugin_diagnostics
    test_plugin_manifest
    test_chunked_array
    test_mapped_file
    test_lod_cache
    test_lod_cache_metrics
    test_chunked_series
    test_pending_series_data
    test_series_thread_safe
    test_accessibility
    test_workspace_autosave
    test_accessible_summary
    test_webgpu_backend
    test_expression_eval
)

# Mock transform plugin — shared library loaded by test_plugin_transforms
add_library(mock_transform_plugin SHARED unit/mock_transform_plugin.cpp)
target_include_directories(mock_transform_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(mock_transform_plugin PRIVATE cxx_std_20)
target_link_libraries(mock_transform_plugin PRIVATE spectra)
set_target_properties(mock_transform_plugin PROPERTIES PREFIX "")

# Mock overlay plugin — shared library loaded by test_plugin_overlays
add_library(mock_overlay_plugin SHARED unit/mock_overlay_plugin.cpp)
target_include_directories(mock_overlay_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(mock_overlay_plugin PRIVATE cxx_std_20)
if(WIN32)
    # Windows requires all symbols resolved at link time.
    # SPECTRA_PLUGIN_API (__declspec(dllexport)) ensures the entry points are
    # visible to GetProcAddress.
    target_link_libraries(mock_overlay_plugin PRIVATE spectra)
else()
    # On Unix, do NOT link spectra — the plugin resolves C ABI symbols from the
    # host at dlopen time.  Linking the static lib duplicates symbols, causing
    # macOS and ASan failures where the plugin's copy of OverlayRegistry methods
    # operates on the test's object with incompatible internal state.
    if(APPLE)
        target_link_options(mock_overlay_plugin PRIVATE -undefined dynamic_lookup)
    else()
        target_link_options(mock_overlay_plugin PRIVATE "LINKER:--allow-shlib-undefined")
    endif()
endif()
set_target_properties(mock_overlay_plugin PROPERTIES PREFIX "")

# Mock version-specific plugins for A3 version negotiation tests
foreach(_mock_ver v1_0 v1_1 v1_99)
    add_library(mock_plugin_${_mock_ver} SHARED unit/mock_plugin_${_mock_ver}.cpp)
    target_include_directories(mock_plugin_${_mock_ver} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_features(mock_plugin_${_mock_ver} PRIVATE cxx_std_20)
    target_link_libraries(mock_plugin_${_mock_ver} PRIVATE spectra)
    set_target_properties(mock_plugin_${_mock_ver} PROPERTIES PREFIX "")
endforeach()

# Mock integration plugin — shared library loaded by test_plugin_integration
add_library(mock_integration_plugin SHARED unit/mock_integration_plugin.cpp)
target_include_directories(mock_integration_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(mock_integration_plugin PRIVATE cxx_std_20)
target_link_libraries(mock_integration_plugin PRIVATE spectra)
set_target_properties(mock_integration_plugin PROPERTIES PREFIX "")

# Tests that require a GPU/display (Vulkan + GLFW window)
set(SPECTRA_GPU_TESTS
    test_3d_pipelines
    test_depth_buffer
    test_3d_integration
    test_transparency
    test_mode_transition
    test_workspace_3d
    test_3d_regression
    test_multi_window
    test_window_manager
    test_figure_registry
    test_easy_api
    test_phase2_integration
    test_phase3_integration
    test_text_renderer
    test_embed_surface
    test_easy_embed
)

foreach(_test ${SPECTRA_UNIT_TESTS})
    # Eigen tests require SPECTRA_USE_EIGEN
    if(_test STREQUAL "test_eigen_support" AND NOT SPECTRA_USE_EIGEN)
        continue()
    endif()
    # test_multi_window provides its own main() to avoid multi-VkInstance teardown
    # crash on NVIDIA — handled in the dedicated block below this loop.
    if(_test STREQUAL "test_multi_window")
        continue()
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/${_test}.cpp)
        add_spectra_test(unit ${_test})
        # Tag GPU-dependent tests so they can be excluded with -LE gpu
        if(_test IN_LIST SPECTRA_GPU_TESTS)
            set_tests_properties(unit_${_test} PROPERTIES
                LABELS "gpu"
                RESOURCE_LOCK gpu)
        endif()
        # Link Eigen3 for Eigen-specific tests
        if(_test STREQUAL "test_eigen_support" AND SPECTRA_USE_EIGEN)
            target_link_libraries(unit_${_test} PRIVATE Eigen3::Eigen)
        endif()
        # Pass mock plugin path to transform plugin tests
        if(_test STREQUAL "test_plugin_transforms")
            target_compile_definitions(unit_${_test} PRIVATE
                SPECTRA_MOCK_TRANSFORM_PLUGIN_PATH="$<TARGET_FILE:mock_transform_plugin>"
            )
            add_dependencies(unit_${_test} mock_transform_plugin)
        endif()
        # Pass mock overlay plugin path to overlay plugin tests
        if(_test STREQUAL "test_plugin_overlays")
            target_compile_definitions(unit_${_test} PRIVATE
                SPECTRA_MOCK_OVERLAY_PLUGIN_PATH="$<TARGET_FILE:mock_overlay_plugin>"
            )
            add_dependencies(unit_${_test} mock_overlay_plugin)
            # Export symbols so the dlopen'd plugin can resolve C ABI functions
            # from the test executable instead of carrying its own copy.
            if(NOT WIN32)
                target_link_options(unit_${_test} PRIVATE -rdynamic)
            endif()
        endif()
        # Pass mock version plugin paths to version negotiation tests
        if(_test STREQUAL "test_plugin_version_negotiation")
            target_compile_definitions(unit_${_test} PRIVATE
                SPECTRA_MOCK_PLUGIN_V1_0_PATH="$<TARGET_FILE:mock_plugin_v1_0>"
                SPECTRA_MOCK_PLUGIN_V1_1_PATH="$<TARGET_FILE:mock_plugin_v1_1>"
                SPECTRA_MOCK_PLUGIN_V1_99_PATH="$<TARGET_FILE:mock_plugin_v1_99>"
            )
            add_dependencies(unit_${_test} mock_plugin_v1_0 mock_plugin_v1_1 mock_plugin_v1_99)
        endif()
        if(_test STREQUAL "test_plugin_integration")
            target_compile_definitions(unit_${_test} PRIVATE
                SPECTRA_MOCK_INTEGRATION_PLUGIN_PATH="$<TARGET_FILE:mock_integration_plugin>"
            )
            add_dependencies(unit_${_test} mock_integration_plugin)
        endif()
        # WebGPU test: depend on WGSL shader generation (always available when
        # the wgsl source dir exists, regardless of SPECTRA_USE_WEBGPU).
        if(_test STREQUAL "test_webgpu_backend" AND TARGET spectra_wgsl_shaders)
            add_dependencies(unit_${_test} spectra_wgsl_shaders)
        endif()
    endif()
endforeach()

# test_multi_window uses a custom main() to avoid multi-VkInstance teardown crash on NVIDIA.
# (Same root cause as the 3D golden test SIGSEGV: the driver crashes when multiple
# VkInstance objects are destroyed sequentially in one process.)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_multi_window.cpp)
    add_executable(unit_test_multi_window unit/test_multi_window.cpp)
    target_link_libraries(unit_test_multi_window PRIVATE spectra GTest::gtest)
    target_compile_features(unit_test_multi_window PRIVATE cxx_std_20)
    target_include_directories(unit_test_multi_window PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/util
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}/generated/flatbuffers
    )
    target_include_directories(unit_test_multi_window SYSTEM PRIVATE
        ${flatbuffers_SOURCE_DIR}/include
    )
    target_compile_definitions(unit_test_multi_window PRIVATE
        $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
        $<$<BOOL:${SPECTRA_USE_WEBGPU}>:SPECTRA_USE_WEBGPU>
        SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME unit_test_multi_window COMMAND unit_test_multi_window)
    set_tests_properties(unit_test_multi_window PROPERTIES
        LABELS "gpu"
        RESOURCE_LOCK gpu)
endif()

# Golden image tests (require Vulkan — headless via lavapipe in CI)
option(SPECTRA_BUILD_GOLDEN_TESTS "Build golden image regression tests" ON)
if(SPECTRA_BUILD_GOLDEN_TESTS)
    add_executable(golden_image_tests golden/golden_test.cpp)
    target_link_libraries(golden_image_tests PRIVATE spectra GTest::gtest_main)
    target_compile_features(golden_image_tests PRIVATE cxx_std_20)
    target_include_directories(golden_image_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/golden
    )
    add_test(NAME golden_image_tests COMMAND golden_image_tests)

    # Label golden tests so CI can run them separately
    set_tests_properties(golden_image_tests PROPERTIES
        LABELS "golden;gpu"
        RESOURCE_LOCK gpu)

    # Phase 2 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_phase2.cpp)
        add_executable(golden_image_tests_phase2 golden/golden_test_phase2.cpp)
        target_link_libraries(golden_image_tests_phase2 PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_phase2 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_phase2 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_phase2 COMMAND golden_image_tests_phase2)
        set_tests_properties(golden_image_tests_phase2 PROPERTIES
            LABELS "golden;gpu"
            RESOURCE_LOCK gpu)
    endif()

    # Phase 3 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_phase3.cpp)
        add_executable(golden_image_tests_phase3 golden/golden_test_phase3.cpp)
        target_link_libraries(golden_image_tests_phase3 PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_phase3 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_phase3 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_phase3 COMMAND golden_image_tests_phase3)
        set_tests_properties(golden_image_tests_phase3 PROPERTIES
            LABELS "golden;gpu"
            RESOURCE_LOCK gpu)
    endif()

    # 3D Phase 3 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_3d_phase3.cpp)
        add_executable(golden_image_tests_3d_phase3 golden/golden_test_3d_phase3.cpp)
        target_link_libraries(golden_image_tests_3d_phase3 PRIVATE spectra GTest::gtest)
        target_compile_features(golden_image_tests_3d_phase3 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_3d_phase3 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_3d_phase3 COMMAND golden_image_tests_3d_phase3)
        set_tests_properties(golden_image_tests_3d_phase3 PROPERTIES
            LABELS "golden;gpu"
            RESOURCE_LOCK gpu)
    endif()

    # 3D golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_3d.cpp)
        add_executable(golden_image_tests_3d golden/golden_test_3d.cpp)
        target_link_libraries(golden_image_tests_3d PRIVATE spectra GTest::gtest)
        target_compile_features(golden_image_tests_3d PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_3d PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_3d COMMAND golden_image_tests_3d)
        set_tests_properties(golden_image_tests_3d PROPERTIES
            LABELS "golden;gpu"
            RESOURCE_LOCK gpu)
    endif()
endif()

# Benchmarks
if(SPECTRA_BUILD_BENCHMARKS)
    function(add_spectra_bench name)
        add_executable(${name} bench/${name}.cpp)
        target_link_libraries(${name} PRIVATE spectra benchmark::benchmark benchmark::benchmark_main)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
    endfunction()

    add_spectra_bench(bench_decimation)
    add_spectra_bench(bench_render)
    add_spectra_bench(bench_ui)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_phase2.cpp)
        add_spectra_bench(bench_phase2)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_phase3.cpp)
        add_spectra_bench(bench_phase3)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_3d.cpp)
        add_spectra_bench(bench_3d)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_3d_phase3.cpp)
        add_spectra_bench(bench_3d_phase3)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_multi_window.cpp)
        add_spectra_bench(bench_multi_window)
    endif()

    add_spectra_bench(bench_startup)
    add_spectra_bench(bench_ipc_codec)

    if(SPECTRA_USE_ROS2 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_ros3d.cpp)
        add_executable(bench_ros3d bench/bench_ros3d.cpp)
        target_link_libraries(bench_ros3d PRIVATE
            spectra_ros2_adapter
            benchmark::benchmark
            benchmark::benchmark_main
        )
        target_compile_features(bench_ros3d PRIVATE cxx_std_20)
        target_include_directories(bench_ros3d PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(bench_ros3d PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
    endif()
endif()

# ROS2 adapter tests — only built when SPECTRA_USE_ROS2 is ON and a ROS2
# workspace is sourced.  Each test links against spectra_ros2_adapter instead
# of the bare spectra library and supplies its own main() to register the
# shared RclcppEnvironment.
if(SPECTRA_USE_ROS2)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros2_bridge.cpp)
        add_executable(unit_test_ros2_bridge
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros2_bridge.cpp
        )
        target_link_libraries(unit_test_ros2_bridge PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_ros2_bridge PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros2_bridge PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros2_bridge PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros2_bridge COMMAND unit_test_ros2_bridge)
        set_tests_properties(unit_test_ros2_bridge PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_discovery.cpp)
        add_executable(unit_test_topic_discovery
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_discovery.cpp
        )
        target_link_libraries(unit_test_topic_discovery PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_topic_discovery PRIVATE cxx_std_20)
        target_include_directories(unit_test_topic_discovery PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_topic_discovery PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_topic_discovery COMMAND unit_test_topic_discovery)
        set_tests_properties(unit_test_topic_discovery PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session_merge.cpp)
        add_executable(unit_test_ros_session_merge
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session_merge.cpp
        )
        target_link_libraries(unit_test_ros_session_merge PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_session_merge PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_session_merge PRIVATE
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        add_test(NAME unit_test_ros_session_merge COMMAND unit_test_ros_session_merge)
        set_tests_properties(unit_test_ros_session_merge PROPERTIES LABELS "ros2")
    endif()

    if(SPECTRA_ROS2_BAG AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_bag_analyze.cpp)
        add_executable(unit_test_ros_bag_analyze
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_bag_analyze.cpp
        )
        target_link_libraries(unit_test_ros_bag_analyze PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_bag_analyze PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_bag_analyze PRIVATE
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        target_compile_definitions(unit_test_ros_bag_analyze PRIVATE SPECTRA_ROS2_BAG)
        add_test(NAME unit_test_ros_bag_analyze COMMAND unit_test_ros_bag_analyze)
        set_tests_properties(unit_test_ros_bag_analyze PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_time_clock.cpp)
        add_executable(unit_test_ros_time_clock
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_time_clock.cpp
        )
        target_link_libraries(unit_test_ros_time_clock PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_time_clock PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_time_clock PRIVATE
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        add_test(NAME unit_test_ros_time_clock COMMAND unit_test_ros_time_clock)
        set_tests_properties(unit_test_ros_time_clock PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_message_introspector.cpp)
        add_executable(unit_test_message_introspector
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_message_introspector.cpp
        )
        target_link_libraries(unit_test_message_introspector PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_message_introspector PRIVATE cxx_std_20)
        target_include_directories(unit_test_message_introspector PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_message_introspector PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_message_introspector COMMAND unit_test_message_introspector)
        set_tests_properties(unit_test_message_introspector PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_generic_subscriber.cpp)
        add_executable(unit_test_generic_subscriber
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_generic_subscriber.cpp
        )
        target_link_libraries(unit_test_generic_subscriber PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_generic_subscriber PRIVATE cxx_std_20)
        target_include_directories(unit_test_generic_subscriber PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_generic_subscriber PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_generic_subscriber COMMAND unit_test_generic_subscriber)
        set_tests_properties(unit_test_generic_subscriber PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_list_panel.cpp)
        add_executable(unit_test_topic_list_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_list_panel.cpp
        )
        target_link_libraries(unit_test_topic_list_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_topic_list_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_topic_list_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_topic_list_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_topic_list_panel COMMAND unit_test_topic_list_panel)
        set_tests_properties(unit_test_topic_list_panel PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_echo_panel.cpp)
        add_executable(unit_test_topic_echo_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_echo_panel.cpp
        )
        target_link_libraries(unit_test_topic_echo_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_topic_echo_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_topic_echo_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_topic_echo_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_topic_echo_panel COMMAND unit_test_topic_echo_panel)
        set_tests_properties(unit_test_topic_echo_panel PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_stats_overlay.cpp)
        add_executable(unit_test_topic_stats_overlay
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_topic_stats_overlay.cpp
        )
        target_link_libraries(unit_test_topic_stats_overlay PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_topic_stats_overlay PRIVATE cxx_std_20)
        target_include_directories(unit_test_topic_stats_overlay PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_topic_stats_overlay PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_topic_stats_overlay COMMAND unit_test_topic_stats_overlay)
        set_tests_properties(unit_test_topic_stats_overlay PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_plot_manager.cpp)
        add_executable(unit_test_ros_plot_manager
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_plot_manager.cpp
        )
        target_link_libraries(unit_test_ros_plot_manager PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_ros_plot_manager PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_plot_manager PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_plot_manager PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_plot_manager COMMAND unit_test_ros_plot_manager)
        set_tests_properties(unit_test_ros_plot_manager PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_phase_a_integration.cpp)
        add_executable(unit_test_phase_a_integration
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_phase_a_integration.cpp
        )
        target_link_libraries(unit_test_phase_a_integration PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_phase_a_integration PRIVATE cxx_std_20)
        target_include_directories(unit_test_phase_a_integration PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_phase_a_integration PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_phase_a_integration COMMAND unit_test_phase_a_integration)
        set_tests_properties(unit_test_phase_a_integration PROPERTIES LABELS "ros2")
    endif()

    # FieldDragDrop tests — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_field_drag_drop.cpp)
        add_executable(unit_test_field_drag_drop
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_field_drag_drop.cpp
        )
        target_link_libraries(unit_test_field_drag_drop PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_field_drag_drop PRIVATE cxx_std_20)
        target_include_directories(unit_test_field_drag_drop PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_field_drag_drop PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_field_drag_drop COMMAND unit_test_field_drag_drop)
        set_tests_properties(unit_test_field_drag_drop PROPERTIES LABELS "ros2")
    endif()

    # BagReader tests — gated behind SPECTRA_ROS2_BAG; stub tests run without it.
    # Full tests use custom main() with RclcppEnvironment; stub tests use GTest::gtest_main.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_reader.cpp)
        add_executable(unit_test_bag_reader
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_reader.cpp
        )
        if(SPECTRA_ROS2_BAG)
            target_link_libraries(unit_test_bag_reader PRIVATE
                spectra_ros2_adapter
                GTest::gtest
            )
        else()
            target_link_libraries(unit_test_bag_reader PRIVATE
                spectra_ros2_adapter
                GTest::gtest_main
            )
        endif()
        target_compile_features(unit_test_bag_reader PRIVATE cxx_std_20)
        target_include_directories(unit_test_bag_reader PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_bag_reader PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_ROS2_BAG}>:SPECTRA_ROS2_BAG>
        )
        add_test(NAME unit_test_bag_reader COMMAND unit_test_bag_reader)
        set_tests_properties(unit_test_bag_reader PROPERTIES LABELS "ros2")
    endif()

    # BagRecorder tests — gated behind SPECTRA_ROS2_BAG; stub tests run without it.
    # Full tests use custom main() with RclcppEnvironment; stub tests use GTest::gtest_main.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_recorder.cpp)
        add_executable(unit_test_bag_recorder
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_recorder.cpp
        )
        if(SPECTRA_ROS2_BAG)
            target_link_libraries(unit_test_bag_recorder PRIVATE
                spectra_ros2_adapter
                GTest::gtest
            )
        else()
            target_link_libraries(unit_test_bag_recorder PRIVATE
                spectra_ros2_adapter
                GTest::gtest_main
            )
        endif()
        target_compile_features(unit_test_bag_recorder PRIVATE cxx_std_20)
        target_include_directories(unit_test_bag_recorder PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_bag_recorder PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_ROS2_BAG}>:SPECTRA_ROS2_BAG>
        )
        add_test(NAME unit_test_bag_recorder COMMAND unit_test_bag_recorder)
        set_tests_properties(unit_test_bag_recorder PROPERTIES LABELS "ros2")
    endif()

    # TfTreePanel tests (F2) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tf_tree_panel.cpp)
        add_executable(unit_test_tf_tree_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tf_tree_panel.cpp
        )
        target_link_libraries(unit_test_tf_tree_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_tf_tree_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_tf_tree_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_tf_tree_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_tf_tree_panel COMMAND unit_test_tf_tree_panel)
        set_tests_properties(unit_test_tf_tree_panel PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tf_buffer.cpp)
        add_executable(unit_test_tf_buffer
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tf_buffer.cpp
        )
        target_link_libraries(unit_test_tf_buffer PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_tf_buffer PRIVATE cxx_std_20)
        target_include_directories(unit_test_tf_buffer PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_tf_buffer PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_tf_buffer COMMAND unit_test_tf_buffer)
        set_tests_properties(unit_test_tf_buffer PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_urdf_parser.cpp)
        add_executable(unit_test_urdf_parser
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_urdf_parser.cpp
        )
        target_link_libraries(unit_test_urdf_parser PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_urdf_parser PRIVATE cxx_std_20)
        target_include_directories(unit_test_urdf_parser PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_urdf_parser PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_urdf_parser COMMAND unit_test_urdf_parser)
        set_tests_properties(unit_test_urdf_parser PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_robot_model_display.cpp)
        add_executable(unit_test_robot_model_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_robot_model_display.cpp
        )
        target_link_libraries(unit_test_robot_model_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_robot_model_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_robot_model_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_robot_model_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_robot_model_display COMMAND unit_test_robot_model_display)
        set_tests_properties(unit_test_robot_model_display PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_display_registry.cpp)
        add_executable(unit_test_display_registry
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_display_registry.cpp
        )
        target_link_libraries(unit_test_display_registry PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_display_registry PRIVATE cxx_std_20)
        target_include_directories(unit_test_display_registry PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_display_registry PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_display_registry COMMAND unit_test_display_registry)
        set_tests_properties(unit_test_display_registry PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_package_resolver.cpp)
        add_executable(unit_test_ros_package_resolver
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_package_resolver.cpp
        )
        target_link_libraries(unit_test_ros_package_resolver PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_package_resolver PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_package_resolver PRIVATE
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        add_test(NAME unit_test_ros_package_resolver COMMAND unit_test_ros_package_resolver)
        set_tests_properties(unit_test_ros_package_resolver PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_measure_tool.cpp)
        add_executable(unit_test_measure_tool
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_measure_tool.cpp
        )
        target_link_libraries(unit_test_measure_tool PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_measure_tool PRIVATE cxx_std_20)
        target_include_directories(unit_test_measure_tool PRIVATE
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        add_test(NAME unit_test_measure_tool COMMAND unit_test_measure_tool)
        set_tests_properties(unit_test_measure_tool PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_scene_manager.cpp)
        add_executable(unit_test_scene_manager
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_scene_manager.cpp
        )
        target_link_libraries(unit_test_scene_manager PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_scene_manager PRIVATE cxx_std_20)
        target_include_directories(unit_test_scene_manager PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_scene_manager PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_scene_manager COMMAND unit_test_scene_manager)
        set_tests_properties(unit_test_scene_manager PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_scene_viewport.cpp)
        add_executable(unit_test_scene_viewport
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_scene_viewport.cpp
        )
        target_link_libraries(unit_test_scene_viewport PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_scene_viewport PRIVATE cxx_std_20)
        target_include_directories(unit_test_scene_viewport PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_scene_viewport PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_scene_viewport COMMAND unit_test_scene_viewport)
        set_tests_properties(unit_test_scene_viewport PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_marker_display.cpp)
        add_executable(unit_test_marker_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_marker_display.cpp
        )
        target_link_libraries(unit_test_marker_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_marker_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_marker_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_marker_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_marker_display COMMAND unit_test_marker_display)
        set_tests_properties(unit_test_marker_display PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_pointcloud_display.cpp)
        add_executable(unit_test_pointcloud_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_pointcloud_display.cpp
        )
        target_link_libraries(unit_test_pointcloud_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_pointcloud_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_pointcloud_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_pointcloud_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_pointcloud_display COMMAND unit_test_pointcloud_display)
        set_tests_properties(unit_test_pointcloud_display PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_laserscan_display.cpp)
        add_executable(unit_test_laserscan_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_laserscan_display.cpp
        )
        target_link_libraries(unit_test_laserscan_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_laserscan_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_laserscan_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_laserscan_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_laserscan_display COMMAND unit_test_laserscan_display)
        set_tests_properties(unit_test_laserscan_display PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_image_display.cpp)
        add_executable(unit_test_image_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_image_display.cpp
        )
        target_link_libraries(unit_test_image_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_image_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_image_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_image_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_image_display COMMAND unit_test_image_display)
        set_tests_properties(unit_test_image_display PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_path_pose_display.cpp)
        add_executable(unit_test_path_pose_display
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_path_pose_display.cpp
        )
        target_link_libraries(unit_test_path_pose_display PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_path_pose_display PRIVATE cxx_std_20)
        target_include_directories(unit_test_path_pose_display PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_path_pose_display PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_path_pose_display COMMAND unit_test_path_pose_display)
        set_tests_properties(unit_test_path_pose_display PROPERTIES LABELS "ros2")
    endif()

    # NodeGraphPanel tests — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_node_graph_panel.cpp)
        add_executable(unit_test_node_graph_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_node_graph_panel.cpp
        )
        target_link_libraries(unit_test_node_graph_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_node_graph_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_node_graph_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_node_graph_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_node_graph_panel COMMAND unit_test_node_graph_panel)
        set_tests_properties(unit_test_node_graph_panel PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_subplot_manager.cpp)
        add_executable(unit_test_subplot_manager
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_subplot_manager.cpp
        )
        target_link_libraries(unit_test_subplot_manager PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_subplot_manager PRIVATE cxx_std_20)
        target_include_directories(unit_test_subplot_manager PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_subplot_manager PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_subplot_manager COMMAND unit_test_subplot_manager)
        set_tests_properties(unit_test_subplot_manager PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_plot_series_pruning.cpp)
        add_executable(unit_test_plot_series_pruning
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_plot_series_pruning.cpp
        )
        target_link_libraries(unit_test_plot_series_pruning PRIVATE
            spectra
            GTest::gtest_main
        )
        target_compile_features(unit_test_plot_series_pruning PRIVATE cxx_std_20)
        target_include_directories(unit_test_plot_series_pruning PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
        )
        add_test(NAME unit_test_plot_series_pruning COMMAND unit_test_plot_series_pruning)
    endif()

    # ExpressionEngine tests — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_expression_engine.cpp)
        add_executable(unit_test_expression_engine
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_expression_engine.cpp
        )
        target_link_libraries(unit_test_expression_engine PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_expression_engine PRIVATE cxx_std_20)
        target_include_directories(unit_test_expression_engine PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_expression_engine PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_expression_engine COMMAND unit_test_expression_engine)
        set_tests_properties(unit_test_expression_engine PROPERTIES LABELS "ros2")
    endif()

    # Phase C integration test — C1 (RosPlotManager), C2 (auto-scroll),
    # C4 (SubplotManager) end-to-end: 3 topics, 100 frames, scroll, pruning, linked axes.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_phase_c_integration.cpp)
        add_executable(unit_test_phase_c_integration
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_phase_c_integration.cpp
        )
        target_link_libraries(unit_test_phase_c_integration PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_phase_c_integration PRIVATE cxx_std_20)
        target_include_directories(unit_test_phase_c_integration PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_phase_c_integration PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_phase_c_integration COMMAND unit_test_phase_c_integration)
        set_tests_properties(unit_test_phase_c_integration PROPERTIES LABELS "ros2")
    endif()

    # RosCsvExport tests — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_csv_export.cpp)
        add_executable(unit_test_ros_csv_export
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_csv_export.cpp
        )
        target_link_libraries(unit_test_ros_csv_export PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_csv_export PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_csv_export PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_csv_export PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_csv_export COMMAND unit_test_ros_csv_export)
        set_tests_properties(unit_test_ros_csv_export PROPERTIES LABELS "ros2")
    endif()

    # BagInfoPanel tests (D4) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_info_panel.cpp)
        add_executable(unit_test_bag_info_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_info_panel.cpp
        )
        target_link_libraries(unit_test_bag_info_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_bag_info_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_bag_info_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_bag_info_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_ROS2_BAG}>:SPECTRA_ROS2_BAG>
        )
        add_test(NAME unit_test_bag_info_panel COMMAND unit_test_bag_info_panel)
        set_tests_properties(unit_test_bag_info_panel PROPERTIES LABELS "ros2")
    endif()

    # RosClipboardExport tests (E2) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_clipboard_export.cpp)
        add_executable(unit_test_ros_clipboard_export
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_clipboard_export.cpp
        )
        target_link_libraries(unit_test_ros_clipboard_export PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_clipboard_export PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_clipboard_export PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_clipboard_export PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_clipboard_export COMMAND unit_test_ros_clipboard_export)
        set_tests_properties(unit_test_ros_clipboard_export PROPERTIES LABELS "ros2")
    endif()

    # RosAppShell tests (G1) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_app_shell.cpp)
        add_executable(unit_test_ros_app_shell
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_app_shell.cpp
        )
        target_link_libraries(unit_test_ros_app_shell PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_app_shell PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_app_shell PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_app_shell PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_app_shell COMMAND unit_test_ros_app_shell)
        set_tests_properties(unit_test_ros_app_shell PROPERTIES LABELS "ros2")
    endif()

    # BagPlayer tests (D2) — state machine / logic tests run without SPECTRA_ROS2_BAG;
    # full injection + activity-band tests require SPECTRA_ROS2_BAG=ON.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_player.cpp)
        add_executable(unit_test_bag_player
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_bag_player.cpp
        )
        if(SPECTRA_ROS2_BAG)
            target_link_libraries(unit_test_bag_player PRIVATE
                spectra_ros2_adapter
                GTest::gtest
            )
        else()
            target_link_libraries(unit_test_bag_player PRIVATE
                spectra_ros2_adapter
                GTest::gtest_main
            )
        endif()
        target_compile_features(unit_test_bag_player PRIVATE cxx_std_20)
        target_include_directories(unit_test_bag_player PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_bag_player PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_ROS2_BAG}>:SPECTRA_ROS2_BAG>
        )
        add_test(NAME unit_test_bag_player COMMAND unit_test_bag_player)
        set_tests_properties(unit_test_bag_player PROPERTIES LABELS "ros2")
    endif()

    # G2: Tools → ROS2 Adapter menu integration tests.
    # Tests ros2_adapter_state flag lifecycle and command registry metadata.
    # Pure C++ logic — no ROS2 runtime, no ImGui context, no GLFW window needed.
    # Uses gtest_main (no custom main / no RclcppEnvironment).
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros2_menu_integration.cpp)
        add_executable(unit_test_ros2_menu_integration
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros2_menu_integration.cpp
        )
        target_link_libraries(unit_test_ros2_menu_integration PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros2_menu_integration PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros2_menu_integration PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros2_menu_integration PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros2_menu_integration COMMAND unit_test_ros2_menu_integration)
        set_tests_properties(unit_test_ros2_menu_integration PROPERTIES LABELS "ros2")
    endif()

    # RosScreenshotExport tests (E3) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    # Writes real PNG files to /tmp to validate stb_image_write integration.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_screenshot_export.cpp)
        add_executable(unit_test_ros_screenshot_export
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_screenshot_export.cpp
        )
        target_link_libraries(unit_test_ros_screenshot_export PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_screenshot_export PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_screenshot_export PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_screenshot_export PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_screenshot_export COMMAND unit_test_ros_screenshot_export)
        set_tests_properties(unit_test_ros_screenshot_export PROPERTIES LABELS "ros2")
    endif()

    # ParamEditorPanel tests (F3) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_param_editor_panel.cpp)
        add_executable(unit_test_param_editor_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_param_editor_panel.cpp
        )
        target_link_libraries(unit_test_param_editor_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_param_editor_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_param_editor_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_param_editor_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_param_editor_panel COMMAND unit_test_param_editor_panel)
        set_tests_properties(unit_test_param_editor_panel PROPERTIES LABELS "ros2")
    endif()

    # H1: Discovery + Introspection unit tests — requires ROS2 runtime for
    # graph queries; uses custom main() with shared RclcppEnvironment.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_discovery_introspection.cpp)
        add_executable(unit_test_discovery_introspection
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_discovery_introspection.cpp
        )
        target_link_libraries(unit_test_discovery_introspection PRIVATE
            spectra_ros2_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_discovery_introspection PRIVATE cxx_std_20)
        target_include_directories(unit_test_discovery_introspection PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_discovery_introspection PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_discovery_introspection
                 COMMAND unit_test_discovery_introspection)
        set_tests_properties(unit_test_discovery_introspection
                             PROPERTIES LABELS "ros2")
    endif()

    # ServiceCaller + ServiceCallerPanel tests (F4) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_service_caller.cpp)
        add_executable(unit_test_service_caller
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_service_caller.cpp
        )
        target_link_libraries(unit_test_service_caller PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_service_caller PRIVATE cxx_std_20)
        target_include_directories(unit_test_service_caller PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_service_caller PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_service_caller COMMAND unit_test_service_caller)
        set_tests_properties(unit_test_service_caller PROPERTIES LABELS "ros2")
    endif()

    # RosSession tests (G3) — pure C++ logic, no ROS2 runtime needed.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session.cpp)
        add_executable(unit_test_ros_session
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session.cpp
        )
        target_link_libraries(unit_test_ros_session PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_session PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_session PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ros_session PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ros_session COMMAND unit_test_ros_session)
        set_tests_properties(unit_test_ros_session PROPERTIES LABELS "ros2")
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session_panels.cpp)
        add_executable(unit_test_ros_session_panels
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ros_session_panels.cpp
        )
        target_link_libraries(unit_test_ros_session_panels PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_ros_session_panels PRIVATE cxx_std_20)
        target_include_directories(unit_test_ros_session_panels PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        add_test(NAME unit_test_ros_session_panels COMMAND unit_test_ros_session_panels)
        set_tests_properties(unit_test_ros_session_panels PROPERTIES LABELS "ros2")
    endif()

    # LogViewerPanel + RosLogViewer tests (F5) — pure C++ logic, no ROS2 runtime.
    # Uses gtest_main (no custom main) since no RclcppEnvironment is required.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_log_viewer.cpp)
        add_executable(unit_test_log_viewer
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_log_viewer.cpp
        )
        target_link_libraries(unit_test_log_viewer PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_log_viewer PRIVATE cxx_std_20)
        target_include_directories(unit_test_log_viewer PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_log_viewer PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_log_viewer COMMAND unit_test_log_viewer)
        set_tests_properties(unit_test_log_viewer PROPERTIES LABELS "ros2")
    endif()

    # DiagnosticsPanel tests (F6) — pure C++ logic, no ROS2 runtime needed.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_diagnostics_panel.cpp)
        add_executable(unit_test_diagnostics_panel
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_diagnostics_panel.cpp
        )
        target_link_libraries(unit_test_diagnostics_panel PRIVATE
            spectra_ros2_adapter
            GTest::gtest_main
        )
        target_compile_features(unit_test_diagnostics_panel PRIVATE cxx_std_20)
        target_include_directories(unit_test_diagnostics_panel PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_diagnostics_panel PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_diagnostics_panel COMMAND unit_test_diagnostics_panel)
        set_tests_properties(unit_test_diagnostics_panel PROPERTIES LABELS "ros2")
    endif()

    # ROS2 runtime tests share the process-global DDS graph and flake under parallel ctest.
    set(SPECTRA_ROS2_DDS_RUNTIME_TESTS
        unit_test_ros2_bridge
        unit_test_topic_discovery
        unit_test_message_introspector
        unit_test_generic_subscriber
        unit_test_topic_echo_panel
        unit_test_ros_plot_manager
        unit_test_phase_a_integration
        unit_test_bag_reader
        unit_test_bag_recorder
        unit_test_node_graph_panel
        unit_test_subplot_manager
        unit_test_phase_c_integration
        unit_test_ros_clipboard_export
        unit_test_bag_player
        unit_test_discovery_introspection
        unit_test_log_viewer
    )
    foreach(_ros2_runtime_test IN LISTS SPECTRA_ROS2_DDS_RUNTIME_TESTS)
        if(TEST ${_ros2_runtime_test})
            set_tests_properties(${_ros2_runtime_test} PROPERTIES RESOURCE_LOCK ros2_dds)
        endif()
    endforeach()

endif()

# PX4 / ULog adapter tests — only built when SPECTRA_USE_PX4 is ON.
# Pure C++ — no PX4 hardware, no MAVLink network, no ImGui context needed.
if(SPECTRA_USE_PX4)
    # ULogReader tests — ULog binary format parser.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ulog_reader.cpp)
        add_executable(unit_test_ulog_reader
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ulog_reader.cpp
        )
        target_link_libraries(unit_test_ulog_reader PRIVATE
            spectra_px4_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_ulog_reader PRIVATE cxx_std_20)
        target_include_directories(unit_test_ulog_reader PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_ulog_reader PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_ulog_reader COMMAND unit_test_ulog_reader)
        set_tests_properties(unit_test_ulog_reader PROPERTIES LABELS "px4")
    endif()

    # Px4PlotManager tests — field management and data extraction.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_plot_manager.cpp)
        add_executable(unit_test_px4_plot_manager
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_plot_manager.cpp
        )
        target_link_libraries(unit_test_px4_plot_manager PRIVATE
            spectra_px4_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_px4_plot_manager PRIVATE cxx_std_20)
        target_include_directories(unit_test_px4_plot_manager PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_px4_plot_manager PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_px4_plot_manager COMMAND unit_test_px4_plot_manager)
        set_tests_properties(unit_test_px4_plot_manager PROPERTIES LABELS "px4")
    endif()

    # Px4Bridge tests — state machine and telemetry channel ring buffer.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_bridge.cpp)
        add_executable(unit_test_px4_bridge
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_bridge.cpp
        )
        target_link_libraries(unit_test_px4_bridge PRIVATE
            spectra_px4_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_px4_bridge PRIVATE cxx_std_20)
        target_include_directories(unit_test_px4_bridge PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_px4_bridge PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_px4_bridge COMMAND unit_test_px4_bridge)
        set_tests_properties(unit_test_px4_bridge PROPERTIES LABELS "px4")
    endif()

    # Px4AppShell tests — argument parsing and offline ULog open flow.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_app_shell.cpp)
        add_executable(unit_test_px4_app_shell
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_px4_app_shell.cpp
        )
        target_link_libraries(unit_test_px4_app_shell PRIVATE
            spectra_px4_adapter
            GTest::gtest
        )
        target_compile_features(unit_test_px4_app_shell PRIVATE cxx_std_20)
        target_include_directories(unit_test_px4_app_shell PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
        target_compile_definitions(unit_test_px4_app_shell PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
        add_test(NAME unit_test_px4_app_shell COMMAND unit_test_px4_app_shell)
        set_tests_properties(unit_test_px4_app_shell PROPERTIES LABELS "px4")
    endif()
endif()

# QA Agents (interactive/manual harnesses, not registered as CTest tests)
option(SPECTRA_BUILD_QA_AGENT "Build QA stress testing agent" OFF)
if(SPECTRA_BUILD_QA_AGENT)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qa/qa_agent.cpp)
        add_executable(spectra_qa_agent qa/qa_agent.cpp)
        target_link_libraries(spectra_qa_agent PRIVATE spectra)
        target_compile_features(spectra_qa_agent PRIVATE cxx_std_20)
        target_include_directories(spectra_qa_agent PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/util
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        target_compile_definitions(spectra_qa_agent PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
            $<$<BOOL:${SPECTRA_USE_IMGUI}>:SPECTRA_USE_IMGUI>
        )
    endif()

    if(SPECTRA_USE_ROS2 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qa/ros_qa_agent.cpp)
        add_executable(spectra_ros_qa_agent qa/ros_qa_agent.cpp)
        target_link_libraries(spectra_ros_qa_agent PRIVATE spectra_ros2_adapter)
        target_compile_features(spectra_ros_qa_agent PRIVATE cxx_std_20)
        target_include_directories(spectra_ros_qa_agent PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/adapters/ros2
            ${CMAKE_CURRENT_SOURCE_DIR}/util
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        target_compile_definitions(spectra_ros_qa_agent PRIVATE
            SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
            $<$<BOOL:${SPECTRA_USE_IMGUI}>:SPECTRA_USE_IMGUI>
        )
    endif()
endif()
