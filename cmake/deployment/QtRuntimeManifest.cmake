# QtRuntimeManifest.cmake — Generate a manifest of required Qt libraries and plugins
#
# This module collects the Qt libraries, QPA platform plugins, and image-format
# plugins that Spectra's Qt desktop frontend actually needs at runtime.
# It produces:
#   - A JSON manifest file (for CI validation and license tracking)
#   - CMake variables listing the file paths for install rules
#
# Usage:
#   include(cmake/deployment/QtRuntimeManifest.cmake)
#   spectra_qt_runtime_manifest(
#       MANIFEST_OUT  /path/to/qt-runtime-manifest.json
#       LIBRARIES_OUT QT_RUNTIME_LIBRARIES
#       PLUGINS_OUT   QT_RUNTIME_PLUGINS
#   )

function(spectra_qt_runtime_manifest)
    set(options "")
    set(oneValueArgs MANIFEST_OUT LIBRARIES_OUT PLUGINS_OUT)
    set(multiValueArgs COMPONENTS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_COMPONENTS)
        set(ARG_COMPONENTS Core Gui Widgets)
    endif()

    # ── Locate Qt6 installation root ──────────────────────────────────────────
    if(NOT TARGET Qt6::Core)
        message(FATAL_ERROR "Qt6::Core target not found — QtRuntimeManifest requires SPECTRA_USE_QT=ON")
    endif()

    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qmake_executable)
        # Fallback: query Qt6::Core for its interface include dirs
        get_target_property(_qt_inc Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
        # Derive prefix from include path: <prefix>/include/QtCore -> <prefix>
        string(REGEX REPLACE "/include/QtCore.*" "" _qt_prefix "${_qt_inc}")
    else()
        execute_process(
            COMMAND ${_qmake_executable} -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE _qt_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    execute_process(
        COMMAND ${_qmake_executable} -query QT_INSTALL_LIBS
        OUTPUT_VARIABLE _qt_lib_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${_qmake_executable} -query QT_INSTALL_PLUGINS
        OUTPUT_VARIABLE _qt_plugin_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT _qt_lib_dir)
        set(_qt_lib_dir "${_qt_prefix}/lib")
    endif()
    if(NOT _qt_plugin_dir)
        set(_qt_plugin_dir "${_qt_prefix}/plugins")
    endif()

    # ── Collect required Qt libraries ─────────────────────────────────────────
    set(_qt_libs "")
    set(_manifest_libs "")

    foreach(_comp ${ARG_COMPONENTS})
        if(WIN32)
            set(_lib_name "Qt6${_comp}.dll")
            set(_lib_path "${_qt_prefix}/bin/${_lib_name}")
        elseif(APPLE)
            set(_lib_name "Qt${_comp}")
            set(_lib_path "${_qt_lib_dir}/Qt${_comp}.framework/Qt${_comp}")
            if(NOT EXISTS "${_qt_lib_dir}/Qt${_comp}.framework")
                set(_lib_path "${_qt_lib_dir}/libQt6${_comp}.${_qt_version_major}.dylib")
            endif()
        else()
            set(_lib_name "libQt6${_comp}.so.6")
            set(_lib_path "${_qt_lib_dir}/${_lib_name}")
        endif()

        if(EXISTS "${_lib_path}")
            list(APPEND _qt_libs "${_lib_path}")
            list(APPEND _manifest_libs "${_lib_name}")
        else()
            message(WARNING "QtRuntimeManifest: ${_lib_name} not found at ${_lib_path}")
        endif()
    endforeach()

    # ── Collect required QPA platform plugins ─────────────────────────────────
    set(_qt_plugins "")
    set(_manifest_plugins "")

    if(WIN32)
        set(_platform_plugin "platforms/qwindows.dll")
        list(APPEND _qt_plugins "${_qt_plugin_dir}/${_platform_plugin}")
        list(APPEND _manifest_plugins "${_platform_plugin}")
    elseif(APPLE)
        set(_platform_plugin "platforms/libqcocoa.dylib")
        list(APPEND _qt_plugins "${_qt_plugin_dir}/${_platform_plugin}")
        list(APPEND _manifest_plugins "${_platform_plugin}")
    else()
        # Linux: XCB (X11) and Wayland
        set(_platform_plugins
            "platforms/libqxcb.so"
            "platforms/libqwayland-generic.so"
            "platforms/libqwayland-egl.so"
            "wayland-decoration-client/libqwayland-generic.so"
            "wayland-shellintegration/libqwayland-generic.so"
            "wayland-shellintegration/qwayland-generic.so"
            "wayland-client-decoration/libqwayland-generic.so"
        )
        foreach(_p ${_platform_plugins})
            set(_full_path "${_qt_plugin_dir}/${_p}")
            if(EXISTS "${_full_path}")
                list(APPEND _qt_plugins "${_full_path}")
                list(APPEND _manifest_plugins "${_p}")
            endif()
        endforeach()

        # Also check for wayland shell integration variants
        file(GLOB _wayland_shell_plugins "${_qt_plugin_dir}/wayland-shell-integration/*.so")
        foreach(_p ${_wayland_shell_plugins})
            file(RELATIVE_PATH _rel "${_qt_plugin_dir}" "${_p}")
            list(APPEND _qt_plugins "${_p}")
            list(APPEND _manifest_plugins "${_rel}")
        endforeach()
    endif()

    # ── Collect image-format plugins (only those Spectra uses) ────────────────
    set(_image_plugins "")
    if(WIN32)
        set(_image_plugin_names "qico.dll" "qsvg.dll" "qjpeg.dll" "qgif.dll" "qpng.dll")
    elseif(APPLE)
        set(_image_plugin_names "libqico.dylib" "libqsvg.dylib" "libqjpeg.dylib" "libqgif.dylib" "libqpng.dylib")
    else()
        set(_image_plugin_names "libqico.so" "libqsvg.so" "libqjpeg.so" "libqgif.so" "libqpng.so")
    endif()

    foreach(_p ${_image_plugin_names})
        set(_full_path "${_qt_plugin_dir}/imageformats/${_p}")
        if(EXISTS "${_full_path}")
            list(APPEND _qt_plugins "${_full_path}")
            list(APPEND _manifest_plugins "imageformats/${_p}")
        endif()
    endforeach()

    # ── Generate JSON manifest ────────────────────────────────────────────────
    set(_manifest_json "{\n")
    set(_manifest_json "${_manifest_json}  \"qt_version\": \"${Qt6_VERSION}\",\n")
    set(_manifest_json "${_manifest_json}  \"qt_prefix\": \"${_qt_prefix}\",\n")
    set(_manifest_json "${_manifest_json}  \"libraries\": [\n")
    set(_first TRUE)
    foreach(_lib ${_manifest_libs})
        if(_first)
            set(_first FALSE)
        else()
            set(_manifest_json "${_manifest_json},\n")
        endif()
        set(_manifest_json "${_manifest_json}    \"${_lib}\"")
    endforeach()
    set(_manifest_json "${_manifest_json}\n  ],\n")
    set(_manifest_json "${_manifest_json}  \"plugins\": [\n")
    set(_first TRUE)
    foreach(_plug ${_manifest_plugins})
        if(_first)
            set(_first FALSE)
        else()
            set(_manifest_json "${_manifest_json},\n")
        endif()
        set(_manifest_json "${_manifest_json}    \"${_plug}\"")
    endforeach()
    set(_manifest_json "${_manifest_json}\n  ]\n")
    set(_manifest_json "${_manifest_json}}\n")

    if(ARG_MANIFEST_OUT)
        file(WRITE "${ARG_MANIFEST_OUT}" "${_manifest_json}")
        message(STATUS "Qt runtime manifest written to ${ARG_MANIFEST_OUT}")
    endif()

    if(ARG_LIBRARIES_OUT)
        set(${ARG_LIBRARIES_OUT} "${_qt_libs}" PARENT_SCOPE)
    endif()
    if(ARG_PLUGINS_OUT)
        set(${ARG_PLUGINS_OUT} "${_qt_plugins}" PARENT_SCOPE)
    endif()

    message(STATUS "QtRuntimeManifest: ${_qt_prefix} (Qt ${Qt6_VERSION})")
    message(STATUS "  Libraries: ${_manifest_libs}")
    message(STATUS "  Plugins: ${_manifest_plugins}")
endfunction()
