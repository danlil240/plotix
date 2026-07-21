# DeployQtLinux.cmake — Linux-specific Qt runtime deployment for Spectra
#
# Installs Qt libraries and QPA plugins into a private runtime directory
# (e.g. /usr/lib/spectra/qt/) with relative RPATH so the Spectra Qt app
# finds its bundled Qt without polluting the system.
#
# Usage (after calling QtRuntimeManifest):
#   include(cmake/deployment/DeployQtLinux.cmake)
#   spectra_deploy_qt_linux(
#       LIBRARIES   ${QT_RUNTIME_LIBRARIES}
#       PLUGINS     ${QT_RUNTIME_PLUGINS}
#       DESTINATION lib/spectra/qt
#   )

function(spectra_deploy_qt_linux)
    set(options "")
    set(oneValueArgs DESTINATION RPATH_ORIGIN)
    set(multiValueArgs LIBRARIES PLUGINS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_DESTINATION)
        set(ARG_DESTINATION "lib/spectra/qt")
    endif()

    if(NOT ARG_RPATH_ORIGIN)
        set(ARG_RPATH_ORIGIN "$ORIGIN/lib")
    endif()

    # ── Install Qt libraries into private runtime ─────────────────────────────
    foreach(_lib ${ARG_LIBRARIES})
        if(EXISTS "${_lib}")
            # Resolve symlinks to get the real file
            get_filename_component(_real_lib "${_lib}" REALPATH)
            get_filename_component(_lib_name "${_lib}" NAME)

            # Install the real .so file (e.g. libQt6Core.so.6.8.0)
            install(FILES "${_real_lib}"
                DESTINATION ${ARG_DESTINATION}/lib
                RENAME "${_lib_name}"
                COMPONENT spectra_qt_runtime
            )

            # Also install versioned symlinks (libQt6Core.so.6, libQt6Core.so.6.8)
            get_filename_component(_real_name "${_real_lib}" NAME)
            # Create symlink targets: .so.6 -> .so.6.8.0, .so.6.8 -> .so.6.8.0
            string(REGEX REPLACE "\\.so\\.[0-9]+\\.[0-9]+\\.[0-9]+$" ".so.6" _link_major "${_real_name}")
            string(REGEX REPLACE "\\.so\\.[0-9]+\\.[0-9]+\\.[0-9]+$" ".so.6.8" _link_minor "${_real_name}")

            install(CODE "
                set(_dest \"\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}/lib\")
                file(MAKE_DIRECTORY \"\${_dest}\")
                if(EXISTS \"\${_dest}/${_real_name}\")
                    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
                        \"${_real_name}\" \"\${_dest}/${_link_major}\")
                    execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
                        \"${_real_name}\" \"\${_dest}/${_link_minor}\")
                endif()
            " COMPONENT spectra_qt_runtime)
        endif()
    endforeach()

    # ── Install QPA plugins preserving directory structure ────────────────────
    foreach(_plugin ${ARG_PLUGINS})
        if(EXISTS "${_plugin}")
            # Determine relative path within Qt plugins directory
            # We need to preserve: platforms/, imageformats/, wayland-*/
            get_filename_component(_plugin_dir "${_plugin}" DIRECTORY)
            get_filename_component(_plugin_dir_name "${_plugin_dir}" NAME)
            get_filename_component(_plugin_name "${_plugin}" NAME)

            install(FILES "${_plugin}"
                DESTINATION ${ARG_DESTINATION}/plugins/${_plugin_dir_name}
                COMPONENT spectra_qt_runtime
            )
        endif()
    endforeach()

    # ── Install qt.conf to redirect Qt to the private runtime ─────────────────
    # qt.conf tells Qt to look for plugins and libraries relative to the
    # executable location, not the system paths.
    set(_qt_conf_content
"[Paths]
Prefix = ../lib/spectra/qt
Libraries = lib
Plugins = plugins
"
    )
    install(CODE "
        file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qt.conf\" \"${_qt_conf_content}\")
    " COMPONENT spectra_qt_runtime)

    message(STATUS "DeployQtLinux: private Qt runtime -> ${ARG_DESTINATION}")
endfunction()
