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

# Enable exception handling.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /EHsc")

# Standards compliant.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /Zc:rvalueCast /Zc:strictStrings")

# The /Zc:inline option strips out the "arch_ctor_<name>" symbols used for
# library initialization by ARCH_CONSTRUCTOR starting in Visual Studio 2019, 
# causing release builds to fail. Disable the option for this and later 
# versions.
# 
# For more details, see:
# https://developercommunity.visualstudio.com/content/problem/914943/zcinline-removes-extern-symbols-inside-anonymous-n.html
if (MSVC_VERSION GREATER_EQUAL 1920)
    set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /Zc:inline-")
else()
    set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /Zc:inline")
endif()

# Turn on all but informational warnings.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /W3")

# Warnings are errors in strict build mode.
if (${PXR_STRICT_BUILD_MODE})
    set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /WX")
endif()

# truncation from 'double' to 'float' due to matrix and vector classes in `Gf`
_disable_warning("4244")
_disable_warning("4305")

# conversion from size_t to int. While we don't want this enabled
# it's in the Python headers. So all the Python wrap code is affected.
_disable_warning("4267")

# no definition for inline function
# this affects Glf only
_disable_warning("4506")

# 'typedef ': ignored on left of '' when no variable is declared
# XXX:figure out why we need this
_disable_warning("4091")

# c:\python27\include\pymath.h(22): warning C4273: 'round': inconsistent dll linkage 
# XXX:figure out real fix
_disable_warning("4273")

# qualifier applied to function type has no meaning; ignored
# tbb/parallel_for_each.h
_disable_warning("4180")

# '<<': result of 32-bit shift implicitly converted to 64 bits
# tbb/enumerable_thread_specific.h
_disable_warning("4334")

# Disable warning C4996 regarding fopen(), strcpy(), etc.
_add_define("_CRT_SECURE_NO_WARNINGS")

# Disable warning C4996 regarding unchecked iterators for std::transform,
# std::copy, std::equal, et al.
_add_define("_SCL_SECURE_NO_WARNINGS")

# Make sure WinDef.h does not define min and max macros which
# will conflict with std::min() and std::max().
_add_define("NOMINMAX")

# Needed to prevent YY files trying to include unistd.h
# (which doesn't exist on Windows)
_add_define("YY_NO_UNISTD_H")

# Forces all libraries that have separate source to be linked as
# DLL's rather than static libraries on Microsoft Windows, unless
# explicitly told otherwise.
if (NOT Boost_USE_STATIC_LIBS)
    _add_define("BOOST_ALL_DYN_LINK")
endif()

if(${PXR_DEFINE_BOOST_DEBUG_PYTHON_FLAG})
    _add_define(BOOST_DEBUG_PYTHON)
    _add_define(BOOST_LINKING_PYTHON)
endif()

# Need half::_toFloat and half::_eLut.
_add_define("OPENEXR_DLL")

# These files require /bigobj compiler flag
#   Vt/arrayPyBuffer.cpp
#   Usd/crateFile.cpp
#   Usd/stage.cpp
# Until we can set the flag on a per file basis, we'll have to enable it
# for all translation units.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /bigobj")

# Enable PDB generation.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /Zi")

# Enable multiprocessor builds.
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /MP")
set(_PXR_CXX_FLAGS "${_PXR_CXX_FLAGS} /Gm-")

# Ignore LNK4221.  This happens when making an archive with a object file
# with no symbols in it.  We do this a lot because of a pattern of having
# a C++ source file for many header-only facilities, e.g. tf/bitUtils.cpp.
set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /IGNORE:4221")
