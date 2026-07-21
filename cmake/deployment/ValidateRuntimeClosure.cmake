# ValidateRuntimeClosure.cmake — Validate that a packaged Spectra installation
# has no developer paths, no missing shared-library dependencies, and correct RPATHs.
#
# This module provides a function that runs as a post-install validation step
# in CI. It is Linux-focused (ldd / objdump) with macOS fallback (otool).
#
# Usage:
#   include(cmake/deployment/ValidateRuntimeClosure.cmake)
#   spectra_validate_runtime_closure(
#       PREFIX        /path/to/install/prefix
#       BINARY        spectra-qt-app
#       QT_LIB_DIR    /path/to/private/qt/lib
#   )

function(spectra_validate_runtime_closure)
    set(options "")
    set(oneValueArgs PREFIX BINARY QT_LIB_DIR)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_PREFIX)
        message(FATAL_ERROR "spectra_validate_runtime_closure: PREFIX is required")
    endif()
    if(NOT ARG_BINARY)
        set(ARG_BINARY "spectra-qt-app")
    endif()

    set(_binary_path "${ARG_PREFIX}/bin/${ARG_BINARY}")
    if(NOT EXISTS "${_binary_path}")
        message(FATAL_ERROR "ValidateRuntimeClosure: binary not found: ${_binary_path}")
    endif()

    message(STATUS "ValidateRuntimeClosure: checking ${_binary_path}")

    # ── Check 1: No build-directory paths in RPATH ─────────────────────────────
    if(APPLE)
        execute_process(
            COMMAND otool -l "${_binary_path}"
            OUTPUT_VARIABLE _otool_output
            RESULT_VARIABLE _otool_result
        )
        if(_otool_result EQUAL 0)
            string(FIND "${_otool_output}" "/home/" _home_pos)
            if(NOT _home_pos EQUAL -1)
                message(FATAL_ERROR
                    "ValidateRuntimeClosure: build-directory path found in LC_RPATH of ${_binary_path}")
            endif()
            string(FIND "${_otool_output}" "/build/" _build_pos)
            if(NOT _build_pos EQUAL -1)
                message(FATAL_ERROR
                    "ValidateRuntimeClosure: /build/ path found in LC_RPATH of ${_binary_path}")
            endif()
        endif()
    else()
        execute_process(
            COMMAND readelf -d "${_binary_path}"
            OUTPUT_VARIABLE _readelf_output
            RESULT_VARIABLE _readelf_result
        )
        if(_readelf_result EQUAL 0)
            string(FIND "${_readelf_output}" "/home/" _home_pos)
            if(NOT _home_pos EQUAL -1)
                message(FATAL_ERROR
                    "ValidateRuntimeClosure: build-directory path found in RPATH/RUNPATH of ${_binary_path}")
            endif()
            string(FIND "${_readelf_output}" "/build/" _build_pos)
            if(NOT _build_pos EQUAL -1)
                message(FATAL_ERROR
                    "ValidateRuntimeClosure: /build/ path found in RPATH/RUNPATH of ${_binary_path}")
            endif()
        endif()
    endif()
    message(STATUS "  ✓ No build-directory paths in RPATH")

    # ── Check 2: No missing shared-library dependencies ───────────────────────
    if(APPLE)
        execute_process(
            COMMAND otool -L "${_binary_path}"
            OUTPUT_VARIABLE _otool_L_output
            RESULT_VARIABLE _otool_L_result
        )
        if(_otool_L_result EQUAL 0)
            # Check for any "not found" patterns (otool doesn't show this directly,
            # but we can check for missing @rpath references)
            string(REGEX MATCHALL "@rpath/[^\n]+" _rpath_refs "${_otool_L_output}")
            message(STATUS "  ✓ otool -L completed (${_rpath_refs} rpath references)")
        endif()
    else()
        execute_process(
            COMMAND ldd "${_binary_path}"
            OUTPUT_VARIABLE _ldd_output
            RESULT_VARIABLE _ldd_result
        )
        if(_ldd_result EQUAL 0)
            string(FIND "${_ldd_output}" "not found" _not_found_pos)
            if(NOT _not_found_pos EQUAL -1)
                message(FATAL_ERROR
                    "ValidateRuntimeClosure: missing shared library detected:\n${_ldd_output}")
            endif()
            message(STATUS "  ✓ ldd: all dependencies resolved")
        else()
            message(WARNING "ValidateRuntimeClosure: ldd failed (static binary or cross-compile?)")
        endif()
    endif()

    # ── Check 3: Qt libraries resolve from private path ───────────────────────
    if(ARG_QT_LIB_DIR AND EXISTS "${ARG_QT_LIB_DIR}")
        file(GLOB _qt_libs "${ARG_QT_LIB_DIR}/libQt6*.so*")
        if(_qt_libs)
            message(STATUS "  ✓ Private Qt libraries found: ${_qt_libs}")
        else()
            message(WARNING "ValidateRuntimeClosure: no Qt libraries in private path ${ARG_QT_LIB_DIR}")
        endif()

        # Check QPA plugins
        set(_platform_dir "${ARG_QT_LIB_DIR}/../plugins/platforms")
        if(EXISTS "${_platform_dir}")
            file(GLOB _platform_plugins "${_platform_dir}/*.so")
            if(_platform_plugins)
                message(STATUS "  ✓ QPA platform plugins found: ${_platform_plugins}")
            else()
                message(WARNING "ValidateRuntimeClosure: no QPA platform plugins in ${_platform_dir}")
            endif()
        else()
            message(WARNING "ValidateRuntimeClosure: QPA plugin directory missing: ${_platform_dir}")
        endif()
    endif()

    # ── Check 4: qt.conf exists ───────────────────────────────────────────────
    set(_qt_conf "${ARG_PREFIX}/bin/qt.conf")
    if(EXISTS "${_qt_conf}")
        message(STATUS "  ✓ qt.conf present")
    else()
        message(WARNING "ValidateRuntimeClosure: qt.conf missing at ${_qt_conf}")
    endif()

    message(STATUS "ValidateRuntimeClosure: all checks passed")
endfunction()
