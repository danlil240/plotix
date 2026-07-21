# DeployQtWindows.cmake — Windows-specific Qt runtime deployment for Spectra
#
# Uses windeployqt to collect all required Qt DLLs and plugins, then
# filters the result to include only what Spectra needs.
#
# Usage:
#   include(cmake/deployment/DeployQtWindows.cmake)
#   spectra_deploy_qt_windows(
#       TARGET       spectra-qt-app
#       DESTINATION  bin
#   )

function(spectra_deploy_qt_windows)
    set(options "")
    set(oneValueArgs TARGET DESTINATION)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "spectra_deploy_qt_windows: TARGET is required")
    endif()
    if(NOT ARG_DESTINATION)
        set(ARG_DESTINATION "bin")
    endif()

    # Find windeployqt
    if(TARGET Qt6::qmake)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            REQUIRED
        )
    else()
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt REQUIRED)
    endif()

    # Run windeployqt as a post-install step
    install(CODE "
        set(WINDEPLOYQT \"${WINDEPLOYQT_EXECUTABLE}\")
        set(DESTDIR \"\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}\")
        execute_process(
            COMMAND \"\${WINDEPLOYQT}\"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                --no-virtualkeyboard
                --release
                --verbose 1
                --dir \"\${DESTDIR}\"
                \"\${DESTDIR}/${ARG_TARGET}.exe\"
            RESULT_VARIABLE _deploy_result
        )
        if(NOT _deploy_result EQUAL 0)
            message(WARNING \"windeployqt returned \${_deploy_result} — check output\")
        endif()

        # Remove unnecessary Qt DLLs that windeployqt may have copied
        file(GLOB _extra_dlls
            \"\${DESTDIR}/Qt6Network.dll\"
            \"\${DESTDIR}/Qt6Sql.dll\"
            \"\${DESTDIR}/Qt6Test.dll\"
            \"\${DESTDIR}/Qt6Xml.dll\"
            \"\${DESTDIR}/Qt6PrintSupport.dll\"
            \"\${DESTDIR}/Qt6OpenGL.dll\"
            \"\${DESTDIR}/Qt6OpenGLWidgets.dll\"
            \"\${DESTDIR}/Qt6DBus.dll\"
        )
        foreach(_dll \${_extra_dlls})
            if(EXISTS \"\${_dll}\")
                file(REMOVE \"\${_dll}\")
                message(STATUS \"Removed unnecessary \${_dll}\")
            endif()
        endforeach()
    " COMPONENT spectra_qt_runtime)

    message(STATUS "DeployQtWindows: windeployqt -> ${ARG_DESTINATION}")
endfunction()
