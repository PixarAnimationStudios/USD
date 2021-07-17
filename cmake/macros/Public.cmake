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
include(Private)

function(pxr_build_documentation)
    configure_file(${CMAKE_SOURCE_DIR}/docs/doxygen/Doxyfile.in
                   ${CMAKE_BINARY_DIR}/Doxyfile)

    add_custom_target(
        documentation
        ALL
        # We need to manually copy pxr.h into the docs/include directory
        # since it's generated outside of the libraries.
        COMMAND
            ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/include/pxr/pxr.h"
            "${CMAKE_BINARY_DIR}/docs/include/pxr/pxr.h"
        COMMAND 
            ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/docs"
            "${CMAKE_BINARY_DIR}/docs"
    )

    # Execute doxygen during the install step. All of the files we want
    # doxygen to process should already have been copied to the docs
    # directory during the build step
    install(CODE "execute_process(COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile)")

    set(INST_DOCS_ROOT  "${CMAKE_INSTALL_PREFIX}/docs")

    set(BUILT_HTML_DOCS "${CMAKE_BINARY_DIR}/docs/doxy_html")
    install(
        DIRECTORY ${BUILT_HTML_DOCS}
        DESTINATION ${INST_DOCS_ROOT}
    )

    set(BUILT_XML_DOCS "${CMAKE_BINARY_DIR}/docs/doxy_xml")
    install(
        DIRECTORY ${BUILT_XML_DOCS}
        DESTINATION ${INST_DOCS_ROOT}
    )
endfunction()

