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
option(PXR_STRICT_BUILD_MODE "Turn on additional warnings. Enforce all warnings as errors." OFF)
option(PXR_VALIDATE_GENERATED_CODE "Validate script generated code" OFF)
option(PXR_HEADLESS_TEST_MODE "Disallow GUI based tests, useful for running under headless CI systems." OFF)
option(PXR_BUILD_TESTS "Build tests" ON)
option(PXR_BUILD_EXAMPLES "Build examples" ON)
option(PXR_BUILD_TUTORIALS "Build tutorials" ON)
option(PXR_BUILD_USD_TOOLS "Build commandline tools" ON)
option(PXR_BUILD_IMAGING "Build imaging components" ON)
option(PXR_BUILD_EMBREE_PLUGIN "Build embree imaging plugin" OFF)
option(PXR_BUILD_OPENIMAGEIO_PLUGIN "Build OpenImageIO plugin" OFF)
option(PXR_BUILD_OPENCOLORIO_PLUGIN "Build OpenColorIO plugin" OFF)
option(PXR_BUILD_USD_IMAGING "Build USD imaging components" ON)
option(PXR_BUILD_USDVIEW "Build usdview" ON)
option(PXR_BUILD_ALEMBIC_PLUGIN "Build the Alembic plugin for USD" OFF)
option(PXR_BUILD_DRACO_PLUGIN "Build the Draco plugin for USD" OFF)
option(PXR_BUILD_PRMAN_PLUGIN "Build the PRMan imaging plugin" OFF)
option(PXR_ENABLE_MATERIALX_SUPPORT "Enable MaterialX support" OFF)
option(PXR_BUILD_DOCUMENTATION "Generate doxygen documentation" OFF)
option(PXR_ENABLE_PYTHON_SUPPORT "Enable Python based components for USD" ON)
option(PXR_USE_PYTHON_3 "Build Python bindings for Python 3" OFF)
option(PXR_ENABLE_HDF5_SUPPORT "Enable HDF5 backend in the Alembic plugin for USD" ON)
option(PXR_ENABLE_OSL_SUPPORT "Enable OSL (OpenShadingLanguage) based components" OFF)
option(PXR_ENABLE_PTEX_SUPPORT "Enable Ptex support" ON)
option(PXR_ENABLE_OPENVDB_SUPPORT "Enable OpenVDB support" OFF)
option(PXR_ENABLE_NAMESPACES "Enable C++ namespaces." ON)
option(PXR_DEFINE_BOOST_DEBUG_PYTHON_FLAG "Define BOOST_DEBUG_PYTHON flag." OFF)
option(PXR_PREFER_SAFETY_OVER_SPEED
       "Enable certain checks designed to avoid crashes or out-of-bounds memory reads with malformed input files.  These checks may negatively impact performance."
        ON)
option(PXR_USE_AR_2 "Use Asset Resolver (Ar) 2.0" OFF)

# Determine GFX api
# Metal only valid on Apple platforms
set(pxr_enable_metal "OFF")
if(APPLE)
    set(pxr_enable_metal "ON")
endif()
option(PXR_ENABLE_METAL_SUPPORT "Enable Metal based components" "${pxr_enable_metal}")
option(PXR_ENABLE_VULKAN_SUPPORT "Enable Vulkan based components" OFF)
option(PXR_ENABLE_GL_SUPPORT "Enable OpenGL based components" ON)

# Precompiled headers are a win on Windows, not on gcc.
set(pxr_enable_pch "OFF")
if(MSVC)
    set(pxr_enable_pch "ON")
