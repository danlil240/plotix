cmake_minimum_required(VERSION 3.20)

# Read version from single source-of-truth
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/version.txt _ver_raw)
string(STRIP "${_ver_raw}" SPECTRA_VERSION)
set(SPECTRA_VERSION_STRING "${SPECTRA_VERSION}")

project(spectra VERSION ${SPECTRA_VERSION} LANGUAGES CXX)

# If a previous configure auto-sourced a ROS2 workspace, restore its
# environment variables so CMake re-runs triggered by the build still work.
include(${CMAKE_CURRENT_BINARY_DIR}/src/adapters/ros2/spectra_ros_env.cmake OPTIONAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Symlink compile_commands.json to project root for ccls / clangd indexing.
# Recreated on every configure so it never goes stale after a clean or rebuild.
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
    file(CREATE_LINK
        "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        SYMBOLIC)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(SPECTRA_HICOLOR_ICON_SIZES 16 32 48 64 128 256 512)
set(SPECTRA_HICOLOR_ICON_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/icons/hicolor)
add_custom_command(
    OUTPUT ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}/16x16/apps/spectra.png
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_hicolor_icons.py
        --input ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
        --output-dir ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}
        --name spectra.png
        --sizes ${SPECTRA_HICOLOR_ICON_SIZES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
    COMMENT "Generating hicolor icon theme PNGs"
    VERBATIM
)
add_custom_target(spectra_hicolor_icons ALL DEPENDS ${SPECTRA_HICOLOR_ICON_OUTPUT_DIR}/16x16/apps/spectra.png)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra/version.hpp
    @ONLY
)

# Embed font data into C arrays (eliminates runtime file dependency)
include(cmake/EmbedAssets.cmake)
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp)
    message("Font file Inter-Regular.ttf already embedded, skipping font embedding")
else()
    embed_binary_file(
        INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Inter-Regular.ttf
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_embedded.hpp
        VARIABLE InterFont_ttf
    )
endif()

# Embed Spectra logo icon into C array for the welcome screen
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_icon_embedded.hpp)
    message("Icon file spectra_icon.png already embedded, skipping")
else()
    embed_binary_file(
        INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/icons/spectra_icon.png
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_icon_embedded.hpp
        VARIABLE SpectraIcon_png
    )
endif()


# Feature flags
option(SPECTRA_USE_GLFW    "Enable GLFW windowing adapter"       ON)
option(SPECTRA_USE_SDL3    "Enable SDL3 windowing adapter"        OFF)
option(SPECTRA_USE_IMGUI   "Enable Dear ImGui menu bar/settings" ON)
option(SPECTRA_USE_QT      "Enable Qt adapter scaffolding"        OFF)
option(SPECTRA_USE_FFMPEG  "Enable video export via ffmpeg pipe"  ON)
option(SPECTRA_USE_EIGEN   "Enable Eigen vector adapters"         ON)
option(SPECTRA_USE_ROS2    "Enable ROS2 adapter (requires sourced ROS2 workspace)" ON)
option(SPECTRA_ROS2_BAG    "Enable rosbag2 support (requires rosbag2 packages)"    ON)
option(SPECTRA_USE_PX4     "Enable PX4 ULog adapter (ULog visualizer + MAVLink)"   ON)
option(SPECTRA_USE_WEBGPU  "Enable WebGPU/wasm rendering backend (requires Dawn or wgpu-native)" OFF)
option(SPECTRA_BUILD_EXAMPLES "Build example programs"            ON)
option(SPECTRA_BUILD_QT_EXAMPLE "Build the Qt6 embed example"     OFF)
option(SPECTRA_BUILD_QT_APP     "Build the production Qt6 desktop app" OFF)
option(SPECTRA_BUILD_QT_TESTS   "Build Qt6 integration tests"        OFF)
option(SPECTRA_PACKAGE_PRIVATE_QT "Package the controlled private Qt runtime" OFF)
set(SPECTRA_DEFAULT_FRONTEND "legacy"
    CACHE STRING "Default frontend binary: legacy (GLFW/ImGui) or qt (Qt6 desktop app)")
set_property(CACHE SPECTRA_DEFAULT_FRONTEND
             PROPERTY STRINGS legacy qt)
set(SPECTRA_QT_DOCKING_PROVIDER "native"
    CACHE STRING "Qt docking provider: native | kddockwidgets | qtads")
set_property(CACHE SPECTRA_QT_DOCKING_PROVIDER
             PROPERTY STRINGS native kddockwidgets qtads)
option(SPECTRA_BUILD_TESTS    "Build unit tests and benchmarks"   ON)
option(SPECTRA_PYTHON_WHEEL  "Install backend into Python package _bin/" OFF)