function(pxr_python_bin BIN_NAME)
    set(oneValueArgs
        PYTHON_FILE
    )
    set(multiValueArgs
        DEPENDENCIES
        LIBRARIES
    )
    cmake_parse_arguments(pb
        ""
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # If we can't build Python modules then do nothing.
    if(NOT TARGET python)
        message(STATUS "Skipping Python program ${BIN_NAME}, Python modules required")
        return()
    endif()

    _get_install_dir(bin installDir)

    # Source file.
    if( "${pb_PYTHON_FILE}" STREQUAL "")
        set(infile ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_NAME}.py)
    else()
        set(infile ${CMAKE_CURRENT_SOURCE_DIR}/${pb_PYTHON_FILE})
    endif()

    # Destination file.
    set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${BIN_NAME})

    # /pxrpythonsubst will be replaced with the full path to the configured
    # python executable. This doesn't use the CMake ${...} or @...@ syntax
    # for backwards compatibility with other build systems.
    add_custom_command(
        OUTPUT ${outfile}
        DEPENDS ${infile}
        COMMENT "Substituting Python shebang"
        COMMAND
            ${PYTHON_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/cmake/macros/shebang.py
            ${PXR_PYTHON_SHEBANG}
            ${infile}
            ${outfile}
    )
    list(APPEND outputs ${outfile})

    install(
        PROGRAMS ${outfile}
        DESTINATION ${installDir}
        RENAME ${BIN_NAME}
    )

    # Windows by default cannot execute Python files so here
    # we create a batch file wrapper that invokes the python
    # files.
    if(WIN32)
        add_custom_command(
            OUTPUT ${outfile}.cmd
            COMMENT "Creating Python cmd wrapper"
            COMMAND
                ${PYTHON_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/cmake/macros/shebang.py
                ${BIN_NAME}
                ${outfile}.cmd
        )
        list(APPEND outputs ${outfile}.cmd)

        install(
            PROGRAMS ${outfile}.cmd
            DESTINATION ${installDir}
            RENAME ${BIN_NAME}.cmd
        )
    endif()

    # Add the target.
    add_custom_target(${BIN_NAME}_script
        DEPENDS ${outputs} ${pb_DEPENDENCIES}
    )
    add_dependencies(python ${BIN_NAME}_script)

    _pxr_target_link_libraries(${BIN_NAME}_script
        ${pb_LIBRARIES}
    )

    _get_folder("" folder)
    set_target_properties(${BIN_NAME}_script
        PROPERTIES
            FOLDER "${folder}"
    )
endfunction() # pxr_python_bin

function(pxr_cpp_bin BIN_NAME)
    _get_install_dir(bin installDir)
    
    set(multiValueArgs
        LIBRARIES
        INCLUDE_DIRS
    )

    cmake_parse_arguments(cb
        ""  
        ""
        "${multiValueArgs}"
        ${ARGN}
    )

    add_executable(${BIN_NAME} ${BIN_NAME}.cpp)

    # Turn PIC ON otherwise ArchGetAddressInfo() on Linux may yield
    # unexpected results.
    _get_folder("" folder)
    set_target_properties(${BIN_NAME}
        PROPERTIES
            FOLDER "${folder}"
            POSITION_INDEPENDENT_CODE ON
    )

    # Install and include headers from the build directory.
    get_filename_component(
        PRIVATE_INC_DIR
        "${CMAKE_BINARY_DIR}/include"
        ABSOLUTE
    )

    target_include_directories(${BIN_NAME}
        PRIVATE 
        ${PRIVATE_INC_DIR}
        ${cb_INCLUDE_DIRS}
    )

    _pxr_init_rpath(rpath "${installDir}")
    _pxr_install_rpath(rpath ${BIN_NAME})

    _pxr_target_link_libraries(${BIN_NAME}
        ${cb_LIBRARIES}
        ${PXR_MALLOC_LIBRARY}
    )

    install(
        TARGETS ${BIN_NAME}
        DESTINATION ${installDir}
    )
endfunction()

function(pxr_library NAME)
    set(options
        DISABLE_PRECOMPILED_HEADERS
    )
    set(oneValueArgs
        TYPE
        PRECOMPILED_HEADER_NAME
    )
    set(multiValueArgs
        PUBLIC_CLASSES
        PUBLIC_HEADERS
        PRIVATE_CLASSES
        PRIVATE_HEADERS
        CPPFILES
        LIBRARIES
        INCLUDE_DIRS
        DOXYGEN_FILES
        RESOURCE_FILES
        PYTHON_PUBLIC_CLASSES
        PYTHON_PRIVATE_CLASSES
        PYTHON_PUBLIC_HEADERS
        PYTHON_PRIVATE_HEADERS
        PYTHON_CPPFILES
        PYMODULE_CPPFILES
        PYMODULE_FILES
        PYSIDE_UI_FILES
    )

    cmake_parse_arguments(args
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # If python support is enabled, merge the python specific categories
    # with the more general before setting up compilation.
    if(PXR_ENABLE_PYTHON_SUPPORT)
        if(args_PYTHON_PUBLIC_CLASSES)
            list(APPEND args_PUBLIC_CLASSES ${args_PYTHON_PUBLIC_CLASSES})
        endif()
        if(args_PYTHON_PUBLIC_HEADERS)
            list(APPEND args_PUBLIC_HEADERS ${args_PYTHON_PUBLIC_HEADERS})
        endif()
        if(args_PYTHON_PRIVATE_CLASSES)
            list(APPEND args_PRIVATE_CLASSES ${args_PYTHON_PRIVATE_CLASSES})
        endif()
        if(args_PYTHON_PRIVATE_HEADERS)
            list(APPEND args_PRIVATE_HEADERS ${args_PYTHON_PRIVATE_HEADERS})
        endif()
        if(args_PYTHON_CPPFILES)
            list(APPEND args_CPPFILES ${args_PYTHON_CPPFILES})
        endif()
    endif()

    # Collect libraries.
    if(NOT args_TYPE STREQUAL "PLUGIN")
        get_property(help CACHE PXR_ALL_LIBS PROPERTY HELPSTRING)
        list(APPEND PXR_ALL_LIBS ${NAME})
        set(PXR_ALL_LIBS "${PXR_ALL_LIBS}" CACHE INTERNAL "${help}")
        if(args_TYPE STREQUAL "STATIC")
            # Note if this library is explicitly STATIC.
            get_property(help CACHE PXR_STATIC_LIBS PROPERTY HELPSTRING)
            list(APPEND PXR_STATIC_LIBS ${NAME})
            set(PXR_STATIC_LIBS "${PXR_STATIC_LIBS}" CACHE INTERNAL "${help}")
        endif()
    endif()

    # Expand classes into filenames.
    _classes(${NAME} ${args_PRIVATE_CLASSES} PRIVATE)
    _classes(${NAME} ${args_PUBLIC_CLASSES} PUBLIC)

    # If building a core plugin for a monolithic build then treat it almost
    # like any other library and include it in the monolithic library.
    if (_building_core AND _building_monolithic AND args_TYPE STREQUAL "PLUGIN")
        set(args_TYPE "OBJECT_PLUGIN")
    endif()

    # Custom tweaks.
    if(args_TYPE STREQUAL "PLUGIN")
        # We can't build plugins if we're not building shared libraries.
        if(NOT TARGET shared_libs)
            message(STATUS "Skipping plugin ${NAME}, shared libraries required")
            return()
        endif()

        set(prefix "")
        set(suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
    else()
        # If the caller didn't specify the library type then choose the
        # type now.
        if("x${args_TYPE}" STREQUAL "x")
            if(_building_monolithic)
                set(args_TYPE "OBJECT")
            elseif(BUILD_SHARED_LIBS)
                set(args_TYPE "SHARED")
            else()
                set(args_TYPE "STATIC")
            endif()
        endif()

        set(prefix "${PXR_LIB_PREFIX}")
        if(args_TYPE STREQUAL "STATIC")
            set(suffix ${CMAKE_STATIC_LIBRARY_SUFFIX})
        else()
            set(suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
        endif()
    endif()

    set(pch "ON")
    if(args_DISABLE_PRECOMPILED_HEADERS)
        set(pch "OFF")
    endif()

    _pxr_library(${NAME}
        TYPE "${args_TYPE}"
        PREFIX "${prefix}"
        SUFFIX "${suffix}"
        SUBDIR "${subdir}"
        CPPFILES "${args_CPPFILES};${${NAME}_CPPFILES}"
        PUBLIC_HEADERS "${args_PUBLIC_HEADERS};${${NAME}_PUBLIC_HEADERS}"
        PRIVATE_HEADERS "${args_PRIVATE_HEADERS};${${NAME}_PRIVATE_HEADERS}"
        LIBRARIES "${args_LIBRARIES}"
        INCLUDE_DIRS "${args_INCLUDE_DIRS}"
        RESOURCE_FILES "${args_RESOURCE_FILES}"
        DOXYGEN_FILES "${args_DOXYGEN_FILES}"
        PRECOMPILED_HEADERS "${pch}"
        PRECOMPILED_HEADER_NAME "${args_PRECOMPILED_HEADER_NAME}"
        LIB_INSTALL_PREFIX_RESULT libInstallPrefix
    )

    if(PXR_ENABLE_PYTHON_SUPPORT AND (args_PYMODULE_CPPFILES OR args_PYMODULE_FILES OR args_PYSIDE_UI_FILES))
        _pxr_python_module(
            ${NAME}
            WRAPPED_LIB_INSTALL_PREFIX "${libInstallPrefix}"
            PYTHON_FILES ${args_PYMODULE_FILES}
            PYSIDE_UI_FILES ${args_PYSIDE_UI_FILES}
            CPPFILES ${args_PYMODULE_CPPFILES}
            INCLUDE_DIRS ${args_INCLUDE_DIRS}
            PRECOMPILED_HEADERS ${pch}
            PRECOMPILED_HEADER_NAME ${args_PRECOMPILED_HEADER_NAME}
        )
    endif()
endfunction()

macro(pxr_shared_library NAME)
    pxr_library(${NAME} TYPE "SHARED" ${ARGN})
endmacro(pxr_shared_library)

macro(pxr_static_library NAME)
    pxr_library(${NAME} TYPE "STATIC" ${ARGN})
endmacro(pxr_static_library)

macro(pxr_plugin NAME)
    pxr_library(${NAME} TYPE "PLUGIN" ${ARGN})
endmacro(pxr_plugin)

function(pxr_setup_python)
    get_property(pxrPythonModules GLOBAL PROPERTY PXR_PYTHON_MODULES)

    # A new list where each python module is quoted
    set(converted "")
    foreach(module ${pxrPythonModules})
        list(APPEND converted "'${module}'")
    endforeach()

    # Join these with a ', '
    string(REPLACE ";" ", " pyModulesStr "${converted}")

    # Install a pxr __init__.py with an appropriate __all__
    _get_install_dir(lib/python/pxr installPrefix)

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated_modules_init.py"
         "__all__ = [${pyModulesStr}]\n")

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/generated_modules_init.py"
        DESTINATION ${installPrefix}
        RENAME "__init__.py"
    )
endfunction() # pxr_setup_python

function (pxr_create_test_module MODULE_NAME)
    # If we can't build Python modules then do nothing.
    if(NOT TARGET python)
        return()
    endif()

    if (NOT PXR_BUILD_TESTS) 
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(tm "" "INSTALL_PREFIX;SOURCE_DIR" "" ${ARGN})

    if (NOT tm_SOURCE_DIR)
        set(tm_SOURCE_DIR testenv)
    endif()

    # Look specifically for an __init__.py and a plugInfo.json prefixed by the
    # module name. These will be installed without the module prefix.
    set(initPyFile ${tm_SOURCE_DIR}/${MODULE_NAME}__init__.py)
    set(plugInfoFile ${tm_SOURCE_DIR}/${MODULE_NAME}_plugInfo.json)

    # XXX -- We shouldn't have to install to run tests.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${initPyFile}")
        install(
            FILES 
                ${initPyFile}
            RENAME 
                __init__.py
            DESTINATION 
                tests/${tm_INSTALL_PREFIX}/lib/python/${MODULE_NAME}
        )
    endif()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${plugInfoFile}")
        install(
            FILES 
                ${plugInfoFile}
            RENAME 
                plugInfo.json
            DESTINATION 
                tests/${tm_INSTALL_PREFIX}/lib/python/${MODULE_NAME}
        )
    endif()
endfunction() # pxr_create_test_module

function(pxr_build_test_shared_lib LIBRARY_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        ""
        "INSTALL_PREFIX;SOURCE_DIR"
        "LIBRARIES;CPPFILES"
        ${ARGN}
    )
        
    add_library(${LIBRARY_NAME}
        SHARED
        ${bt_CPPFILES}
    )
    _pxr_target_link_libraries(${LIBRARY_NAME}
        ${bt_LIBRARIES}
    )
    _get_folder("tests/lib" folder)
    set_target_properties(${LIBRARY_NAME}
        PROPERTIES 
            FOLDER "${folder}"
    )

    # Find libraries under the install prefix, which has the core USD
    # libraries.
    _pxr_init_rpath(rpath "tests/lib")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${LIBRARY_NAME})

    if (NOT bt_SOURCE_DIR)
        set(bt_SOURCE_DIR testenv)
    endif()
    set(testPlugInfoSrcPath ${bt_SOURCE_DIR}/${LIBRARY_NAME}_plugInfo.json)

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${testPlugInfoSrcPath}")
        set(TEST_PLUG_INFO_RESOURCE_PATH "Resources")
        set(TEST_PLUG_INFO_ROOT "..")
        set(LIBRARY_FILE "${CMAKE_SHARED_LIBRARY_PREFIX}${LIBRARY_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")

        set(testPlugInfoLibDir "tests/${bt_INSTALL_PREFIX}/lib/${LIBRARY_NAME}")
        set(testPlugInfoResourceDir "${testPlugInfoLibDir}/${TEST_PLUG_INFO_RESOURCE_PATH}")
        set(testPlugInfoPath "${CMAKE_BINARY_DIR}/${testPlugInfoResourceDir}/plugInfo.json")

        file(RELATIVE_PATH 
            TEST_PLUG_INFO_LIBRARY_PATH
            "${CMAKE_INSTALL_PREFIX}/${testPlugInfoLibDir}"
            "${CMAKE_INSTALL_PREFIX}/tests/lib/${LIBRARY_FILE}")

        configure_file("${testPlugInfoSrcPath}" "${testPlugInfoPath}")
        # XXX -- We shouldn't have to install to run tests.
        install(
            FILES ${testPlugInfoPath}
            DESTINATION ${testPlugInfoResourceDir})
    endif()

    # We always want this test to build after the package it's under, even if
    # it doesn't link directly. This ensures that this test is able to include
    # headers from its parent package.
    add_dependencies(${LIBRARY_NAME} ${PXR_PACKAGE})

    # Test libraries can include the private headers of their parent PXR_PACKAGE
    # library
    target_include_directories(${LIBRARY_NAME}
        PRIVATE $<TARGET_PROPERTY:${PXR_PACKAGE},INCLUDE_DIRECTORIES>
    )

    # XXX -- We shouldn't have to install to run tests.
    install(
        TARGETS ${LIBRARY_NAME}
        LIBRARY DESTINATION "tests/lib"
        ARCHIVE DESTINATION "tests/lib"
        RUNTIME DESTINATION "tests/lib"
    )
endfunction() # pxr_build_test_shared_lib

function(pxr_build_test TEST_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        "" ""
        "LIBRARIES;CPPFILES"
        ${ARGN}
    )

    add_executable(${TEST_NAME}
        ${bt_CPPFILES}
    )

    # Turn PIC ON otherwise ArchGetAddressInfo() on Linux may yield
    # unexpected results.
    _get_folder("tests/bin" folder)
    set_target_properties(${TEST_NAME}
        PROPERTIES 
            FOLDER "${folder}"
        	POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(${TEST_NAME}
        PRIVATE $<TARGET_PROPERTY:${PXR_PACKAGE},INCLUDE_DIRECTORIES>
    )
    _pxr_target_link_libraries(${TEST_NAME}
        ${bt_LIBRARIES}
    )

    # Find libraries under the install prefix, which has the core USD
    # libraries.
    _pxr_init_rpath(rpath "tests")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${TEST_NAME})

    # XXX -- We shouldn't have to install to run tests.
    install(TARGETS ${TEST_NAME}
        RUNTIME DESTINATION "tests"
    )
endfunction() # pxr_build_test

function(pxr_test_scripts)
    # If we can't build Python modules then do nothing.
    if(NOT TARGET python)
        return()
    endif()

    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    foreach(file ${ARGN})
        get_filename_component(destFile ${file} NAME_WE)
        # XXX -- We shouldn't have to install to run tests.
        install(
            PROGRAMS ${file}
            DESTINATION tests
            RENAME ${destFile}
        )
    endforeach()
endfunction() # pxr_test_scripts

function(pxr_install_test_dir)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        "" 
        "SRC;DEST"
        ""
        ${ARGN}
    )

    # XXX -- We shouldn't have to install to run tests.
    install(
        DIRECTORY ${bt_SRC}/
        DESTINATION tests/ctest/${bt_DEST}
    )
endfunction() # pxr_install_test_dir

function(pxr_register_test TEST_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        "RUN_SERIAL;PYTHON;REQUIRES_SHARED_LIBS;REQUIRES_PYTHON_MODULES" 
        "CUSTOM_PYTHON;COMMAND;STDOUT_REDIRECT;STDERR_REDIRECT;POST_COMMAND;POST_COMMAND_STDOUT_REDIRECT;POST_COMMAND_STDERR_REDIRECT;PRE_COMMAND;PRE_COMMAND_STDOUT_REDIRECT;PRE_COMMAND_STDERR_REDIRECT;FILES_EXIST;FILES_DONT_EXIST;CLEAN_OUTPUT;EXPECTED_RETURN_CODE;TESTENV"
        "DIFF_COMPARE;ENV;PRE_PATH;POST_PATH"
        ${ARGN}
    )

    # Discard tests that required shared libraries.
    if(NOT TARGET shared_libs)
        # Explicit requirement.  This is for C++ tests that dynamically
        # load libraries linked against USD code.  These tests will have
        # multiple copies of symbols and will likely re-execute
        # ARCH_CONSTRUCTOR and registration functions, which will almost
        # certainly cause problems.
        if(bt_REQUIRES_SHARED_LIBS)
            message(STATUS "Skipping test ${TEST_NAME}, shared libraries required")
            return()
        endif()
    endif()

    if(NOT TARGET python)
        # Implicit requirement.  Python modules require shared USD
        # libraries.  If the test runs python it's certainly going
        # to load USD modules.  If the test uses C++ to load USD
        # modules it tells us via REQUIRES_PYTHON_MODULES.
        if(bt_PYTHON OR bt_CUSTOM_PYTHON OR bt_REQUIRES_PYTHON_MODULES)
            message(STATUS "Skipping test ${TEST_NAME}, Python modules required")
            return()
        endif()
    endif()

    # This harness is a filter which allows us to manipulate the test run, 
    # e.g. by changing the environment, changing the expected return code, etc.
    set(testWrapperCmd ${PROJECT_SOURCE_DIR}/cmake/macros/testWrapper.py --verbose)

    if (bt_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --stdout-redirect=${bt_STDOUT_REDIRECT})
    endif()

    if (bt_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --stderr-redirect=${bt_STDERR_REDIRECT})
    endif()

    if (bt_PRE_COMMAND_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --pre-command-stdout-redirect=${bt_PRE_COMMAND_STDOUT_REDIRECT})
    endif()

    if (bt_PRE_COMMAND_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --pre-command-stderr-redirect=${bt_PRE_COMMAND_STDERR_REDIRECT})
    endif()

    if (bt_POST_COMMAND_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --post-command-stdout-redirect=${bt_POST_COMMAND_STDOUT_REDIRECT})
    endif()

    if (bt_POST_COMMAND_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --post-command-stderr-redirect=${bt_POST_COMMAND_STDERR_REDIRECT})
    endif()

    # Not all tests will have testenvs, but if they do let the wrapper know so
    # it can copy the testenv contents into the run directory. By default,
    # assume the testenv has the same name as the test but allow it to be
    # overridden by specifying TESTENV.
    if (bt_TESTENV)
        set(testenvDir ${CMAKE_INSTALL_PREFIX}/tests/ctest/${bt_TESTENV})
    else()
        set(testenvDir ${CMAKE_INSTALL_PREFIX}/tests/ctest/${TEST_NAME})
    endif()

    set(testWrapperCmd ${testWrapperCmd} --testenv-dir=${testenvDir})

    if (bt_DIFF_COMPARE)
        foreach(compareFile ${bt_DIFF_COMPARE})
            set(testWrapperCmd ${testWrapperCmd} --diff-compare=${compareFile})
        endforeach()

        # For now the baseline directory is assumed by convention from the test
        # name. There may eventually be cases where we'd want to specify it by
        # an argument though.
        set(baselineDir ${testenvDir}/baseline)
        set(testWrapperCmd ${testWrapperCmd} --baseline-dir=${baselineDir})
    endif()

    if (bt_CLEAN_OUTPUT)
        set(testWrapperCmd ${testWrapperCmd} --clean-output-paths=${bt_CLEAN_OUTPUT})
    endif()

    if (bt_FILES_EXIST)
        set(testWrapperCmd ${testWrapperCmd} --files-exist=${bt_FILES_EXIST})
    endif()

    if (bt_FILES_DONT_EXIST)
        set(testWrapperCmd ${testWrapperCmd} --files-dont-exist=${bt_FILES_DONT_EXIST})
    endif()

    if (bt_PRE_COMMAND)
        set(testWrapperCmd ${testWrapperCmd} --pre-command=${bt_PRE_COMMAND})
    endif()

    if (bt_POST_COMMAND)
        set(testWrapperCmd ${testWrapperCmd} --post-command=${bt_POST_COMMAND})
    endif()

    if (bt_EXPECTED_RETURN_CODE)
        set(testWrapperCmd ${testWrapperCmd} --expected-return-code=${bt_EXPECTED_RETURN_CODE})
    endif()

    if (bt_ENV)
        foreach(env ${bt_ENV})
            set(testWrapperCmd ${testWrapperCmd} --env-var=${env})
        endforeach()
    endif()

    if (bt_PRE_PATH)
        foreach(path ${bt_PRE_PATH})
            set(testWrapperCmd ${testWrapperCmd} --pre-path=${path})
        endforeach()
    endif()

    if (bt_POST_PATH)
        foreach(path ${bt_POST_PATH})
            set(testWrapperCmd ${testWrapperCmd} --post-path=${path})
        endforeach()
    endif()
        
    # If we're building static libraries, the C++ tests that link against
    # these libraries will look for resource files in the "usd" subdirectory
    # relative to where the tests are installed. However, the build installs
    # these files in the "lib" directory where the libraries are installed. 
    #
    # We don't want to copy these resource files for each test, so instead
    # we set the PXR_PLUGINPATH_NAME env var to point to the "lib/usd"
    # directory where these files are installed.
    if (NOT TARGET shared_libs)
        set(testWrapperCmd ${testWrapperCmd} --env-var=${PXR_PLUGINPATH_NAME}=${CMAKE_INSTALL_PREFIX}/lib/usd)
    endif()

    # Ensure that Python imports the Python files built by this build.
    # On Windows convert backslash to slash and don't change semicolons
    # to colons.
    set(_testPythonPath "${CMAKE_INSTALL_PREFIX}/lib/python;$ENV{PYTHONPATH}")
    if(WIN32)
        string(REGEX REPLACE "\\\\" "/" _testPythonPath "${_testPythonPath}")
    else()
        string(REPLACE ";" ":" _testPythonPath "${_testPythonPath}")
    endif()

    # Ensure we run with the appropriate python executable.
    if (bt_CUSTOM_PYTHON)
        set(testCmd "${bt_CUSTOM_PYTHON} ${bt_COMMAND}")
    elseif (bt_PYTHON)
        set(testCmd "${PYTHON_EXECUTABLE} ${bt_COMMAND}")
    else()
        set(testCmd "${bt_COMMAND}")
    endif()

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${PYTHON_EXECUTABLE} ${testWrapperCmd}
                "--env-var=PYTHONPATH=${_testPythonPath}" ${testCmd}
    )

    # But in some cases, we need to pass cmake properties directly to cmake
    # run_test, rather than configuring the environment
    if (bt_RUN_SERIAL)
        set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
    endif()

