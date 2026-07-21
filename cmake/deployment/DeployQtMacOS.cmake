# DeployQtMacOS.cmake — macOS-specific Qt + MoltenVK deployment for Spectra
#
# Uses macdeployqt to collect Qt frameworks, then adds MoltenVK
# and validates the app bundle structure.
#
# Usage:
#   include(cmake/deployment/DeployQtMacOS.cmake)
#   spectra_deploy_qt_macos(
#       APP_BUNDLE   Spectra.app
#       DESTINATION  .
#       MOLTENVK_LIB /path/to/libMoltenVK.dylib
#   )

function(spectra_deploy_qt_macos)
    set(options "")
    set(oneValueArgs APP_BUNDLE DESTINATION MOLTENVK_LIB)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_APP_BUNDLE)
        message(FATAL_ERROR "spectra_deploy_qt_macos: APP_BUNDLE is required")
    endif()
    if(NOT ARG_DESTINATION)
        set(ARG_DESTINATION ".")
    endif()

    # Find macdeployqt
    if(TARGET Qt6::qmake)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake}" DIRECTORY)
        find_program(MACDEPLOYQT_EXECUTABLE
            NAMES macdeployqt
            HINTS "${_qt_bin_dir}"
            REQUIRED
        )
    else()
        find_program(MACDEPLOYQT_EXECUTABLE NAMES macdeployqt REQUIRED)
    endif()

    # Run macdeployqt
    install(CODE "
        set(MACDEPLOYQT \"${MACDEPLOYQT_EXECUTABLE}\")
        set(APPDIR \"\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}/${ARG_APP_BUNDLE}\")

        # Run macdeployqt to fix framework paths and copy Qt frameworks
        execute_process(
            COMMAND \"\${MACDEPLOYQT}\"
                \"\${APPDIR}\"
                -verbose=1
                -always-overwrite
            RESULT_VARIABLE _deploy_result
        )
        if(NOT _deploy_result EQUAL 0)
            message(WARNING \"macdeployqt returned \${_deploy_result}\")
        endif()

        # ── Bundle MoltenVK ───────────────────────────────────────────────
        set(MOLTENVK_LIB \"${ARG_MOLTENVK_LIB}\")
        if(MOLTENVK_LIB AND EXISTS \"\${MOLTENVK_LIB}\")
            set(_mvk_dest \"\${APPDIR}/Contents/Frameworks\")
            file(MAKE_DIRECTORY \"\${_mvk_dest}\")
            file(COPY \"\${MOLTENVK_LIB}\" DESTINATION \"\${_mvk_dest}\")

            # Fix install name to use @rpath
            execute_process(COMMAND install_name_tool
                -id \"@rpath/libMoltenVK.dylib\"
                \"\${_mvk_dest}/libMoltenVK.dylib\")

            # Add rpath to the executable
            set(_exe \"\${APPDIR}/Contents/MacOS/Spectra\")
            if(EXISTS \"\${_exe}\")
                execute_process(COMMAND install_name_tool
                    -add_rpath \"@executable_path/../Frameworks\"
                    \"\${_exe}\")
            endif()

            message(STATUS \"Bundled MoltenVK into \${_mvk_dest}\")
        else()
            message(WARNING \"MoltenVK library not found at \${MOLTENVK_LIB} — macOS Vulkan will not work\")
        endif()

        # ── Validate bundle structure ─────────────────────────────────────
        set(_frameworks_dir \"\${APPDIR}/Contents/Frameworks\")
        if(NOT EXISTS \"\${_frameworks_dir}/QtCore.framework\")
            message(WARNING \"QtCore.framework not found in bundle — Qt deployment may have failed\")
        endif()
        set(_plugins_dir \"\${APPDIR}/Contents/PlugIns\")
        if(NOT EXISTS \"\${_plugins_dir}/platforms/libqcocoa.dylib\")
            message(WARNING \"libqcocoa.dylib not found — Qt platform plugin missing\")
        endif()
    " COMPONENT spectra_qt_runtime)

    message(STATUS "DeployQtMacOS: macdeployqt + MoltenVK -> ${ARG_APP_BUNDLE}")
endfunction()
