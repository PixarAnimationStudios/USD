#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#
include(Version)

# Copy headers to the build tree.  Under pxr/ the include paths match the
# source tree paths but elsewhere they do not. Instead we use include
# paths like rmanArgsParser/rmanArgsParser.h.  So if /pxr/ is not in the
# source tree path then copy the headers (public and private) into the
# build tree under paths of the latter scheme.
function(_copy_headers LIBRARY_NAME)
    set(options  "")
    set(oneValueArgs PREFIX)
    set(multiValueArgs FILES)
    cmake_parse_arguments(_args
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(files_copied "")
    set(hpath "${_args_PREFIX}/${LIBRARY_NAME}")
    if ("${CMAKE_CURRENT_SOURCE_DIR}" MATCHES ".*/pxr/.*")
        # Include paths under pxr/ match the source path.
        file(RELATIVE_PATH hpath "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    set(header_dest_dir "${CMAKE_BINARY_DIR}/${PXR_INSTALL_SUBDIR}/include/${hpath}")
    if( NOT "${_args_FILES}" STREQUAL "")
        set(files_copied "")
        foreach (f ${_args_FILES})
            set(infile "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
            set(outfile "${header_dest_dir}/${f}")
            get_filename_component(dir_to_create "${outfile}" PATH)
            add_custom_command(
                OUTPUT ${outfile}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${dir_to_create}"
                COMMAND ${CMAKE_COMMAND} -Dinfile="${infile}" -Doutfile="${outfile}" -P "${PROJECT_SOURCE_DIR}/cmake/macros/copyHeaderForBuild.cmake"
                MAIN_DEPENDENCY "${infile}"
                COMMENT "Copying ${f} ..."
                VERBATIM
            )
            list(APPEND files_copied ${outfile})
        endforeach()
    endif()

    # Add a headers target.
    add_custom_target(${LIBRARY_NAME}_headerfiles
        DEPENDS ${files_copied}
    )
    set_target_properties(${LIBRARY_NAME}_headerfiles
        PROPERTIES
            FOLDER "headerfiles"
    )

    # Make sure headers are installed before building the library.
    add_dependencies(${LIBRARY_NAME} ${LIBRARY_NAME}_headerfiles)
endfunction() # _copy_headers

# Converts a library name, such as _tf.so to the internal module name given
# our naming conventions, e.g. Tf
function(_get_python_module_name LIBRARY_FILENAME MODULE_NAME)
    # Library names are either something like tf.so for shared libraries
    # or _tf.pyd/_tf_d.pyd for Python module libraries.
    # We want to strip off the leading "_" and the trailing "_d".
    set(LIBNAME ${LIBRARY_FILENAME})
    if (PXR_DEFINE_BOOST_DEBUG_PYTHON_FLAG)
        string(REGEX REPLACE "_d$" "" LIBNAME ${LIBNAME})
    endif()
    string(REGEX REPLACE "^_" "" LIBNAME ${LIBNAME})
    string(SUBSTRING ${LIBNAME} 0 1 LIBNAME_FL)
    string(TOUPPER ${LIBNAME_FL} LIBNAME_FL)
    string(SUBSTRING ${LIBNAME} 1 -1 LIBNAME_SUFFIX)
    set(${MODULE_NAME}
        "${LIBNAME_FL}${LIBNAME_SUFFIX}"
        PARENT_SCOPE
    )
endfunction() # _get_python_module_name

function(_plugInfo_subst libTarget pluginToLibraryPath plugInfoPath)
    _get_resources_dir_name(PLUG_INFO_RESOURCE_PATH)
    set(PLUG_INFO_ROOT "..")
    set(PLUG_INFO_PLUGIN_NAME "pxr.${libTarget}")
    set(PLUG_INFO_LIBRARY_PATH "${pluginToLibraryPath}")

    configure_file(
        ${plugInfoPath}
        ${CMAKE_CURRENT_BINARY_DIR}/${plugInfoPath}
    )
endfunction() # _plugInfo_subst

# Install compiled python files alongside the python object,
# e.g. lib/python/pxr/Ar/__init__.pyc
function(_install_python LIBRARY_NAME)
    set(options  "")
    set(oneValueArgs "")
    set(multiValueArgs FILES)
    cmake_parse_arguments(ip
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(libPythonPrefix lib/python)
    _get_python_module_name(${LIBRARY_NAME} LIBRARY_INSTALLNAME)

    set(files_copied "")
    foreach(file ${ip_FILES})
        set(filesToInstall "")
        set(installDest
            "${libPythonPrefix}/pxr/${LIBRARY_INSTALLNAME}")

        # Only attempt to compile .py files. Files like plugInfo.json may also
        # be in this list
        if (${file} MATCHES ".py$")
            get_filename_component(file_we ${file} NAME_WE)

            # Preserve any directory prefix, just strip the extension. This
            # directory needs to exist in the binary dir for the COMMAND below
            # to work.
            get_filename_component(dir ${file} PATH)
            if (dir)
                file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${dir})
                set(file_we ${dir}/${file_we})
                set(installDest ${installDest}/${dir})
            endif()

            set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${file_we}.pyc)
            list(APPEND files_copied ${outfile})
            add_custom_command(OUTPUT ${outfile}
                COMMAND
                    ${PYTHON_EXECUTABLE}
                    ${PROJECT_SOURCE_DIR}/cmake/macros/compilePython.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/${file}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${file}
                    ${CMAKE_CURRENT_BINARY_DIR}/${file_we}.pyc
            )
            list(APPEND filesToInstall ${CMAKE_CURRENT_SOURCE_DIR}/${file})
            list(APPEND filesToInstall ${CMAKE_CURRENT_BINARY_DIR}/${file_we}.pyc)
        elseif (${file} MATCHES ".qss$")
            # XXX -- Allow anything or allow nothing?
            list(APPEND filesToInstall ${CMAKE_CURRENT_SOURCE_DIR}/${file})
        else()
            message(FATAL_ERROR "Cannot have non-Python file ${file} in PYTHON_FILES.")
        endif()

        # Note that we always install under lib/python/pxr, even if we are in
        # the third_party project. This means the import will always look like
        # 'from pxr import X'. We need to do this per-loop iteration because
        # the installDest may be different due to the presence of subdirs.
        install(
            FILES
                ${filesToInstall}
            DESTINATION
                "${installDest}"
        )
    endforeach()

    # Add the target.
    add_custom_target(${LIBRARY_NAME}_pythonfiles
        DEPENDS ${files_copied}
    )
    add_dependencies(python ${LIBRARY_NAME}_pythonfiles)

    _get_folder("_python" folder)
    set_target_properties(${LIBRARY_NAME}_pythonfiles
        PROPERTIES
            FOLDER "${folder}"
    )
endfunction() #_install_python

function(_install_resource_files NAME pluginInstallPrefix pluginToLibraryPath)
    # Resource files install into a structure that looks like:
    # lib/
    #     usd/
    #         ${NAME}/
    #             resources/
    #                 resourceFileA
    #                 subdir/
    #                     resourceFileB
    #                     resourceFileC
    #                 ...
    #
    _get_resources_dir(${pluginInstallPrefix} ${NAME} resourcesPath)

    foreach(resourceFile ${ARGN})
        # A resource file may be marked to not do any variable substitution,
        # like <src file>:no_subst, for these resources _plugInfo_subst will not
        # be called
        # A resource file may be specified like <src file>:<dst file> to
        # indicate that it should be installed to a different location in
        # the resources area. Check if this is the case.
        string(REPLACE ":" ";" resourceFile "${resourceFile}")
        list(LENGTH resourceFile n)
        if (n EQUAL 1)
           set(resourceDestFile ${resourceFile})
        elseif (n EQUAL 2)
           list(GET resourceFile 1 secondaryOption)
           list(GET resourceFile 0 resourceFile)
           if (${secondaryOption} STREQUAL "no_subst")
               set(plugInfoNoSubstitution ON)
               set(resourceDestFile ${resourceFile})
           else()
               # secondaryOption provides resourceDestFile
               set(resourceDestFile ${secondaryOption})
           endif()
        else()
           message(FATAL_ERROR
               "Failed to parse resource path ${resourceFile}")
        endif()

        get_filename_component(dirPath ${resourceDestFile} PATH)
        get_filename_component(destFileName ${resourceDestFile} NAME)

        # plugInfo.json go through an initial template substitution step files
        # install it from the binary (gen) directory specified by the full
        # path. Otherwise, use the original relative path which is relative to
        # the source directory.
        if (${destFileName} STREQUAL "plugInfo.json")
            if (DEFINED plugInfoNoSubstitution)
                # Do not substitute variables and only copy the plugInfo file
                configure_file(
                    ${resourceFile}
                    ${CMAKE_CURRENT_BINARY_DIR}/${resourceFile}
                    COPYONLY
                )
            else()
                _plugInfo_subst(${NAME} "${pluginToLibraryPath}" ${resourceFile})
            endif()
            set(resourceFile "${CMAKE_CURRENT_BINARY_DIR}/${resourceFile}")
        endif()

        install(
            FILES ${resourceFile}
            DESTINATION ${resourcesPath}/${dirPath}
            RENAME ${destFileName}
        )
    endforeach()
endfunction() # _install_resource_files

function(_install_pyside_ui_files LIBRARY_NAME)
    set(uiFiles "")
    foreach(uiFile ${ARGN})
        get_filename_component(outFileName ${uiFile} NAME_WE)
        get_filename_component(uiFilePath ${uiFile} ABSOLUTE)
        set(outFilePath "${CMAKE_CURRENT_BINARY_DIR}/${outFileName}.py")
        add_custom_command(
            OUTPUT ${outFilePath}
            COMMAND "${PYSIDEUICBINARY}"
            ARGS -o ${outFilePath} ${uiFilePath}
            MAIN_DEPENDENCY "${uiFilePath}"
            COMMENT "Generating Python for ${uiFilePath} ..."
            VERBATIM
        )
        list(APPEND uiFiles ${outFilePath})
    endforeach()

    # Add the target.
    add_custom_target(${LIBRARY_NAME}_pysideuifiles
        DEPENDS ${uiFiles}
    )
    add_dependencies(python ${LIBRARY_NAME}_pythonfiles)

    _get_folder("_pysideuifiles" folder)
    set_target_properties(
        ${LIBRARY_NAME}_pysideuifiles
        PROPERTIES
            FOLDER "${folder}"
    )

    set(libPythonPrefix lib/python)
    _get_python_module_name(${LIBRARY_NAME} LIBRARY_INSTALLNAME)

    install(
        FILES ${uiFiles}
        DESTINATION "${libPythonPrefix}/pxr/${LIBRARY_INSTALLNAME}"
    )
endfunction() # _install_pyside_ui_files

function(_classes LIBRARY_NAME)
    # Install headers to build or install prefix
    set(options PUBLIC PRIVATE)
    cmake_parse_arguments(classes
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    # If both get set, fall back to public.
    if(${classes_PUBLIC})
        set(VISIBILITY "PUBLIC")
    elseif(${classes_PRIVATE})
        set(VISIBILITY "PRIVATE")
    else()
        message(FATAL_ERROR
            "Library ${LIBRARY_NAME} has implicit visibility.  "
            "Provide PUBLIC or PRIVATE to classes() call.")
    endif()

    # Should the classes have an argument name?
    foreach(cls ${classes_UNPARSED_ARGUMENTS})
        list(APPEND ${LIBRARY_NAME}_${VISIBILITY}_HEADERS ${cls}.h)
        list(APPEND ${LIBRARY_NAME}_CPPFILES ${cls}.cpp)
    endforeach()
    set(${LIBRARY_NAME}_${VISIBILITY}_HEADERS
        ${${LIBRARY_NAME}_${VISIBILITY}_HEADERS}
        PARENT_SCOPE
    )
    set(${LIBRARY_NAME}_CPPFILES ${${LIBRARY_NAME}_CPPFILES} PARENT_SCOPE)
endfunction() # _classes

function(_get_install_dir path out)
    if (PXR_INSTALL_SUBDIR)
        set(${out} ${PXR_INSTALL_SUBDIR}/${path} PARENT_SCOPE)
    else()
        set(${out} ${path} PARENT_SCOPE)
    endif()
endfunction() # get_install_dir

function(_get_resources_dir_name output)
    set(${output}
        resources
        PARENT_SCOPE)
endfunction() # _get_resources_dir_name

function(_get_resources_dir pluginsPrefix pluginName output)
    _get_resources_dir_name(resourcesDir)
    set(${output}
        ${pluginsPrefix}/${pluginName}/${resourcesDir}
        PARENT_SCOPE)
endfunction() # _get_resources_dir

function(_get_folder suffix result)
    # XXX -- Shouldn't we set PXR_PREFIX everywhere?
    if(PXR_PREFIX)
        set(folder "${PXR_PREFIX}")
    elseif(PXR_INSTALL_SUBDIR)
        set(folder "${PXR_INSTALL_SUBDIR}")
    else()
        set(folder "misc")
    endif()
    if(suffix)
        set(folder "${folder}/${suffix}")
    endif()
    set(${result} ${folder} PARENT_SCOPE)
endfunction()

function(_pch_get_directory_property property separator output)
    get_property(value DIRECTORY PROPERTY ${property})
    if(NOT value STREQUAL "value-NOTFOUND")
        # XXX -- Need better list joining.
        if(${output})
            set(${output} "${${output}}${separator}${value}" PARENT_SCOPE)
        else()
            set(${output} "${value}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(_pch_get_target_property target property separator output)
    get_property(value TARGET ${target} PROPERTY ${property})
    if(NOT value STREQUAL "value-NOTFOUND")
        # XXX -- Need better list joining.
        if(${output})
            set(${output} "${${output}}${separator}${value}" PARENT_SCOPE)
        else()
            set(${output} "${value}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(_pch_get_property target property output)
    set(sep ";")
    if("${property}" STREQUAL "COMPILE_FLAGS")
        set(sep " ")
        set(accum "${accum}${sep}${CMAKE_CXX_FLAGS}")
        if(CMAKE_BUILD_TYPE)
            string(TOUPPER ${CMAKE_BUILD_TYPE} buildType)
            set(accum "${accum}${sep}${CMAKE_CXX_FLAGS_${buildType}}")
        endif()
    endif()
    _pch_get_directory_property(${property} "${sep}" accum)
    _pch_get_target_property(${target} ${property} "${sep}" accum)
    set(${output} "${accum}" PARENT_SCOPE)
endfunction()

function(_pxr_enable_precompiled_header TARGET_NAME)
    # Ignore if disabled.
    if(NOT PXR_ENABLE_PRECOMPILED_HEADERS)
        return()
    endif()

    set(options
    )
    set(oneValueArgs
        SOURCE_NAME
        OUTPUT_NAME_PREFIX
    )
    set(multiValueArgs
        EXCLUDE
    )
    cmake_parse_arguments(pch
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # Header to precompile.  SOURCE_NAME falls back to
    # ${PXR_PRECOMPILED_HEADER_NAME}.
    if("${pch_SOURCE_NAME}" STREQUAL "")
        set(pch_SOURCE_NAME "${PXR_PRECOMPILED_HEADER_NAME}")
    endif()
    if("${pch_SOURCE_NAME}" STREQUAL "")
        # Emergency backup name is "pch.h".
        set(pch_SOURCE_NAME "pch.h")
    endif()
    set(source_header_name ${pch_SOURCE_NAME})
    get_filename_component(source_header_name_we ${source_header_name} NAME_WE)

    # Name of file to precompile in the build directory.  The client can
    # specify a prefix for this file, allowing multiple binaries/libraries
    # in a single subdirectory to use unique precompiled headers, meaning
    # each can have different compile options.
    set(output_header_name_we "${pch_OUTPUT_NAME_PREFIX}${source_header_name_we}")
    set(output_header_name ${output_header_name_we}.h)

    # Precompiled header file name.  We choose the name that matches the
    # convention for the compiler.  That isn't necessary since we give
    # this name explicitly wherever it's needed.
    if(MSVC)
        set(precompiled_name ${output_header_name_we}.pch)
    elseif(CMAKE_COMPILER_IS_GNUCXX)
        set(precompiled_name ${output_header_name_we}.h.gch)
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        set(precompiled_name ${output_header_name_we}.h.pch)
    else()
        # Silently ignore unknown compiler.
        return()
    endif()

    # Headers live in subdirectories.
    set(rel_output_header_path "${PXR_PREFIX}/${TARGET_NAME}/${output_header_name}")
    set(abs_output_header_path "${CMAKE_BINARY_DIR}/include/${rel_output_header_path}")
    set(abs_precompiled_path ${CMAKE_BINARY_DIR}/include/${PXR_PREFIX}/${TARGET_NAME}/${precompiled_name})

    # Additional compile flags to use precompiled header.  This will be
    set(compile_flags "")
    if(MSVC)
        # Build with precompiled header (/Yu, /Fp) and automatically
        # include the header (/FI).
        set(compile_flags "/Yu\"${rel_output_header_path}\" /FI\"${rel_output_header_path}\" /Fp\"${abs_precompiled_path}\"")
    else()
        # Automatically include the header (-include) and warn if there's
        # a problem with the precompiled header.
        set(compile_flags "-Winvalid-pch -include \"${rel_output_header_path}\"")
    endif()

    # Use FALSE if we have an external precompiled header we can use.
    if(TRUE)
        if(MSVC)
            # Copy the header to precompile.
            add_custom_command(
                OUTPUT "${abs_output_header_path}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${source_header_name}" "${abs_output_header_path}"
                DEPENDS "${source_header_name}"
                COMMENT "Copying ${source_header_name}"
            )

            # Make an empty trigger file.  We need a source file to do the
            # precompilation.  This file only needs to include the header to
            # precompile but we're implicitly including that header so this
            # file can be empty.
            set(abs_output_source_path ${CMAKE_CURRENT_BINARY_DIR}/${output_header_name_we}.cpp)
            add_custom_command(
                OUTPUT "${abs_output_source_path}"
                COMMAND ${CMAKE_COMMAND} -E touch ${abs_output_source_path}
            )

            # The trigger file gets a special compile flag (/Yc).
            set_source_files_properties(${abs_output_source_path} PROPERTIES
                COMPILE_FLAGS "/Yc\"${rel_output_header_path}\" /FI\"${rel_output_header_path}\" /Fp\"${abs_precompiled_path}\""
                OBJECT_OUTPUTS "${abs_precompiled_path}"
                OBJECT_DEPENDS "${abs_output_header_path}"
            )

            # Add the header file to the target.
            target_sources(${TARGET_NAME} PRIVATE "${abs_output_header_path}")

            # Add the trigger file to the target.
            target_sources(${TARGET_NAME} PRIVATE "${abs_output_source_path}")

            # Exclude the trigger.
            list(APPEND pch_EXCLUDE ${abs_output_source_path})
        else()
            # Copy the header to precompile.
            add_custom_command(
                OUTPUT "${abs_output_header_path}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${source_header_name}" "${abs_output_header_path}"
                DEPENDS "${source_header_name}"
                COMMENT "Copying ${source_header_name}"
            )

            set(incs "$<TARGET_PROPERTY:${TARGET_NAME},INCLUDE_DIRECTORIES>")
            set(defs "$<TARGET_PROPERTY:${TARGET_NAME},COMPILE_DEFINITIONS>")
            set(opts "$<TARGET_PROPERTY:${TARGET_NAME},COMPILE_OPTIONS>")
            set(incs "$<$<BOOL:${incs}>:-I$<JOIN:${incs}, -I>>")
            set(defs "$<$<BOOL:${defs}>:-D$<JOIN:${defs}, -D>>")
            _pch_get_property(${TARGET_NAME} COMPILE_FLAGS flags)

            # Ideally we'd just put have generator expressions in the
            # COMMAND in add_custom_command().  However that will
            # write the result of the JOINs as single strings (escaping
            # spaces) and we want them as individual options.
            #
            # So we use file(GENERATE) which doesn't suffer from that
            # problem and execute the generated cmake script as the
            # COMMAND.
            file(GENERATE
                OUTPUT "$<TARGET_FILE:${TARGET_NAME}>.pchgen"
                CONTENT "execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${flags} ${opt} ${defs} ${incs} -c -x c++-header -o \"${abs_precompiled_path}\" \"${abs_output_header_path}\")"
            )

            # Command to generate the precompiled header.
            add_custom_command(
                OUTPUT "${abs_precompiled_path}"
                COMMAND ${CMAKE_COMMAND} -P "$<TARGET_FILE:${TARGET_NAME}>.pchgen"
                DEPENDS "${abs_output_header_path}"
                COMMENT "Precompiling ${source_header_name} in ${TARGET_NAME}"
            )
        endif()
    endif()

    # Update every C++ source in the target to implicitly include and
    # depend on the precompiled header.
    get_property(target_sources TARGET ${TARGET_NAME} PROPERTY SOURCES)
    foreach(source ${target_sources})
        # All target C++ sources not in EXCLUDE list.
        if(source MATCHES \\.cpp$)
            if (NOT ";${pch_EXCLUDE};" MATCHES ";${source};")
                set_source_files_properties(${source} PROPERTIES
                    COMPILE_FLAGS "${compile_flags}"
                    OBJECT_DEPENDS "${abs_precompiled_path}")
            endif()
        endif()
    endforeach()
endfunction()

# Initialize a variable to accumulate an rpath.  The origin is the
# RUNTIME DESTINATION of the target.  If not absolute it's appended
# to CMAKE_INSTALL_PREFIX.
function(_pxr_init_rpath rpathRef origin)
    if(NOT IS_ABSOLUTE ${origin})
        set(origin "${CMAKE_INSTALL_PREFIX}/${origin}")
        get_filename_component(origin "${origin}" REALPATH)
    endif()
    set(${rpathRef} "${origin}" PARENT_SCOPE)
endfunction()

# Add a relative target path to the rpath.  If target is absolute compute
# and add a relative path from the origin to the target.
function(_pxr_add_rpath rpathRef target)
    if(IS_ABSOLUTE "${target}")
        # Make target relative to $ORIGIN (which is the first element in
        # rpath when initialized with _pxr_init_rpath()).
        list(GET ${rpathRef} 0 origin)
        file(RELATIVE_PATH
            target
            "${origin}"
            "${target}"
        )
        if("x${target}" STREQUAL "x")
            set(target ".")
        endif()
    endif()
    file(TO_CMAKE_PATH "${target}" target)
    set(new_rpath "${${rpathRef}}")
    list(APPEND new_rpath "$ORIGIN/${target}")
    set(${rpathRef} "${new_rpath}" PARENT_SCOPE)
endfunction()

function(_pxr_install_rpath rpathRef NAME)
    # Get and remove the origin.
    list(GET ${rpathRef} 0 origin)
    set(rpath ${${rpathRef}})
    list(REMOVE_AT rpath 0)

    # Canonicalize and uniquify paths.
    set(final "")
    foreach(path ${rpath})
        # Replace $ORIGIN with @loader_path
        if(APPLE)
            if("${path}/" MATCHES "^[$]ORIGIN/")
                # Replace with origin path.
                string(REPLACE "$ORIGIN/" "@loader_path/" path "${path}/")
            endif()
        endif()

        # Strip trailing slashes.
        string(REGEX REPLACE "/+$" "" path "${path}")

        # Ignore paths we already have.
        if (NOT ";${final};" MATCHES ";${path};")
            list(APPEND final "${path}")
        endif()
    endforeach()

    set_target_properties(${NAME}
        PROPERTIES
            INSTALL_RPATH_USE_LINK_PATH TRUE
            INSTALL_RPATH "${final}"
    )
endfunction()

# Split the library (target) names in libs into internal-to-the-monolithic-
# library and external-of-it lists.
function(_pxr_split_libraries libs internal_result external_result)
    set(internal "")
    set(external "")
    foreach(lib ${libs})
        if(";${PXR_CORE_LIBS};" MATCHES ";${lib};")
            list(APPEND internal "${lib}")
        else()
            list(APPEND external "${lib}")
        endif()
    endforeach()
    set(${internal_result} ${internal} PARENT_SCOPE)
    set(${external_result} ${external} PARENT_SCOPE)
endfunction()

# Helper functions for _pxr_transitive_internal_libraries.
function(_pxr_transitive_internal_libraries_r libs transitive_libs)
    set(result "${${transitive_libs}}")
    foreach(lib ${libs})
        # Handle library ${lib} only if it hasn't been seen yet.
        if(NOT ";${result};" MATCHES ";${lib};")
            # Add to result.
            list(APPEND result ${lib})

            # Get the implicit link libraries.
            get_property(implicit TARGET ${lib} PROPERTY INTERFACE_LINK_LIBRARIES)

            # Discard the external libraries.
            _pxr_split_libraries("${implicit}" internal external)

            # Recurse on the internal libraries.
            _pxr_transitive_internal_libraries_r("${internal}" result)
        endif()
    endforeach()
    set(${transitive_libs} "${result}" PARENT_SCOPE)
endfunction()

function(_pxr_transitive_internal_libraries libs transitive_libs)
    # Get the transitive libs in some order.
    set(transitive "")
    _pxr_transitive_internal_libraries_r("${libs}" transitive)

    # Get the transitive libs in build order.
    set(result "")
    foreach(lib ${PXR_ALL_LIBS})
        if(";${transitive};" MATCHES ";${lib};")
            list(APPEND result "${lib}")
        endif()
    endforeach()

    # Reverse the order to get the link order.
    list(REVERSE result)
    set(${transitive_libs} "${result}" PARENT_SCOPE)
endfunction()

# This function is equivalent to target_link_libraries except it does
# a few extra things:
#
#   1) We can't call target_link_libraries() on a target that's an OBJECT
#      library but we do need the transitive definitions and include
#      directories so we manually add them.  We also manually set the
#      INTERFACE_LINK_LIBRARIES so we can use it for targets that want to
#      "link" the OBJECT library.  And we manually add a dependency.
#      This would all be a lot easier if cmake treated OBJECT libraries
#      like a STATIC or SHARED library in target_link_libraries().
#
#   2) If the target is not an OBJECT library and this is a monolithic
#      build and we're linking to core libraries then link against the
#      monolithic library instead.
#
#   3) If the target is not an OBJECT library and this is not a monolithic
#      build and we're not building shared libraries and we're linking
#      with core libraries then we must link the static libraries using
#      whole-archive functionality.  Without this any object file in a
#      static library that doesn't have any symbols used from it will not
#      be linked at all.  If the object file has global constructors with
#      side effects then those constructors and side effects will not
#      run.  We depend on these constructs (e.g. TF_REGISTRY_FUNCTION).
#
#   4) We link against PXR_MALLOC_LIBRARY and PXR_THREAD_LIBS because we
#      always want those.
#
function(_pxr_target_link_libraries NAME)
    # Split core libraries from non-core libraries.
    _pxr_split_libraries("${ARGN}" internal external)

    get_property(type TARGET ${NAME} PROPERTY TYPE)
    if("${type}" STREQUAL "OBJECT_LIBRARY")
        # Collect the definitions and include directories.
        set(finalDefs "")
        set(finalIncs "")
        set(finalSystemIncs "")
        _pxr_transitive_internal_libraries("${internal}" internal)
        foreach(lib ${internal})
            get_property(defs TARGET ${lib} PROPERTY INTERFACE_COMPILE_DEFINITIONS)
            foreach(def ${defs})
                if(NOT ";${finalDefs};" MATCHES ";${def};")
                    list(APPEND finalDefs "${def}")
                endif()
            endforeach()
            get_property(incs TARGET ${lib} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
            foreach(inc ${incs})
                if(NOT ";${finalIncs};" MATCHES ";${inc};")
                    list(APPEND finalIncs "${inc}")
                endif()
            endforeach()
            get_property(incs TARGET ${lib} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
            foreach(inc ${incs})
                if(NOT ";${finalSystemIncs};" MATCHES ";${inc};")
                    list(APPEND finalSystemIncs "${inc}")
                endif()
            endforeach()
        endforeach()

        # Collect libraries.  We must convert debug/optimized/general
        # link-type keywords to generator expressions in order to add
        # them to the INTERFACE_LINK_LIBRARIES.
        set(finalLibs "")
        set(keyword "")
        foreach(lib ${external})
            if("${lib}" STREQUAL "debug" OR "${lib}" STREQUAL "optimized")
                set(keyword ${lib})
            elseif("${lib}" STREQUAL "general")
                set(keyword "")
            elseif(lib)
                if("${keyword}" STREQUAL "debug")
                    set(keyword "")
                    set(entry "$<$<CONFIG:DEBUG>:${lib}>")
                elseif("${keyword}" STREQUAL "optimized")
                    set(keyword "")
                    set(entry "$<$<NOT:$<CONFIG:DEBUG>>:${lib}>")
                else()
                    set(entry "${lib}")
                endif()
                if(entry AND NOT ";${finalLibs};" MATCHES ";${entry};")
                    list(APPEND finalLibs "${entry}")
                endif()
            endif()
        endforeach()

        # Record the definitions, include directories and "linked" libraries.
        target_compile_definitions(${NAME} PUBLIC ${finalDefs})
        target_include_directories(${NAME} PUBLIC ${finalIncs})
        target_include_directories(${NAME} SYSTEM PUBLIC ${finalSystemIncs})
        set_property(TARGET ${NAME} PROPERTY
            INTERFACE_LINK_LIBRARIES
                ${finalLibs}
                ${PXR_MALLOC_LIBRARY}
                ${PXR_THREAD_LIBS}
        )

        # Depend on core libraries we use.
        if(internal)
            add_dependencies(${NAME} ${internal})
        endif()
    else()
        # If we use any internal libraries then just link against the
        # monolithic library.
        if(PXR_BUILD_MONOLITHIC)
            if(internal)
                if(TARGET usd_ms)
                    set(internal usd_ms)
                else()
                    set(internal usd_m)
                endif()
            endif()
        elseif(NOT BUILD_SHARED_LIBS)
            # Indicate that all symbols should be pulled in from internal
            # static libraries.  This ensures we don't drop unused symbols
            # with dynamic initialization side effects.  The exceptions are
            # any libraries explicitly static;  not only does that explicitly
            # say we don't have to worry about the dynamic initialization, but
            # also would maybe cause multiple symbol definitions if we tried
            # to get all symbols.
            #
            # On gcc use: --whole_archive LIB --no-whole-archive.
            # On clang use: -force_load LIB
            # On Windows use: /WHOLEARCHIVE:LIB
            #
            # A final complication is that we must also process transitive
            # link libraries since any transitively linked internal libraries
            # need the same treatment.
            _pxr_transitive_internal_libraries("${internal}" internal)
            set(final "")
            foreach(lib ${internal})
                if(";${PXR_STATIC_LIBS};" MATCHES ";${lib};")
                    # The library is explicitly static.
                    list(APPEND final ${lib})
                elseif(MSVC)
                    # The syntax here is -WHOLEARCHIVE[:lib] but CMake will
                    # treat that as a link flag and not "see" the library.
                    # As a result it won't replace a target with the path
                    # to the built target and it won't add a dependency.
                    #
                    # We can't simply link against the full path to the
                    # library because we CMake will not add a dependency
                    # and won't use interface link libraries and flags
                    # from the targets.  Rather than trying to add those
                    # things manually we instead link against the target
                    # and link against the full path to the built target
                    # with WHOLEARCHIVE.
                    #
                    # This ends up with the library on the link line twice.
                    # That's okay, though, because the linker will read
                    # the WHOLEARCHIVE one first and will use none of the
                    # (duplicate) symbols from the second since they're
                    # all provided by the first.  The order doesn't really
                    # matter; we pull in the whole archive first.
                    #
                    list(APPEND final -WHOLEARCHIVE:$<TARGET_FILE:${lib}>)
                    list(APPEND final ${lib})
                elseif(CMAKE_COMPILER_IS_GNUCXX)
                    list(APPEND final -Wl,--whole-archive ${lib} -Wl,--no-whole-archive)
                elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
                    list(APPEND final -Wl,-force_load ${lib})
                else()
                    # Unknown platform.
                    list(APPEND final ${lib})
                endif()
            endforeach()
            set(internal ${final})
        endif()
        target_link_libraries(${NAME}
            ${internal}
            ${external}
            ${PXR_MALLOC_LIBRARY}
            ${PXR_THREAD_LIBS}
        )
    endif()
endfunction()

# Add a python module for the target named NAME.  It implicitly links
# against the library named NAME (or the monolithic library if
# PXR_BUILD_MONOLITHIC is enabled).
function(_pxr_python_module NAME)
    set(oneValueArgs
        PRECOMPILED_HEADERS
        PRECOMPILED_HEADER_NAME
        WRAPPED_LIB_INSTALL_PREFIX
    )
    set(multiValueArgs
        CPPFILES
        PYTHON_FILES
        PYSIDE_UI_FILES
        INCLUDE_DIRS
    )
    cmake_parse_arguments(args
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # If we can't build Python modules then do nothing.
    if(NOT TARGET shared_libs)
        message(STATUS "Skipping Python module ${NAME}, shared libraries required")
        return()
    endif()

    if (WIN32 AND PXR_DEFINE_BOOST_DEBUG_PYTHON_FLAG)
        # On Windows when compiling with debug python the library must be named with _d.
        set(LIBRARY_NAME "_${NAME}_d")
    else()
        set(LIBRARY_NAME "_${NAME}")
    endif()

    # Install .py files.
    if(args_PYTHON_FILES)
        _install_python(${LIBRARY_NAME}
            FILES ${args_PYTHON_FILES}
        )
    endif()

    # Install .ui files.
    if (args_PYSIDE_UI_FILES)
        _install_pyside_ui_files(${LIBRARY_NAME} ${args_PYSIDE_UI_FILES})
    endif()

    # If no C++ files then we're done.
    if (NOT args_CPPFILES)
        return()
    endif()

    # Add the module target.
    add_library(${LIBRARY_NAME}
        SHARED
        ${args_CPPFILES}
    )
    add_dependencies(python ${LIBRARY_NAME})
    if(args_PYTHON_FILES)
        add_dependencies(${LIBRARY_NAME} ${LIBRARY_NAME}_pythonfiles)
    endif()
    if (args_PYSIDE_UI_FILES)
        add_dependencies(${LIBRARY_NAME} ${LIBRARY_NAME}_pysideuifiles)
    endif()

    # Convert the name of the library into the python module name
    # , e.g. _tf.so -> Tf. This is later used to determine the eventual
    # install location as well as for inclusion into the __init__.py's
    # __all__ list.
    _get_python_module_name(${LIBRARY_NAME} pyModuleName)

    # Accumulate Python module names.
    set_property(GLOBAL
        APPEND PROPERTY PXR_PYTHON_MODULES ${pyModuleName}
    )

    # Always install under the 'pxr' module, rather than base on the
    # project name. This makes importing consistent, e.g.
    # 'from pxr import X'. Additionally, python libraries always install
    # into the default lib install, not into the third_party subdirectory
    # or similar.
    set(libInstallPrefix "lib/python/pxr/${pyModuleName}")

    # Python modules need to be able to access their corresponding
    # wrapped library and the install lib directory.
    _pxr_init_rpath(rpath "${libInstallPrefix}")
    _pxr_add_rpath(rpath
        "${CMAKE_INSTALL_PREFIX}/${args_WRAPPED_LIB_INSTALL_PREFIX}")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${LIBRARY_NAME})

    _get_folder("_python" folder)
    set_target_properties(${LIBRARY_NAME}
        PROPERTIES
            PREFIX ""
            FOLDER "${folder}"
    )
    if(WIN32)
        # Python modules must be suffixed with .pyd on Windows.
        set_target_properties(${LIBRARY_NAME}
            PROPERTIES
                SUFFIX ".pyd"
        )
    elseif(APPLE)
        # Python modules must be suffixed with .so on Mac.
        set_target_properties(${LIBRARY_NAME}
            PROPERTIES
                SUFFIX ".so"
        )
    endif()

    target_compile_definitions(${LIBRARY_NAME}
        PRIVATE
            MFB_PACKAGE_NAME=${PXR_PACKAGE}
            MFB_ALT_PACKAGE_NAME=${PXR_PACKAGE}
            MFB_PACKAGE_MODULE=${pyModuleName}
    )

    _pxr_target_link_libraries(${LIBRARY_NAME}
        ${NAME}
        ${PXR_MALLOC_LIBRARY}
    )

    # All Python modules require support code from tf.  Linking with the
    # monolithic library will (deliberately) not pick up the dependency
    # on tf.
    add_dependencies(${LIBRARY_NAME} tf)

    # Include headers from the build directory.
    get_filename_component(
        PRIVATE_INC_DIR
        "${CMAKE_BINARY_DIR}/include"
        ABSOLUTE
    )
    if (PXR_INSTALL_SUBDIR)
        get_filename_component(
            SUBDIR_INC_DIR
            "${CMAKE_BINARY_DIR}/${PXR_INSTALL_SUBDIR}/include"
            ABSOLUTE
        )
    endif()
    target_include_directories(${LIBRARY_NAME}
        PRIVATE
            ${PRIVATE_INC_DIR}
            ${SUBDIR_INC_DIR}
    )

    # The INCLUDE_DIRS argument specifies directories containing headers
    # for third-party libraries needed by this library. We treat these
    # as system include directories so that compiler warnings from these
    # headers are ignored, since we have no control over the contents
    # of those headers.
    target_include_directories(${LIBRARY_NAME}
        SYSTEM
        PUBLIC
            ${args_INCLUDE_DIRS}
    )

    # Ensure the Python header directory is included as a system include
    # directory. This is a workaround for an issue in which Python headers 
    # unequivocally redefine macros defined in standard library headers.
    # This behavior prevents users from running strict builds with
    # PXR_STRICT_BUILD_MODE as the redefinition warnings would cause build
    # failures.
    target_include_directories(${LIBRARY_NAME}
        SYSTEM
        PUBLIC
            ${PYTHON_INCLUDE_DIRS}
    )

    install(
        TARGETS ${LIBRARY_NAME}
        LIBRARY DESTINATION ${libInstallPrefix}
        RUNTIME DESTINATION ${libInstallPrefix}
    )

    if(NOT "${PXR_PREFIX}" STREQUAL "")
        if(args_PRECOMPILED_HEADERS)
            _pxr_enable_precompiled_header(${LIBRARY_NAME}
                OUTPUT_NAME_PREFIX "py"
                SOURCE_NAME "${args_PRECOMPILED_HEADER_NAME}"
            )
        endif()
    endif()
endfunction() # pxr_python_module

# Add a library target named NAME.
function(_pxr_library NAME)
    # Argument parsing.
    set(options
    )
    set(oneValueArgs
        PREFIX
        SUBDIR
        SUFFIX
        TYPE
        PRECOMPILED_HEADERS
        PRECOMPILED_HEADER_NAME
    )
    set(multiValueArgs
        PUBLIC_HEADERS
        PRIVATE_HEADERS
        CPPFILES
        LIBRARIES
        INCLUDE_DIRS
        RESOURCE_FILES
        DOXYGEN_FILES
        LIB_INSTALL_PREFIX_RESULT
    )
    cmake_parse_arguments(args
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    #
    # Set up the target.
    #

    # Note OBJECT and PLUGIN types.
    set(isObject FALSE)
    set(isPlugin FALSE)
    if(args_TYPE STREQUAL "OBJECT" OR args_TYPE STREQUAL "OBJECT_PLUGIN")
        set(isObject TRUE)
    endif()
    if(args_TYPE STREQUAL "PLUGIN" OR args_TYPE STREQUAL "OBJECT_PLUGIN")
        set(isPlugin TRUE)
    endif()

    if(_building_core)
        # We need to distinguish core libraries.  We keep track of them here.
        get_property(help CACHE PXR_CORE_LIBS PROPERTY HELPSTRING)
        list(APPEND PXR_CORE_LIBS ${NAME})
        set(PXR_CORE_LIBS "${PXR_CORE_LIBS}" CACHE INTERNAL "${help}")

        # Keep track of core OBJECT libraries.
        if(isObject)
            get_property(help CACHE PXR_OBJECT_LIBS PROPERTY HELPSTRING)
            list(APPEND PXR_OBJECT_LIBS ${NAME})
            set(PXR_OBJECT_LIBS "${PXR_OBJECT_LIBS}" CACHE INTERNAL "${help}")
        endif()
    endif()

    # Add the target.  We also add the headers because that's the easiest
    # way to get them to appear in IDE projects.
    if(isObject)
        # When building a monolithic library we don't build individual
        # static or shared libraries.  Instead we build OBJECT libraries
        # which simply compile the sources.
        #
        # These can't be linked like other libraries and as a result we
        # don't automatically get transitive compiler definitions,
        # include directories or link libraries.  We have to do that
        # manually.  See pxr_monolithic_epilogue().
        add_library(${NAME}
            OBJECT
            ${args_CPPFILES}
            ${args_PUBLIC_HEADERS}
            ${args_PRIVATE_HEADERS}
        )

    elseif(args_TYPE STREQUAL "STATIC")
        # Building an explicitly static library.
        add_library(${NAME}
            STATIC
            ${args_CPPFILES}
            ${args_PUBLIC_HEADERS}
            ${args_PRIVATE_HEADERS}
        )

    else()
        # Building an explicitly shared library or plugin.
        add_library(${NAME}
            SHARED
            ${args_CPPFILES}
            ${args_PUBLIC_HEADERS}
            ${args_PRIVATE_HEADERS}
        )
        if(PXR_PY_UNDEFINED_DYNAMIC_LOOKUP)
            # When not explicitly linking to the python lib we need to allow
            # the linker to complete without resolving all symbols. This lets
            # python resolve at runtime, and use this to support python
            # versions built with different compilers and point versions.
            # This only needed on macOS; this is not an issue on Windows,
            # and on Linux the equivalent --allow-shlib-undefined option for ld
            # is enabled by default when creating shared libraries.
            if(APPLE)
                target_link_options(${NAME}
                    PUBLIC
                    "LINKER:SHELL:-undefined dynamic_lookup"
                )
            endif()
        endif()
    endif()

    #
    # Compute names and paths.
    #

    # Where do we install to?
    _get_install_dir("include" headerInstallDir)
    _get_install_dir("include/${PXR_PREFIX}/${NAME}" headerInstallPrefix)
    _get_install_dir("lib" libInstallPrefix)
    if(isPlugin)
        _get_install_dir("plugin" pluginInstallPrefix)
        if(NOT PXR_INSTALL_SUBDIR)
            # XXX -- Why this difference?
            _get_install_dir("plugin/usd" pluginInstallPrefix)
        endif()
        if(NOT isObject)
            # A plugin embedded in the monolithic library is found in
            # the usual library location, otherwise plugin libraries
            # are in the plugin install location.
            set(libInstallPrefix "${pluginInstallPrefix}")
        endif()
    else()
        _get_install_dir("lib/usd" pluginInstallPrefix)
    endif()
    if(args_SUBDIR)
        set(libInstallPrefix "${libInstallPrefix}/${args_SUBDIR}")
        set(pluginInstallPrefix "${pluginInstallPrefix}/${args_SUBDIR}")
    endif()
    # Return libInstallPrefix to caller.
    if(args_LIB_INSTALL_PREFIX_RESULT)
        set(${args_LIB_INSTALL_PREFIX_RESULT} "${libInstallPrefix}" PARENT_SCOPE)
    endif()

    # Names and paths passed to the compile via macros.  Paths should be
    # relative to facilitate relocating the build.
    _get_python_module_name(${NAME} pythonModuleName)
    string(TOUPPER ${NAME} uppercaseName)
    if(PXR_INSTALL_LOCATION)
        file(TO_CMAKE_PATH "${PXR_INSTALL_LOCATION}" pxrInstallLocation)
        set(pxrInstallLocation "PXR_INSTALL_LOCATION=${pxrInstallLocation}")
    endif()

    # API macros.
    set(apiPublic "")
    set(apiPrivate ${uppercaseName}_EXPORTS=1)
    if(NOT _building_monolithic AND args_TYPE STREQUAL "STATIC")
        set(apiPublic PXR_STATIC=1)
    endif()

    # Final name.
    set(libraryFilename "${args_PREFIX}${NAME}${args_SUFFIX}")
    set(pluginToLibraryPath "")

    # Figure out the relative path from this library's plugin location
    # (in the libplug sense, which applies even to non-plugins, and is
    # where we can find external resources for the library) to the
    # library's location.  This can be embedded into resource files.
    #
    # If we're building a monolithic library or individual static libraries,
    # these libraries are not separately loadable at runtime. In these cases,
    # we don't need to specify the library's location, so we leave
    # pluginToLibraryPath empty.
    if(NOT args_TYPE STREQUAL "STATIC")
   	if(NOT (";${PXR_CORE_LIBS};" MATCHES ";${NAME};" AND _building_monolithic))
            file(RELATIVE_PATH
                pluginToLibraryPath
                ${CMAKE_INSTALL_PREFIX}/${pluginInstallPrefix}/${NAME}
                ${CMAKE_INSTALL_PREFIX}/${libInstallPrefix}/${libraryFilename})
        endif()
    endif()

    #
    # Set up the compile/link.
    #

    # PIC is required by shared libraries. It's on for static libraries
    # because we'll likely link them into a shared library.
    #
    # We set PUBLIC_HEADER so we install directly from the source tree.
    # We don't want to install the headers copied to the build tree
    # because they have #line directives embedded to aid in debugging.
    _get_folder("" folder)
    set_target_properties(${NAME}
        PROPERTIES
            FOLDER "${folder}"
            POSITION_INDEPENDENT_CODE ON
            IMPORT_PREFIX "${args_PREFIX}"            
            PREFIX "${args_PREFIX}"
            SUFFIX "${args_SUFFIX}"
            PUBLIC_HEADER "${args_PUBLIC_HEADERS}"
    )

    set(pythonEnabled "PXR_PYTHON_ENABLED=1")
    if(TARGET shared_libs)
        set(pythonModulesEnabled "PXR_PYTHON_MODULES_ENABLED=1")
    endif()
    target_compile_definitions(${NAME}
        PUBLIC
            ${pythonEnabled}
            ${apiPublic}
        PRIVATE
            MFB_PACKAGE_NAME=${PXR_PACKAGE}
            MFB_ALT_PACKAGE_NAME=${PXR_PACKAGE}
            MFB_PACKAGE_MODULE=${pythonModuleName}
            PXR_BUILD_LOCATION=usd
            PXR_PLUGIN_BUILD_LOCATION=../plugin/usd
            ${pxrInstallLocation}
            ${pythonModulesEnabled}
            ${apiPrivate}
    )

    # Copy headers to the build directory and include from there and from
    # external packages.
    _copy_headers(${NAME}
        FILES
            ${args_PUBLIC_HEADERS}
            ${args_PRIVATE_HEADERS}
        PREFIX
            ${PXR_PREFIX}
    )

    target_include_directories(${NAME}
        PRIVATE
            "${CMAKE_BINARY_DIR}/include"
            "${CMAKE_BINARY_DIR}/${PXR_INSTALL_SUBDIR}/include"
        INTERFACE
            $<INSTALL_INTERFACE:${headerInstallDir}>
    )

    # The INCLUDE_DIRS argument specifies directories containing headers
    # for third-party libraries needed by this library. We treat these
    # as system include directories so that compiler warnings from these
    # headers are ignored, since we have no control over the contents
    # of those headers.
    target_include_directories(${NAME}
        SYSTEM
        PUBLIC
            ${args_INCLUDE_DIRS}
    )

    # If we're building documentation, we need to copy the files we want
    # doxygen to process to a parallel structure in the build directory.
    # Doxygen will be run on these files during the install step ---
    # see pxr_build_documentation().
    if(PXR_BUILD_DOCUMENTATION)
        set(docBuildDir ${CMAKE_BINARY_DIR}/docs/${headerInstallPrefix})
        set(doxygenFiles "${args_PUBLIC_HEADERS};${args_DOXYGEN_FILES}")

        set(files_copied "")

        foreach(doxygenFile ${doxygenFiles})
            add_custom_command(
                OUTPUT ${docBuildDir}/${doxygenFile}
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory ${docBuildDir}
                COMMAND
                    ${CMAKE_COMMAND} -E copy 
                    ${CMAKE_CURRENT_SOURCE_DIR}/${doxygenFile}
                    ${docBuildDir}/${doxygenFile}
                MAIN_DEPENDENCY
                    ${CMAKE_CURRENT_SOURCE_DIR}/${doxygenFile}
                VERBATIM
            )

            list(APPEND files_copied ${docBuildDir}/${doxygenFile})
        endforeach()

        add_custom_target(${NAME}_docfiles
            DEPENDS ${files_copied}
        )
        add_dependencies(${NAME} ${NAME}_docfiles)

        set_target_properties(${NAME}_docfiles
            PROPERTIES
                FOLDER "docs"
        )
    endif()

    # XXX -- May want some plugins to be baked into monolithic.
    _pxr_target_link_libraries(${NAME} ${args_LIBRARIES})

    # Rpath has libraries under the third party prefix and the install prefix.
    # The former is for helper libraries for a third party application and
    # the latter for core USD libraries.
    _pxr_init_rpath(rpath "${libInstallPrefix}")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/${PXR_INSTALL_SUBDIR}/lib")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${NAME})

    #
    # Set up the install.
    #

    if(isObject)
        get_target_property(install_headers ${NAME} PUBLIC_HEADER)
        if (install_headers)
            install(
                FILES ${install_headers}
                DESTINATION ${headerInstallPrefix}
            )
        endif()
    else()
        # Do not include plugins libs in externally linkable targets
        if(isPlugin)
            install(
                TARGETS ${NAME}
                LIBRARY DESTINATION ${libInstallPrefix}
                ARCHIVE DESTINATION ${libInstallPrefix}
                RUNTIME DESTINATION ${libInstallPrefix}
                PUBLIC_HEADER DESTINATION ${headerInstallPrefix}
            )
            if(WIN32)
                install(
                    FILES $<TARGET_PDB_FILE:${NAME}>
                    DESTINATION ${libInstallPrefix}
                    OPTIONAL
                )
            endif()
        elseif(BUILD_SHARED_LIBS)
            install(
                TARGETS ${NAME}
                EXPORT pxrTargets
                LIBRARY DESTINATION ${libInstallPrefix}
                ARCHIVE DESTINATION ${libInstallPrefix}
                RUNTIME DESTINATION ${libInstallPrefix}
                PUBLIC_HEADER DESTINATION ${headerInstallPrefix}
            )
            if(WIN32)
                install(
                    FILES $<TARGET_PDB_FILE:${NAME}>
                    EXPORT pxrTargets
                    DESTINATION ${libInstallPrefix}
                    OPTIONAL
                )
            endif()
        else()
            install(
                TARGETS ${NAME}
                EXPORT pxrTargets
                LIBRARY DESTINATION ${libInstallPrefix}
                ARCHIVE DESTINATION ${libInstallPrefix}
                RUNTIME DESTINATION ${libInstallPrefix}
                PUBLIC_HEADER DESTINATION ${headerInstallPrefix}
            )
        endif()
        
        if(NOT isPlugin)
            export(TARGETS ${NAME}
                APPEND
                FILE "${PROJECT_BINARY_DIR}/pxrTargets.cmake"
            )
        endif()

    endif()

    _install_resource_files(
        ${NAME}
        "${pluginInstallPrefix}"
        "${pluginToLibraryPath}"
        ${args_RESOURCE_FILES})

    #
    # Set up precompiled headers.
    #

    if(NOT "${PXR_PREFIX}" STREQUAL "")
        if(args_PRECOMPILED_HEADERS)
            _pxr_enable_precompiled_header(${NAME}
                SOURCE_NAME "${args_PRECOMPILED_HEADER_NAME}"
            )
        endif()
    endif()
endfunction() # _pxr_library