# Linux wheel builds: keep code generation on portable x86_64 ISA.
if(SPECTRA_PYTHON_WHEEL
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_compile_options(-march=x86-64 -mtune=generic)
    message(STATUS "SPECTRA_PYTHON_WHEEL: forcing portable x86_64 ISA (-march=x86-64 -mtune=generic)")
endif()

# Runtime mode: inproc (single-process, all windows in one process)
#               multiproc (future: each window in its own process via IPC)
set(SPECTRA_RUNTIME_MODE "multiproc" CACHE STRING "Runtime mode: inproc | multiproc")
set_property(CACHE SPECTRA_RUNTIME_MODE PROPERTY STRINGS "inproc" "multiproc")

# Vulkan present mode:
#   FIFO      — VSync, stable frame pacing, guaranteed by spec (default)
#   MAILBOX   — Low-latency + tear-free if available, may over-queue on resize
#   IMMEDIATE — No VSync, tearing possible, shows max FPS
set(SPECTRA_PRESENT_MODE "FIFO" CACHE STRING "Vulkan present mode: FIFO | MAILBOX | IMMEDIATE")
set_property(CACHE SPECTRA_PRESENT_MODE PROPERTY STRINGS "FIFO" "MAILBOX" "IMMEDIATE")

# Skip git network checks when FetchContent source dirs already exist.
# On a clean checkout these are absent so the download still happens normally.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

message(STATUS "SPECTRA_RUNTIME_MODE: ${SPECTRA_RUNTIME_MODE}")
message(STATUS "SPECTRA_PRESENT_MODE: ${SPECTRA_PRESENT_MODE}")

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Shader compilation (GLSL -> SPIR-V)
include(cmake/CompileShaders.cmake)

# WGSL shader embedding — always generated when the wgsl source dir exists.
# This allows the WebGPU shader tests to run even when SPECTRA_USE_WEBGPU=OFF
# (e.g. in CI without Dawn), because the embedded sources are plain strings.
set(WGSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/wgsl)
set(WGSL_HEADER     ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_wgsl.hpp)
if(EXISTS ${WGSL_SOURCE_DIR})
    file(GLOB _wgsl_inputs "${WGSL_SOURCE_DIR}/*.wgsl")
    if(_wgsl_inputs)
        add_custom_command(
            OUTPUT  ${WGSL_HEADER}
            COMMAND ${CMAKE_COMMAND}
                -DWGSL_SOURCE_DIR=${WGSL_SOURCE_DIR}
                -DOUTPUT_HEADER=${WGSL_HEADER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWGSLShaders.cmake
            DEPENDS ${_wgsl_inputs}
            COMMENT "Embedding WGSL shader sources into shader_wgsl.hpp"
            VERBATIM
        )
        add_custom_target(spectra_wgsl_shaders DEPENDS ${WGSL_HEADER})
    endif()
endif()

# FlatBuffers schema compilation (IPC protocol v2)
include(cmake/CompileFlatBuffers.cmake)

# Collect source files
file(GLOB_RECURSE SPECTRA_CORE_SOURCES   src/core/*.cpp)
file(GLOB_RECURSE SPECTRA_RENDER_SOURCES src/render/*.cpp)
file(GLOB_RECURSE SPECTRA_PLATFORM_SOURCES src/platform/*.cpp)
file(GLOB_RECURSE SPECTRA_ANIM_SOURCES   src/anim/*.cpp)
file(GLOB_RECURSE SPECTRA_IO_SOURCES     src/io/*.cpp)
file(GLOB_RECURSE SPECTRA_DATA_SOURCES  src/data/*.cpp)
file(GLOB_RECURSE SPECTRA_MATH_SOURCES  src/math/*.cpp)
file(GLOB_RECURSE SPECTRA_IPC_SOURCES   src/ipc/*.cpp)
file(GLOB SPECTRA_DAEMON_SOURCES
    src/daemon/session_graph.cpp
    src/daemon/process_manager.cpp
    src/daemon/figure_model.cpp
    src/daemon/topic_registry.cpp
)

# UI sources (non-ImGui) — guarded so parallel agents don't break each other's builds.
# app.cpp is always present; others are included only if they exist on disk.
file(GLOB SPECTRA_UI_SOURCES src/ui/app/app.cpp src/ui/app/app_inproc.cpp src/ui/app/app_multiproc.cpp src/ui/app/app_step.cpp src/app/application_services.cpp src/app/imgui_frontend_services.cpp)
foreach(_ui_src
    src/app/inproc_topic_server.cpp
    src/ui/app/window_ui_context_builder.cpp
    src/ui/app/window_manager_bootstrap.cpp
    src/ui/app/window_ui_context_runtime.cpp
    src/ui/input/input.cpp
    src/ui/input/input_pan_zoom.cpp
    src/ui/input/input_select.cpp
    src/ui/input/input_measure.cpp
    src/ui/input/input_annotate.cpp
    src/ui/layout/layout_manager.cpp
    src/ui/figures/tab_bar.cpp
    src/ui/animation/animation_controller.cpp
    src/ui/input/gesture_recognizer.cpp
    src/ui/animation/transition_engine.cpp
    src/ui/figures/figure_manager.cpp
    src/ui/viewmodel/figure_view_model.cpp
    src/ui/viewmodel/axes_view_model.cpp
    src/ui/viewmodel/series_view_model.cpp
    src/ui/animation/timeline_editor.cpp
    src/ui/animation/recording_export.cpp
    src/ui/docking/split_view.cpp
    src/ui/docking/dock_system.cpp
    src/ui/panel/panel_detach_controller.cpp
    src/ui/shell/panel.cpp
    src/ui/shell/panel_registry.cpp
    src/ui/shell/menu_bar.cpp
    src/ui/shell/nav_rail.cpp
    src/ui/shell/status_bar.cpp
    src/ui/shell/canvas_host.cpp
    src/ui/shell/app_shell.cpp
    src/ui/shell/spectra_app_shell.cpp
    src/ui/shell/shell_style.cpp
    src/ui/data/axis_link.cpp
    src/ui/animation/keyframe_interpolator.cpp
    src/ui/animation/camera_animator.cpp
    src/ui/animation/animation_curve_editor.cpp
    src/ui/animation/mode_transition.cpp
    src/ui/imgui/axes3d_renderer.cpp
    src/ui/window/window_manager.cpp
    src/ui/window/window_glfw_callbacks.cpp
    src/ui/window/window_lifecycle.cpp
    src/ui/window/window_figure_ops.cpp
    src/ui/app/window_runtime.cpp
    src/ui/app/session_runtime.cpp
    src/ui/figures/figure_registry.cpp
    src/ui/data/csv_loader.cpp
    src/ui/data/clipboard_export.cpp
    src/ui/data/html_table_export.cpp
    src/ui/accessibility/sonification.cpp
    src/ui/accessibility/accessible_summary.cpp
    src/ui/native_dialog_policy.cpp
    src/ui/ui_interaction_log.cpp
    src/ui/workspace/figure_serializer.cpp
    src/ui/commands/series_clipboard.cpp
    src/ui/overlay/data_editor.cpp
    src/ui/overlay/custom_transform_dialog.cpp
    src/ui/overlay/plot_overlay_dialog.cpp
    src/ui/plot/plot_annotations.cpp
    src/ui/overlay/overlay_registry.cpp
    src/adapters/data_source_registry.cpp
    src/embed/embed_surface.cpp
    src/embed/spectra_embed_c.cpp
    src/ui/automation/automation_server.cpp
    src/ui/automation/automation_dispatch.cpp
    src/ui/automation/mcp_server.cpp
    src/ui/automation/handlers/handlers_command.cpp
    src/ui/automation/handlers/handlers_input.cpp
    src/ui/automation/handlers/handlers_figure.cpp
    src/ui/automation/handlers/handlers_capture.cpp
    src/ui/automation/handlers/handlers_window.cpp
    src/ui/automation/handlers/handlers_utility.cpp
    src/ui/automation/handlers/handlers_fuzz.cpp
    src/ui/commands/command_registry.cpp
    src/ui/commands/shortcut_manager.cpp
    src/ui/commands/undo_manager.cpp
    src/ui/commands/shortcut_config.cpp
    src/ui/settings/settings_store.cpp
    src/ui/workspace/workspace.cpp
    src/ui/workspace/workspace_autosave.cpp
    src/ui/workspace/plugin_api.cpp
    src/ui/workspace/plugin_ui_schema.cpp
    src/ui/workspace/plugin_guard.cpp
    src/ui/workspace/plugin_manifest.cpp
    src/ui/app/register_commands.cpp
    src/ui/app/commands/command_descriptor.cpp
    src/ui/app/commands/commands_view.cpp
    src/ui/app/commands/commands_edit.cpp
    src/ui/app/commands/commands_file.cpp
    src/ui/app/commands/commands_figure.cpp
    src/ui/app/commands/commands_series.cpp
    src/ui/app/commands/commands_animation.cpp
    src/ui/app/commands/commands_panel.cpp
    src/ui/app/commands/commands_theme.cpp
    src/ui/app/commands/commands_tools.cpp
    src/ui/app/commands/commands_plot.cpp
    src/ui/app/commands/commands_data.cpp
    src/ui/app/commands/commands_app.cpp
    src/ui/theme/theme_api.cpp
    src/ui/theme/theme.cpp
)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ui_src})
        list(APPEND SPECTRA_UI_SOURCES ${_ui_src})
    endif()
endforeach()

# ─── spectra-core: headless data model library (no Vulkan, no GLFW, no ImGui) ─
# This is a real static/shared library containing the core data model, math,
# data processing, I/O export, animation, and knob manager.  It has zero GPU
# dependencies, making it suitable for headless Python consumers, CI pipelines,
# and embedding scenarios that don't need rendering.

add_library(spectra-core
    ${SPECTRA_CORE_SOURCES}
    ${SPECTRA_ANIM_SOURCES}
    ${SPECTRA_IO_SOURCES}
    ${SPECTRA_DATA_SOURCES}
    ${SPECTRA_MATH_SOURCES}
    src/ui/overlay/knob_manager.cpp
    src/ui/camera/camera.cpp
)

set_target_properties(spectra-core PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(spectra-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(spectra-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_include_directories(spectra-core SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_definitions(spectra-core PUBLIC
    $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    SPECTRA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPECTRA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPECTRA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPECTRA_VERSION_STRING="${SPECTRA_VERSION_STRING}"
)

# Windows toolchains require _USE_MATH_DEFINES to expose M_PI, M_E etc. from <cmath>.
if(WIN32)
    target_compile_definitions(spectra-core PUBLIC _USE_MATH_DEFINES)
endif()

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra-core PRIVATE -Wno-error=maybe-uninitialized)
endif()

# ─── spectra: full GPU-accelerated library (rendering + UI + IPC) ─────────────
# Links spectra-core and adds Vulkan rendering, platform windowing, IPC,
# daemon support, and the full ImGui-based UI stack.

add_library(spectra
    ${SPECTRA_RENDER_SOURCES}
    ${SPECTRA_PLATFORM_SOURCES}
    third_party/tinyfiledialogs.cpp
    ${SPECTRA_IPC_SOURCES}
    ${SPECTRA_DAEMON_SOURCES}
    ${SPECTRA_UI_SOURCES}
)

set_target_properties(spectra PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# Public include dirs come transitively from spectra-core.
target_include_directories(spectra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers
)
target_include_directories(spectra SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
    ${flatbuffers_SOURCE_DIR}/include
)

# Suppress warnings in third-party headers (stb_image.h tc16 array — GCC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(spectra PRIVATE -Wno-error=maybe-uninitialized)
endif()

target_link_libraries(spectra PUBLIC spectra-core Vulkan::Vulkan)

target_compile_definitions(spectra PUBLIC
    SPECTRA_RUNTIME_MODE_${SPECTRA_RUNTIME_MODE}
)
message("SPECTRA_VERSION_STRING: ${SPECTRA_VERSION_STRING}")
target_compile_definitions(spectra PRIVATE
    $<$<BOOL:${SPECTRA_USE_EIGEN}>:SPECTRA_USE_EIGEN>
)
target_compile_definitions(spectra PRIVATE SPECTRA_PRESENT_MODE_${SPECTRA_PRESENT_MODE})

# Ensure shaders are compiled before the library
if(TARGET spectra_shaders)
    add_dependencies(spectra spectra_shaders)
endif()

# Ensure FlatBuffers schemas are generated before the library
add_dependencies(spectra spectra_flatbuffers_gen)

# GLFW adapter (optional)
if(SPECTRA_USE_GLFW)
    # Always build GLFW from source to guarantee the latest Wayland support
    # and a consistent API version across all platforms.
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # Don't install GLFW's own headers/libs into the system prefix — they
    # conflict with libglfw3-dev on the user's machine when the spectra .deb
    # is installed.  The install and export of the glfw target is handled in
    # the Install targets section.
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
    # Bundle the GLFW static library in the spectra install so consumers
    # don't need system GLFW.  $<BUILD_INTERFACE:> scopes the build-tree
    # target; $<INSTALL_INTERFACE:> provides the exported name (spectra::glfw)
    # for the installed package.
    set(_spectra_glfw_link "$<BUILD_INTERFACE:glfw>;$<INSTALL_INTERFACE:spectra::glfw>")

    target_sources(spectra PRIVATE src/ui/window/glfw_adapter.cpp)
    # GLFW is linked PRIVATE (no public header includes glfw3.h), but the
    # static library has unresolved GLFW symbols.  $<INSTALL_INTERFACE:spectra::glfw>
    # in the PRIVATE interface ensures the exported target carries the GLFW
    # dependency for consumers.
    target_link_libraries(spectra PRIVATE ${_spectra_glfw_link})
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_GLFW)
    if(WIN32)
        target_sources(spectra PRIVATE src/platform/window_system/win32_glfw_hook.cpp)
        target_link_libraries(spectra PRIVATE comctl32)
    endif()
endif()

# SDL3 adapter (optional, mutually exclusive with GLFW)
if(SPECTRA_USE_GLFW AND SPECTRA_USE_SDL3)
    message(FATAL_ERROR "SPECTRA_USE_GLFW and SPECTRA_USE_SDL3 are mutually exclusive. "
                        "Enable only one windowing backend at a time.")
endif()

if(SPECTRA_USE_SDL3)
    find_package(SDL3 QUIET)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.0/SDL3-3.2.0.tar.gz
        )
        set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
        set(SDL_TESTS   OFF CACHE BOOL "" FORCE)
        set(SDL_VULKAN  ON  CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
        # Build SDL3 static lib with -fPIC so it can be linked into shared libraries
        # (e.g. libspectra_embed.so, Python extension modules, Qt adapters).
        if(TARGET SDL3-static)
            set_target_properties(SDL3-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()

    target_sources(spectra PRIVATE
        src/ui/window/sdl3_adapter.cpp
        src/ui/window/window_sdl3_events.cpp
        src/platform/window_system/sdl3_surface_host.cpp
    )
    # Link PRIVATE: SDL3 is an implementation detail; don't export to downstream consumers.
    target_link_libraries(spectra PRIVATE SDL3::SDL3-static)
    target_include_directories(spectra PRIVATE
        $<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_SDL3)

    # FetchContent produces real (non-IMPORTED) targets. CMake requires every target in
    # the dependency chain of an exported target to also be in the export set.
    # SDL3-static links SDL3_Headers (interface). We add both so the export validates.
    # IMPORTED targets from find_package don't have this restriction.
    if(NOT SDL3_FOUND)
        set(_sdl3_export_targets SDL3-static)
        foreach(_t SDL3_Headers SDL3main)
            if(TARGET ${_t})
                list(APPEND _sdl3_export_targets ${_t})
            endif()
        endforeach()
        install(TARGETS ${_sdl3_export_targets} EXPORT spectraTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
endif()

# Qt adapter (optional)
if(SPECTRA_USE_QT)
    find_package(Qt6 COMPONENTS Gui Widgets REQUIRED)

    add_library(spectra_qt_adapter
        src/adapters/qt/qt_runtime.cpp
        src/adapters/qt/qt_surface_host.cpp
        src/adapters/qt/spectra_vulkan_window.cpp
        src/adapters/qt/figure_canvas_widget.cpp
        src/adapters/qt/qt_frontend_services.cpp
        src/adapters/qt/qt_action_bridge.cpp
        src/adapters/qt/qt_main_window.cpp
        src/adapters/qt/qt_application.cpp
        src/adapters/qt/qt_workspace_bridge.cpp
        src/adapters/qt/qt_automation_adapter.cpp
        src/adapters/qt/qt_ipc_client.cpp
        src/adapters/qt/docking/native_qt_docking_host.cpp
        src/adapters/qt/docking/main_window_registry.cpp
        src/adapters/qt/panels/command_palette_dialog.cpp
        src/adapters/qt/panels/inspector_widget.cpp
        src/adapters/qt/panels/settings_widget.cpp
        src/adapters/qt/panels/topics_widget.cpp
        src/adapters/qt/panels/timeline_widget.cpp
        src/adapters/qt/panels/export_widget.cpp
        src/adapters/qt/panels/shortcut_widget.cpp
        src/adapters/qt/panels/transform_widget.cpp
        src/adapters/qt/panels/data_editor_widget.cpp
        src/adapters/qt/panels/accessibility_widget.cpp
        src/adapters/qt/panels/plugin_panel_widget.cpp
        src/adapters/qt/panels/plugins_widget.cpp
        src/adapters/qt/split_view_container.cpp
        src/adapters/qt/qt_overlay_draw_list.cpp
        src/adapters/qt/components/spectra_design_tokens.cpp
        src/adapters/qt/components/spectra_title_bar.cpp
        src/adapters/qt/components/spectra_app_header.cpp
        src/adapters/qt/components/spectra_menu_strip.cpp
        src/adapters/qt/components/spectra_nav_button.cpp
        src/adapters/qt/components/spectra_nav_rail.cpp
        src/adapters/qt/components/spectra_document_tab_bar.cpp
        src/adapters/qt/components/spectra_status_chip.cpp
        src/adapters/qt/components/spectra_status_bar.cpp
        src/adapters/qt/components/spectra_canvas_frame.cpp
        src/adapters/qt/components/spectra_dock_header.cpp
        src/adapters/qt/components/spectra_panel.cpp
        src/adapters/qt/components/spectra_controls.cpp
        src/adapters/qt/components/spectra_inspector_drawer.cpp
    )
    # ROS2 panel composition (requires both Qt and ROS2)
    if(SPECTRA_USE_ROS2)
        target_sources(spectra_qt_adapter PRIVATE
            src/adapters/qt/ros2/ros_panel_manager.cpp
        )
        target_link_libraries(spectra_qt_adapter PUBLIC spectra_ros2_adapter)
    endif()
    target_link_libraries(spectra_qt_adapter PUBLIC spectra Qt6::Gui Qt6::Widgets)
    target_compile_features(spectra_qt_adapter PRIVATE cxx_std_20)
    target_include_directories(spectra_qt_adapter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_compile_definitions(spectra_qt_adapter PUBLIC SPECTRA_USE_QT SPECTRA_HAS_QT6)
    set_target_properties(spectra_qt_adapter PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    # Production Qt desktop application (Phase 3+)
    # Supports both in-process and multiprocess (--socket) modes.
    if(SPECTRA_BUILD_QT_APP)
        add_executable(spectra-qt-app examples/qt_app.cpp)
        target_link_libraries(spectra-qt-app PRIVATE spectra_qt_adapter spectra Qt6::Widgets)
        target_compile_features(spectra-qt-app PRIVATE cxx_std_20)
        target_include_directories(spectra-qt-app PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
        target_compile_definitions(spectra-qt-app PRIVATE SPECTRA_HAS_QT6)
        set_target_properties(spectra-qt-app PROPERTIES
            AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON
        )
        if(SPECTRA_DEFAULT_FRONTEND STREQUAL "qt")
            set_target_properties(spectra-qt-app PROPERTIES OUTPUT_NAME spectra)
            message(STATUS "SPECTRA_DEFAULT_FRONTEND=qt: Qt app is the default 'spectra' binary")
        endif()

        # ── RPATH for private Qt runtime ───────────────────────────────────
        # When SPECTRA_PACKAGE_PRIVATE_QT is ON, the Qt app must find its
        # bundled Qt libraries at $ORIGIN/../lib/spectra/qt/lib instead of
        # the system Qt.
        if(SPECTRA_PACKAGE_PRIVATE_QT)
            if(APPLE)
                set_target_properties(spectra-qt-app PROPERTIES
                    INSTALL_RPATH "@executable_path/../lib/spectra/qt/lib"
                    BUILD_WITH_INSTALL_RPATH FALSE
                )
            else()
                set_target_properties(spectra-qt-app PROPERTIES
                    INSTALL_RPATH "$ORIGIN/../lib/spectra/qt/lib"
                    BUILD_WITH_INSTALL_RPATH FALSE
                )
            endif()
            message(STATUS "SPECTRA_PACKAGE_PRIVATE_QT: RPATH configured for private Qt runtime")
        endif()
    endif()

    # ── Private Qt runtime deployment ─────────────────────────────────────
    if(SPECTRA_PACKAGE_PRIVATE_QT)
        include(cmake/deployment/QtRuntimeManifest.cmake)
        spectra_qt_runtime_manifest(
            MANIFEST_OUT  ${CMAKE_BINARY_DIR}/qt-runtime-manifest.json
            LIBRARIES_OUT _qt_runtime_libs
            PLUGINS_OUT   _qt_runtime_plugins
            COMPONENTS    Core Gui Widgets
        )

        if(WIN32)
            include(cmake/deployment/DeployQtWindows.cmake)
            spectra_deploy_qt_windows(
                TARGET       spectra-qt-app
                DESTINATION  ${CMAKE_INSTALL_BINDIR}
            )
        elseif(APPLE)
            include(cmake/deployment/DeployQtMacOS.cmake)
            # MoltenVK path can be overridden via -DMOLTENVK_LIB=/path
            spectra_deploy_qt_macos(
                APP_BUNDLE   Spectra.app
                DESTINATION  .
                MOLTENVK_LIB "${MOLTENVK_LIB}"
            )
        else()
            include(cmake/deployment/DeployQtLinux.cmake)
            spectra_deploy_qt_linux(
                LIBRARIES   ${_qt_runtime_libs}
                PLUGINS     ${_qt_runtime_plugins}
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/spectra/qt
            )
        endif()

        include(cmake/deployment/ValidateRuntimeClosure.cmake)
    endif()
endif()

# Dear ImGui (optional — requires GLFW or SDL3)
if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3))
    include(FetchContent)
    FetchContent_Declare(
        imgui
        URL        https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6-docking.tar.gz
        URL_HASH   SHA256=5e84cdaa6a6041586a0d11a3071b749734a0439d66fdbdad37ae5b27e37d396c
    )
    FetchContent_MakeAvailable(imgui)

    # ImGui core (always present when ImGui is enabled)
    if(SPECTRA_USE_GLFW)
        set(_imgui_window_backend_src ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    elseif(SPECTRA_USE_SDL3)
        set(_imgui_window_backend_src ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
    endif()
    target_sources(spectra PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${_imgui_window_backend_src}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        src/ui/imgui/imgui_integration.cpp
        src/ui/imgui/imgui_command_bar.cpp
        src/ui/imgui/imgui_panels.cpp
        src/ui/imgui/imgui_pane_tabs.cpp
        src/ui/imgui/imgui_animation.cpp
        src/ui/imgui/imgui_selection.cpp
        src/ui/imgui/imgui_dialogs.cpp
        src/ui/imgui/imgui_preview.cpp
    )

    # GCC 14 warns on an upstream ImGui backend memset pattern; keep -Werror for
    # project code while relaxing this one third-party source file.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-error=class-memaccess"
        )
    endif()

    # ImGui-dependent UI sources — guarded so parallel agents don't break the build.
    # When adding a new .cpp that depends on ImGui, add an if(EXISTS) block here.
    foreach(_imgui_src
        src/ui/theme/icons.cpp
        src/ui/imgui/widgets.cpp
        src/ui/overlay/inspector.cpp
        src/ui/overlay/tooltip.cpp
        src/ui/overlay/crosshair.cpp
        src/ui/overlay/data_marker.cpp
        src/ui/overlay/annotation.cpp
        src/ui/overlay/data_interaction.cpp
        src/ui/overlay/imgui_overlay_draw_list.cpp
        src/ui/input/region_select.cpp
        src/ui/overlay/legend_interaction.cpp
        src/ui/input/box_zoom_overlay.cpp
        src/ui/commands/command_palette.cpp
        src/ui/figures/tab_drag_controller.cpp
        src/ui/topics/topics_panel.cpp
        src/ui/settings/settings_panel.cpp
    )
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_imgui_src})
            target_sources(spectra PRIVATE ${_imgui_src})
        endif() 
    endforeach()
    target_include_directories(spectra PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    if(SPECTRA_USE_SDL3)
        target_include_directories(spectra PRIVATE
            $<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>
        )
    endif()
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_IMGUI)
endif()

# ROS2 adapter (optional) — all ROS2 build logic lives in src/adapters/ros2/CMakeLists.txt
add_subdirectory(src/adapters/ros2)
# Humble ${std_msgs_TARGETS} genexes reference fastcdr at generate time when tests
# link spectra_ros2_adapter. find_package in the subdirectory alone is not always
# visible to add_subdirectory(tests); re-find in parent scope when ROS2 is enabled.
if(SPECTRA_USE_ROS2)
    find_package(fastcdr REQUIRED)
    find_package(fastrtps QUIET)
endif()
# Eigen adapter (optional)
if(SPECTRA_USE_EIGEN)
    find_package(Eigen3 QUIET)
    if(NOT Eigen3_FOUND)
        message(STATUS "Eigen3 not found — disabling Eigen adapters (SPECTRA_USE_EIGEN=OFF)")
        set(SPECTRA_USE_EIGEN OFF)
    else()
        target_link_libraries(spectra PUBLIC Eigen3::Eigen)
        target_compile_definitions(spectra PUBLIC SPECTRA_USE_EIGEN)
    endif()
endif()

# ─── WebGPU backend (optional) ───────────────────────────────────────────────
# Implements the Backend interface using the WebGPU C API.  Supports native
# builds with Google Dawn and Emscripten/wasm browser targets.
#
# Usage:
#   Native (Dawn):     cmake -DSPECTRA_USE_WEBGPU=ON -Ddawn_DIR=<dawn_install>/lib/cmake/dawn
#   Emscripten/wasm:   emcmake cmake -DSPECTRA_USE_WEBGPU=ON
#
# The WebGPU backend sources are conditionally compiled into the main spectra
# library when this option is ON.  No extra library target is created — the
# backend is selected at runtime by the application.
#
# WGSL shader embedding is always generated (unconditionally, above) so the
# shader content tests can run even when SPECTRA_USE_WEBGPU=OFF.
if(SPECTRA_USE_WEBGPU)
    # ── WebGPU / Dawn dependency ─────────────────────────────────────────────
    if(NOT EMSCRIPTEN)
        # Native build: require Dawn (Google's WebGPU implementation).
        # Users must set dawn_DIR or DAWN_INSTALL_DIR.
        find_package(Dawn QUIET)
        if(NOT Dawn_FOUND)
            # Try finding via pkg-config as fallback.
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(DAWN QUIET dawn webgpu_dawn)
            endif()
        endif()

        if(Dawn_FOUND OR DAWN_FOUND)
            message(STATUS "WebGPU backend enabled (Dawn found)")
        else()
            message(STATUS
                "Dawn not found — WebGPU backend enabled but you must link "
                "webgpu_dawn manually (set Dawn_DIR or DAWN_INSTALL_DIR).")
        endif()
    else()
        message(STATUS "WebGPU backend enabled (Emscripten target)")
    endif()

    # ── Add WebGPU sources to the spectra library ────────────────────────────
    target_sources(spectra PRIVATE
        src/render/webgpu/wgpu_backend.cpp
    )
    target_compile_definitions(spectra PUBLIC SPECTRA_USE_WEBGPU)

    # Ensure WGSL shaders are generated before building the backend.
    if(TARGET spectra_wgsl_shaders)
        add_dependencies(spectra spectra_wgsl_shaders)
    endif()

    # Link Dawn when found natively.
    if(NOT EMSCRIPTEN)
        if(TARGET dawn::webgpu_dawn)
            target_link_libraries(spectra PUBLIC dawn::webgpu_dawn)
        elseif(TARGET webgpu_dawn)
            target_link_libraries(spectra PUBLIC webgpu_dawn)
        elseif(DAWN_LIBRARIES)
            target_link_libraries(spectra PUBLIC ${DAWN_LIBRARIES})
            if(DAWN_INCLUDE_DIRS)
                target_include_directories(spectra PUBLIC ${DAWN_INCLUDE_DIRS})
            endif()
        endif()
    else()
        # Emscripten provides WebGPU via USE_WEBGPU linker flag.
        target_link_options(spectra PUBLIC "SHELL:-s USE_WEBGPU=1")
        target_compile_options(spectra PUBLIC "SHELL:-s USE_WEBGPU=1")
    endif()

    message(STATUS "SPECTRA_USE_WEBGPU: ON (inproc mode only; IPC/multiproc not supported on wasm)")
endif()

# PX4 / ULog adapter (optional) — standalone flight log viewer + MAVLink bridge.
# No external dependencies beyond POSIX sockets — always buildable.
if(SPECTRA_USE_PX4)
    add_library(spectra_px4_adapter
        src/adapters/px4/px4_adapter.cpp
        src/adapters/px4/ulog_reader.cpp
        src/adapters/px4/px4_bridge.cpp
        src/adapters/px4/px4_plot_manager.cpp
        src/adapters/px4/px4_app_shell.cpp
        src/adapters/px4/px4_app_shell_menus.cpp
        src/adapters/px4/ui/ulog_file_panel.cpp
        src/adapters/px4/ui/live_connection_panel.cpp
    )

    target_link_libraries(spectra_px4_adapter PUBLIC spectra)
    target_compile_features(spectra_px4_adapter PRIVATE cxx_std_20)

    target_include_directories(spectra_px4_adapter PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4
    )
    target_include_directories(spectra_px4_adapter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_compile_definitions(spectra_px4_adapter PUBLIC SPECTRA_USE_PX4)

    if(WIN32)
        target_link_libraries(spectra_px4_adapter PRIVATE ws2_32)
    endif()

    # Forward ImGui include paths so UI panels can find imgui.h.
    if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3) AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_px4_adapter PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()

    # spectra-px4 standalone executable
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4/main.cpp)
        add_executable(spectra-px4
            src/adapters/px4/main.cpp
        )
        target_link_libraries(spectra-px4 PRIVATE spectra_px4_adapter)
        target_compile_features(spectra-px4 PRIVATE cxx_std_20)
        target_include_directories(spectra-px4 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/px4
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
    endif()

    message(STATUS "PX4/ULog adapter enabled")
endif()

# ─── Convenience INTERFACE targets ────────────────────────────────────────────
# spectra-core is now a real library (defined above).  These remaining
# INTERFACE targets are thin aliases for backward compatibility.

add_library(spectra-ipc INTERFACE)
target_link_libraries(spectra-ipc INTERFACE spectra)

add_library(spectra-render INTERFACE)
target_link_libraries(spectra-render INTERFACE spectra)

# ─── Shared library for embedding (Python ctypes, C FFI) ────────────────────
# Build with: cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
option(SPECTRA_BUILD_EMBED_SHARED "Build libspectra_embed shared library for FFI" OFF)
if(SPECTRA_BUILD_EMBED_SHARED)
    add_library(spectra_embed SHARED
        src/embed/embed_surface.cpp
        src/embed/spectra_embed_c.cpp
    )
    target_link_libraries(spectra_embed PRIVATE spectra)
    target_compile_features(spectra_embed PRIVATE cxx_std_20)
    target_include_directories(spectra_embed PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    # When ImGui is enabled, the embed surface uses it for full UI rendering.
    # Forward ImGui include paths so embed_surface.cpp can find imgui headers.
    if(SPECTRA_USE_IMGUI AND (SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3) AND DEFINED imgui_SOURCE_DIR)
        target_include_directories(spectra_embed PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
    endif()
    set_target_properties(spectra_embed PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME spectra_embed
    )
    install(TARGETS spectra_embed
        LIBRARY DESTINATION lib
    )
endif()

# spectra — main application executable (welcome screen, no initial figure)
if((SPECTRA_USE_GLFW OR SPECTRA_USE_SDL3) AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp)
    add_executable(spectra-app src/app/main.cpp)
    target_link_libraries(spectra-app PRIVATE spectra)
    # glfw is PRIVATE to spectra, so its include directories don't propagate;
    # link directly so <GLFW/glfw3.h> resolves in app/main.cpp.
    if(SPECTRA_USE_GLFW AND DEFINED _spectra_glfw_link)
        target_link_libraries(spectra-app PRIVATE ${_spectra_glfw_link})
    endif()
    target_compile_features(spectra-app PRIVATE cxx_std_20)
    target_include_directories(spectra-app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    set_target_properties(spectra-app PROPERTIES OUTPUT_NAME spectra)
    if(SPECTRA_DEFAULT_FRONTEND STREQUAL "qt")
        set_target_properties(spectra-app PROPERTIES OUTPUT_NAME spectra-legacy)
        message(STATUS "SPECTRA_DEFAULT_FRONTEND=qt: legacy app renamed to 'spectra-legacy'")
    endif()
endif()

# Multi-process daemon + agent binaries (Phase 4 & 5)
if(SPECTRA_RUNTIME_MODE STREQUAL "multiproc")
    # spectra-backend daemon
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/daemon/main.cpp)
        add_executable(spectra-backend
            src/daemon/main.cpp
            src/daemon/daemon_server.cpp
            src/daemon/agent_message_handler.cpp
            src/daemon/python_message_handler.cpp
            src/daemon/topic_message_handler.cpp
            src/daemon/topic_registry.cpp
            src/daemon/heartbeat_monitor.cpp
            src/daemon/session_graph.cpp
            src/daemon/process_manager.cpp
            src/daemon/figure_model.cpp
        )
        target_link_libraries(spectra-backend PRIVATE spectra)
        target_compile_features(spectra-backend PRIVATE cxx_std_20)
        target_include_directories(spectra-backend PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
    endif()

    # spectra-window agent — uses the SAME UI stack as inproc
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/agent/main.cpp)
        add_executable(spectra-window
            src/agent/main.cpp
        )
        target_link_libraries(spectra-window PRIVATE spectra)
        # glfw is PRIVATE to the spectra library target, so its include
        # directories don't propagate here — link directly so the GLFW/glfw3.h
        # include in agent/main.cpp resolves.
        if(SPECTRA_USE_GLFW AND DEFINED _spectra_glfw_link)
            target_link_libraries(spectra-window PRIVATE ${_spectra_glfw_link})
        endif()
        target_compile_features(spectra-window PRIVATE cxx_std_20)
        target_include_directories(spectra-window PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
        if(SPECTRA_USE_IMGUI)
            target_include_directories(spectra-window PRIVATE
                ${imgui_SOURCE_DIR}
                ${imgui_SOURCE_DIR}/backends
            )
            if(SPECTRA_USE_SDL3)
                target_include_directories(spectra-window PRIVATE
                    $<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>
                )
            endif()
        endif()
    endif()
endif()

# ─── Install targets ─────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Library
install(TARGETS spectra-core spectra EXPORT spectraTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# When GLFW was built from FetchContent, add it to the export set so
# spectra::glfw is available to consumers via find_package(spectra).
if(SPECTRA_USE_GLFW AND TARGET glfw)
    install(TARGETS glfw EXPORT spectraTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/spectra
    )
endif()

# Public headers
install(DIRECTORY include/spectra/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
    FILES_MATCHING PATTERN "*.hpp"
)

# Generated version header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra
)

# Main application binary (inproc mode)
if(TARGET spectra-app)
    install(TARGETS spectra-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Qt desktop application binary
if(TARGET spectra-qt-app)
    install(TARGETS spectra-qt-app
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT spectra
    )
endif()

# Multiproc binaries
if(SPECTRA_RUNTIME_MODE STREQUAL "multiproc" AND NOT SPECTRA_PYTHON_WHEEL)
    if(TARGET spectra-backend)
        install(TARGETS spectra-backend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(TARGET spectra-window)
        install(TARGETS spectra-window RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# ROS2 adapter deployment (only when built with SPECTRA_USE_ROS2=ON)
if(TARGET spectra_ros2_adapter)
    install(TARGETS spectra_ros2_adapter
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET spectra-ros)
    install(TARGETS spectra-ros RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(EXISTS ${CMAKE_SOURCE_DIR}/sessions/presets)
        install(DIRECTORY sessions/presets/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/ros/sessions/presets
            FILES_MATCHING PATTERN "*.spectra-ros-session"
        )
    endif()
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/ros2_launch)
        install(DIRECTORY examples/ros2_launch/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/ros/launch
            FILES_MATCHING PATTERN "*.launch.py"
        )
    endif()
endif()

# PX4 adapter deployment (only when built with SPECTRA_USE_PX4=ON)
if(TARGET spectra_px4_adapter)
    install(TARGETS spectra_px4_adapter
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET spectra-px4)
    install(TARGETS spectra-px4 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Python wheel: bundle backend binary into the Python package
if(SPECTRA_PYTHON_WHEEL AND TARGET spectra-backend)
    install(TARGETS spectra-backend
        RUNTIME DESTINATION spectra/_bin
    )
endif()

# Data assets (fonts, icons)
install(FILES
    third_party/Inter-Regular.ttf
    third_party/SpectraIcons.ttf
    third_party/SpectraIcons.otf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/fonts
)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/icons/hicolor/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
    FILES_MATCHING PATTERN "spectra.png"
)
install(FILES icons/spectra.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Man page
install(FILES docs/man/spectra-backend.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

# Third-party license manifest
install(FILES packaging/LICENSES/THIRD_PARTY_LICENSES.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# Shell completions
install(FILES packaging/completions/spectra-backend.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
    RENAME spectra-backend
)
install(FILES packaging/completions/spectra-backend.zsh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
    RENAME _spectra-backend
)
install(FILES packaging/completions/spectra-backend.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
    RENAME spectra-backend.fish
)

# CMake package config (find_package(spectra) support)
install(EXPORT spectraTargets
    FILE spectraTargets.cmake
    NAMESPACE spectra::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spectraConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

# ─── CPack ───────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "spectra")
set(CPACK_PACKAGE_VENDOR "spectra-plot")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU-accelerated scientific plotting library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "danlil240")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README  ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

# Component-based packaging
set(CPACK_COMPONENTS_ALL spectra spectra_qt_runtime)
set(CPACK_COMPONENT_SPECTRA_DISPLAY_NAME "Spectra Desktop Application")
set(CPACK_COMPONENT_SPECTRA_DESCRIPTION "Qt 6 desktop frontend with Vulkan rendering")
set(CPACK_COMPONENT_SPECTRA_QT_RUNTIME_DISPLAY_NAME "Spectra Private Qt Runtime")
set(CPACK_COMPONENT_SPECTRA_QT_RUNTIME_DESCRIPTION "Bundled Qt 6.8.x libraries and QPA plugins")
set(CPACK_COMPONENT_SPECTRA_QT_RUNTIME_DEPENDS spectra)

# Debian
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# SHLIBDEPS only scans executables, not static libraries. libspectra.a has
# unresolved GLFW symbols, but GLFW 3.4 is bundled as spectra::glfw in the
# install. Eigen3 is a PUBLIC dependency (find_dependency in spectraConfig).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1 (>= 1.3.204.0)")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libvulkan-dev, libeigen3-dev, pkg-config, cmake")
if(SPECTRA_PKG_DISTRO_CODENAME)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1~${SPECTRA_PKG_DISTRO_CODENAME}")
endif()

# Debian component-specific settings
set(CPACK_DEBIAN_SPECTRA_PACKAGE_NAME "spectra")
set(CPACK_DEBIAN_SPECTRA_PACKAGE_DEPENDS "libvulkan1 (>= 1.3.204.0), spectra-qt-runtime (>= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_SPECTRA_PACKAGE_SHLIBDEPS ON)

set(CPACK_DEBIAN_SPECTRA_QT_RUNTIME_PACKAGE_NAME "spectra-qt-runtime")
set(CPACK_DEBIAN_SPECTRA_QT_RUNTIME_PACKAGE_DEPENDS "libvulkan1 (>= 1.3.204.0)")
set(CPACK_DEBIAN_SPECTRA_QT_RUNTIME_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_SPECTRA_QT_RUNTIME_PACKAGE_CONFLICTS "libqt6-core (<< 6.8)")

# RPM
set(CPACK_RPM_PACKAGE_REQUIRES "vulkan-loader >= 1.2")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Science")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)

# ─── Examples ────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ─── Tests ───────────────────────────────────────────────────────────────────
if(SPECTRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