endfunction() # pxr_register_test

function(pxr_setup_plugins)
    # Install a top-level plugInfo.json in the shared area and into the 
    # top-level plugin area
    _get_resources_dir_name(resourcesDir)

    # Add extra plugInfo.json include paths to the top-level plugInfo.json,
    # relative to that top-level file.
    set(extraIncludes "")
    list(REMOVE_DUPLICATES PXR_EXTRA_PLUGINS)
    foreach(dirName ${PXR_EXTRA_PLUGINS})
        file(RELATIVE_PATH
            relDirName
            "${CMAKE_INSTALL_PREFIX}/lib/usd"
            "${CMAKE_INSTALL_PREFIX}/${dirName}"
        )
        set(extraIncludes "${extraIncludes},\n        \"${relDirName}/\"")
    endforeach()

    set(plugInfoContents "{\n    \"Includes\": [\n        \"*/${resourcesDir}/\"${extraIncludes}\n    ]\n}\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugins_plugInfo.json"
         "${plugInfoContents}")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plugins_plugInfo.json"
        DESTINATION lib/usd
        RENAME "plugInfo.json"
    )

    set(plugInfoContents "{\n    \"Includes\": [ \"*/${resourcesDir}/\" ]\n}\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usd_plugInfo.json"
         "${plugInfoContents}")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/usd_plugInfo.json"
        DESTINATION plugin/usd
        RENAME "plugInfo.json"
    )
