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
if (NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR "Unable to find Python executable - PySide not present")
    return()
endif()

# Prefer PySide2 over PySide
# Note: Windows does not support PySide2 with Python2.7
execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "-c" "import PySide2"
    RESULT_VARIABLE pySideImportResult 
)
if (pySideImportResult EQUAL 0)
    set(pySideImportResult "PySide2")
    set(pySideUIC pyside2-uic python2-pyside2-uic pyside2-uic-2.7)
endif()

# PySide2 not found OR PYSIDE explicitly requested
if (pySideImportResult EQUAL 1 OR PYSIDE_USE_PYSIDE)
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" "-c" "import PySide"
        RESULT_VARIABLE pySideImportResult 
    )
    if (pySideImportResult EQUAL 0)
        set(pySideImportResult "PySide")
        set(pySideUIC pyside-uic python2-pyside-uic pyside-uic-2.7)
    else()
        set(pySideImportResult 0)
    endif()
endif()

# If nothing is found, the result will be <VAR>-NOTFOUND.
find_program(PYSIDEUICBINARY NAMES ${pySideUIC} HINTS ${PYSIDE_BIN_DIR})

if (pySideImportResult)
    # False if the constant ends in the suffix -NOTFOUND.
    if (PYSIDEUICBINARY)
        message(STATUS "Found ${pySideImportResult}: with ${PYTHON_EXECUTABLE}, will use ${PYSIDEUICBINARY} for pyside-uic binary")
        set(PYSIDE_AVAILABLE True)
    else()
        message(STATUS "Found ${pySideImportResult} but NOT pyside-uic binary")
        set(PYSIDE_AVAILABLE False)
    endif()
else()
    if (PYSIDE_USE_PYSIDE)
        message(STATUS "Did not find PySide with ${PYTHON_EXECUTABLE}")
    else()
        message(STATUS "Did not find PySide2 with ${PYTHON_EXECUTABLE}")
    endif()
    set(PYSIDE_AVAILABLE False)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(PySide
    REQUIRED_VARS
        PYSIDE_AVAILABLE
        PYSIDEUICBINARY
)