endif()
option(PXR_ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers." "${pxr_enable_pch}")
set(PXR_PRECOMPILED_HEADER_NAME "pch.h"
    CACHE
    STRING
    "Default name of precompiled header files"
)

set(PXR_INSTALL_LOCATION ""
    CACHE
    STRING
    "Intended final location for plugin resource files."
)

set(PXR_OVERRIDE_PLUGINPATH_NAME ""
    CACHE
    STRING
    "Name of the environment variable that will be used to get plugin paths."
)

set(PXR_ALL_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built libraries."
)
set(PXR_STATIC_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built explicitly static libraries."
)
set(PXR_CORE_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built core libraries."
)
set(PXR_OBJECT_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all core libraries built as OBJECT libraries."
)

set(PXR_LIB_PREFIX ${CMAKE_SHARED_LIBRARY_PREFIX}
    CACHE
    STRING
    "Prefix for build library name"
)

option(BUILD_SHARED_LIBS "Build shared libraries." ON)
option(PXR_BUILD_MONOLITHIC "Build a monolithic library." OFF)
set(PXR_MONOLITHIC_IMPORT ""
    CACHE
    STRING
    "Path to cmake file that imports a usd_ms target"
)

set(PXR_EXTRA_PLUGINS ""
    CACHE
    INTERNAL
    "Aggregation of extra plugin directories containing a plugInfo.json.")

# Resolve options that depend on one another so that subsequent .cmake scripts
# all have the final value for these options.
if (${PXR_BUILD_USD_IMAGING} AND NOT ${PXR_BUILD_IMAGING})
    message(STATUS
        "Setting PXR_BUILD_USD_IMAGING=OFF because PXR_BUILD_IMAGING=OFF")
    set(PXR_BUILD_USD_IMAGING "OFF" CACHE BOOL "" FORCE)
endif()

if (${PXR_ENABLE_GL_SUPPORT} OR ${PXR_ENABLE_METAL_SUPPORT} OR ${PXR_ENABLE_VULKAN_SUPPORT})
    set(PXR_BUILD_GPU_SUPPORT "ON")
else()
    set(PXR_BUILD_GPU_SUPPORT "OFF")
endif()

if (${PXR_BUILD_USDVIEW})
    if (NOT ${PXR_BUILD_USD_IMAGING})
        message(STATUS
            "Setting PXR_BUILD_USDVIEW=OFF because "
            "PXR_BUILD_USD_IMAGING=OFF")
        set(PXR_BUILD_USDVIEW "OFF" CACHE BOOL "" FORCE)
    elseif (NOT ${PXR_ENABLE_PYTHON_SUPPORT})
        message(STATUS
            "Setting PXR_BUILD_USDVIEW=OFF because "
            "PXR_ENABLE_PYTHON_SUPPORT=OFF")
        set(PXR_BUILD_USDVIEW "OFF" CACHE BOOL "" FORCE)
    elseif (NOT ${PXR_BUILD_GPU_SUPPORT})
        message(STATUS
            "Setting PXR_BUILD_USDVIEW=OFF because "
            "PXR_BUILD_GPU_SUPPORT=OFF")
        set(PXR_BUILD_USDVIEW "OFF" CACHE BOOL "" FORCE)
    endif()
endif()

if (${PXR_BUILD_EMBREE_PLUGIN})
    if (NOT ${PXR_BUILD_IMAGING})
        message(STATUS
            "Setting PXR_BUILD_EMBREE_PLUGIN=OFF because PXR_BUILD_IMAGING=OFF")
        set(PXR_BUILD_EMBREE_PLUGIN "OFF" CACHE BOOL "" FORCE)
    elseif (NOT ${PXR_BUILD_GPU_SUPPORT})
        message(STATUS
            "Setting PXR_BUILD_EMBREE_PLUGIN=OFF because "
            "PXR_BUILD_GPU_SUPPORT=OFF")
        set(PXR_BUILD_EMBREE_PLUGIN "OFF" CACHE BOOL "" FORCE)
    endif()
endif()

if (${PXR_BUILD_PRMAN_PLUGIN})
    if (NOT ${PXR_BUILD_IMAGING})
        message(STATUS
            "Setting PXR_BUILD_PRMAN_PLUGIN=OFF because PXR_BUILD_IMAGING=OFF")
        set(PXR_BUILD_PRMAN_PLUGIN "OFF" CACHE BOOL "" FORCE)
    endif()
endif()

# Error out if user is building monolithic library on windows with draco plugin
# enabled. This currently results in missing symbols.
if (${PXR_BUILD_DRACO_PLUGIN} AND ${PXR_BUILD_MONOLITHIC} AND WIN32)
    message(FATAL_ERROR 
        "Draco plugin can not be enabled for monolithic builds on Windows")
endif()