endfunction() # pxr_setup_plugins

function(pxr_add_extra_plugins PLUGIN_AREAS)
    # Install a top-level plugInfo.json in the given plugin areas.
    _get_resources_dir_name(resourcesDir)
    set(plugInfoContents "{\n    \"Includes\": [ \"*/${resourcesDir}/\" ]\n}\n")

    get_property(help CACHE PXR_EXTRA_PLUGINS PROPERTY HELPSTRING)

    foreach(area ${PLUGIN_AREAS})
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${area}_plugInfo.json"
            "${plugInfoContents}")
        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/${area}_plugInfo.json"
            DESTINATION "${PXR_INSTALL_SUBDIR}/${area}"
            RENAME "plugInfo.json"
        )
        list(APPEND PXR_EXTRA_PLUGINS "${PXR_INSTALL_SUBDIR}/${area}")
    endforeach()

    set(PXR_EXTRA_PLUGINS "${PXR_EXTRA_PLUGINS}" CACHE INTERNAL "${help}")
endfunction() # pxr_setup_third_plugins

function(pxr_toplevel_prologue)
    # Generate a namespace declaration header, pxr.h, at the top level of
    # pxr at configuration time.
    configure_file(${CMAKE_SOURCE_DIR}/pxr/pxr.h.in
        ${CMAKE_BINARY_DIR}/include/pxr/pxr.h     
    )  
    install(
        FILES ${CMAKE_BINARY_DIR}/include/pxr/pxr.h
        DESTINATION include/pxr
    )

    # Create a monolithic shared library target if we should import one
    # or create one.
    if(PXR_BUILD_MONOLITHIC)
        if(PXR_MONOLITHIC_IMPORT)
            # Gather the export information for usd_ms.
            include("${PXR_MONOLITHIC_IMPORT}" OPTIONAL RESULT_VARIABLE found)

            # If the import wasn't found then create it and import it.
            # This ensures that the build files will be regenerated if
            # the file's contents change.  If this isn't desired or
            # write permissions aren't granted the client can configure
            # first without PXR_MONOLITHIC_IMPORT, build the 'monolithic'
            # target, build their own shared library and export file,
            # then configure again with PXR_MONOLITHIC_IMPORT.
            if(found STREQUAL "NOTFOUND")
                file(WRITE "${PXR_MONOLITHIC_IMPORT}" "")
                include("${PXR_MONOLITHIC_IMPORT}")
            endif()

            # If there's an IMPORTED_LOCATION then its parent must be
            # the install directory ${CMAKE_INSTALL_PREFIX}.  If it
            # isn't then we won't be able to find plugInfo.json files
            # at runtime because they're found relative to the library
            # that contains pxr/base/lib/plug/initConfig.cpp.  The
            # exception is if ${PXR_INSTALL_LOCATION} is set;  in that
            # case we assume the files will be found there regardless
            # of IMPORTED_LOCATION.  Note, however, that the install
            # cannot be relocated in this case.
            if(NOT PXR_INSTALL_LOCATION AND TARGET usd_ms)
                get_property(location TARGET usd_ms PROPERTY IMPORTED_LOCATION)
                if(location)
                    # Remove filename and directory.
                    get_filename_component(parent "${location}" PATH)
                    get_filename_component(parent "${parent}" PATH)
                    get_filename_component(parent "${parent}" ABSOLUTE)
                    get_filename_component(prefix "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
                    if(NOT "${parent}" STREQUAL "${prefix}")
                        message("IMPORTED_LOCATION for usd_ms ${location} inconsistent with install directory ${CMAKE_INSTALL_PREFIX}.")
                        message(WARNING "May not find plugins at runtime.")
                    endif()
                endif()
            endif()
        else()
            # Note that we ignore BUILD_SHARED_LIBS when building monolithic
            # when PXR_MONOLITHIC_IMPORT isn't set:  we always build an
            # archive library from the core libraries and then build a
            # shared library from that.  BUILD_SHARED_LIBS is still used
            # for libraries outside of the core.

            # We need at least one source file for the library so we
            # create an empty one.
            add_custom_command(
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp"
                COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp"
            )

            # Our shared library.
            add_library(usd_ms SHARED "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp")
            _get_folder("" folder)
            set_target_properties(usd_ms
                PROPERTIES
                    FOLDER "${folder}"
                    PREFIX "${PXR_LIB_PREFIX}"
                    IMPORT_PREFIX "${PXR_LIB_PREFIX}"
            )
            _get_install_dir("lib" libInstallPrefix)
            install(
                TARGETS usd_ms
                LIBRARY DESTINATION ${libInstallPrefix}
                ARCHIVE DESTINATION ${libInstallPrefix}
                RUNTIME DESTINATION ${libInstallPrefix}
            )
            if(WIN32)
                install(
                    FILES $<TARGET_PDB_FILE:usd_ms>
                    DESTINATION ${libInstallPrefix}
                    OPTIONAL
                )
            endif()
        endif()
    endif()

    # Create a target for shared libraries.  We currently use this only
    # to test its existence.
    if(BUILD_SHARED_LIBS OR TARGET usd_ms)
        add_custom_target(shared_libs)
    endif()

    # Create a target for targets that require Python.  Each should add
    # itself as a dependency to the "python" target.
    if(TARGET shared_libs AND PXR_ENABLE_PYTHON_SUPPORT)
        add_custom_target(python ALL)
    endif()
endfunction() # pxr_toplevel_prologue

function(pxr_toplevel_epilogue)
    # If we're building a shared monolithic library then link it against
    # usd_m.
    if(TARGET usd_ms AND NOT PXR_MONOLITHIC_IMPORT)
        # We need to use whole-archive to get all the symbols.  Also note
        # that we carefully avoid adding the usd_m target itself by using
        # TARGET_FILE.  Linking the usd_m target would link usd_m and
        # everything it links to.
        if(MSVC)
            target_link_libraries(usd_ms
                PRIVATE
                    -WHOLEARCHIVE:$<TARGET_FILE:usd_m>
            )
        elseif(CMAKE_COMPILER_IS_GNUCXX)
            target_link_libraries(usd_ms
                PRIVATE
                    -Wl,--whole-archive $<TARGET_FILE:usd_m> -Wl,--no-whole-archive
            )
        elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
            target_link_libraries(usd_ms
                PRIVATE
                    -Wl,-force_load $<TARGET_FILE:usd_m>
            )
        endif()

        # Since we didn't add a dependency to usd_ms on usd_m above, we
        # manually add it here along with compile definitions, include
        # directories, etc
        add_dependencies(usd_ms usd_m)

        # Add the stuff we didn't get because we didn't link against the
        # usd_m target.
        target_compile_definitions(usd_ms
            PUBLIC
                $<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>
        )
        target_include_directories(usd_ms
            PUBLIC
                $<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_include_directories(usd_ms
            SYSTEM
            PUBLIC
                $<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>
        )
        foreach(lib ${PXR_OBJECT_LIBS})
            get_property(libs TARGET ${lib} PROPERTY INTERFACE_LINK_LIBRARIES)
            target_link_libraries(usd_ms
                PUBLIC
                    ${libs}
            )
        endforeach()
        target_link_libraries(usd_ms
            PUBLIC
                ${PXR_MALLOC_LIBRARY}
                ${PXR_THREAD_LIBS}
        )

        _pxr_init_rpath(rpath "${libInstallPrefix}")
        _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/${PXR_INSTALL_SUBDIR}/lib")
        _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
        _pxr_install_rpath(rpath usd_ms)
    endif()

    # Setup the plugins in the top epilogue to ensure that everybody has had a
    # chance to update PXR_EXTRA_PLUGINS with their plugin paths.
    pxr_setup_plugins()
endfunction() # pxr_toplevel_epilogue

function(pxr_monolithic_epilogue)
    # When building a monolithic library we want all API functions to be
    # exported.  So add FOO_EXPORTS=1 for every library in PXR_OBJECT_LIBS,
    # where FOO is the uppercase version of the library name, to every
    # library in PXR_OBJECT_LIBS.
    set(exports "")
    foreach(lib ${PXR_OBJECT_LIBS})
        string(TOUPPER ${lib} uppercaseName)
        list(APPEND exports "${uppercaseName}_EXPORTS=1")
    endforeach()
    foreach(lib ${PXR_OBJECT_LIBS})
        set(objects "${objects};\$<TARGET_OBJECTS:${lib}>")
        target_compile_definitions(${lib} PRIVATE ${exports})
    endforeach()

    # Collect all of the objects for all of the core libraries to add to
    # the monolithic library.
    set(objects "")
    foreach(lib ${PXR_OBJECT_LIBS})
        set(objects "${objects};\$<TARGET_OBJECTS:${lib}>")
    endforeach()

    # Add the monolithic library.  This has to be delayed until now
    # because $<TARGET_OBJECTS> isn't a real generator expression
    # in that it can only appear in the sources of add_library() or
    # add_executable();  it can't appear in target_sources().  We
    # need at least one source file so we create an empty one
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp"
    )
    add_library(usd_m STATIC "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp" ${objects})

    _get_folder("" folder)
    set_target_properties(usd_m
        PROPERTIES
            FOLDER "${folder}"
            POSITION_INDEPENDENT_CODE ON
            PREFIX "${PXR_LIB_PREFIX}"
            IMPORT_PREFIX "${PXR_LIB_PREFIX}"
    )

    # Adding $<TARGET_OBJECTS:foo> will not bring along compile
    # definitions, include directories, etc.  Since we'll want those
    # attached to usd_m we explicitly add them.
    foreach(lib ${PXR_OBJECT_LIBS})
        target_compile_definitions(usd_m
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_COMPILE_DEFINITIONS>
        )
        target_include_directories(usd_m
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_include_directories(usd_m
            SYSTEM
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>
        )

        get_property(libs TARGET ${lib} PROPERTY INTERFACE_LINK_LIBRARIES)
        target_link_libraries(usd_m
            PUBLIC
                ${libs}
        )
    endforeach()

    # Manual export targets.  We can't use install(EXPORT) because usd_m
    # depends on OBJECT libraries which cannot be exported yet must be
    # in order to export usd_m.  We also have boilerplate for usd_ms, the
    # externally built monolithic shared library containing usd_m.  The
    # client should replace the FIXMEs with the appropriate paths or
    # use the usd_m export to build against and generate a usd_ms export.
    set(export "")
    set(export "${export}add_library(usd_m STATIC IMPORTED)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY IMPORTED_LOCATION $<TARGET_FILE:usd_m>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_COMPILE_DEFINITIONS $<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_LINK_LIBRARIES $<TARGET_PROPERTY:usd_m,INTERFACE_LINK_LIBRARIES>)\n")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
        CONTENT "${export}"
    )
    set(export "")
    set(export "${export}# Boilerplate for export of usd_ms.  Replace FIXMEs with appropriate paths\n")
    set(export "${export}# or include usd-targets-$<CONFIG>.cmake in your own build and generate your\n")
    set(export "${export}# own export file.  Configure with PXR_MONOLITHIC_IMPORT set to the path of\n")
    set(export "${export}# the export file.\n")
    set(export "${export}add_library(usd_ms SHARED IMPORTED)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY IMPORTED_LOCATION FIXME)\n")
    set(export "${export}#set_property(TARGET usd_ms PROPERTY IMPORTED_IMPLIB FIXME)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_COMPILE_DEFINITIONS $<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_LINK_LIBRARIES $<TARGET_PROPERTY:usd_m,INTERFACE_LINK_LIBRARIES>)\n")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
        CONTENT "${export}"
    )

    # Convenient name for building the monolithic library.
    add_custom_target(monolithic
        DEPENDS
            usd_m
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
            "${CMAKE_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
            "${CMAKE_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
        COMMAND ${CMAKE_COMMAND} -E echo Export file: ${CMAKE_BINARY_DIR}/usd-targets-$<CONFIG>.cmake
        COMMAND ${CMAKE_COMMAND} -E echo Import file: ${CMAKE_BINARY_DIR}/usd-imports-$<CONFIG>.cmake
    )
endfunction() # pxr_monolithic_epilogue

function(pxr_core_prologue)
    set(_building_core TRUE PARENT_SCOPE)
    if(PXR_BUILD_MONOLITHIC)
        set(_building_monolithic TRUE PARENT_SCOPE)
    endif()
endfunction() # pxr_core_prologue

function(pxr_core_epilogue)
    if(_building_core)
        if(_building_monolithic)
            pxr_monolithic_epilogue()
            set(_building_monolithic FALSE PARENT_SCOPE)
        endif()
        if(PXR_ENABLE_PYTHON_SUPPORT)
            pxr_setup_python()
        endif()
        set(_building_core FALSE PARENT_SCOPE)
    endif()
endfunction() # pxr_core_epilogue
