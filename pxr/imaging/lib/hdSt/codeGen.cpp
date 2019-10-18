//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/glf/contextCaps.h"

#include "pxr/imaging/hdSt/codeGen.h"
#include "pxr/imaging/hdSt/geometricShader.h"
#include "pxr/imaging/hdSt/glConversions.h"
#include "pxr/imaging/hdSt/glslProgram.h"
#include "pxr/imaging/hdSt/glUtils.h"
#include "pxr/imaging/hdSt/package.h"
#include "pxr/imaging/hdSt/resourceBinder.h"
#include "pxr/imaging/hdSt/shaderCode.h"

#include "pxr/imaging/hd/binding.h"
#include "pxr/imaging/hd/instanceRegistry.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include "pxr/imaging/hio/glslfx.h"

#include "pxr/base/tf/iterator.h"
#include "pxr/base/tf/staticTokens.h"

#include <boost/functional/hash.hpp>

#include <sstream>

#include <opensubdiv/osd/glslPatchShaderSource.h>

PXR_NAMESPACE_OPEN_SCOPE


TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((_double, "double"))
    ((_float, "float"))
    ((_int, "int"))
    (hd_vec3)
    (hd_vec3_get)
    (hd_vec3_set)
    (hd_ivec3)
    (hd_ivec3_get)
    (hd_ivec3_set)
    (hd_dvec3)
    (hd_dvec3_get)
    (hd_dvec3_set)
    (hd_mat3)
    (hd_mat3_get)
    (hd_mat3_set)
    (hd_dmat3)
    (hd_dmat3_get)
    (hd_dmat3_set)
    (hd_vec4_2_10_10_10_get)
    (hd_vec4_2_10_10_10_set)
    (inPrimvars)
    (ivec2)
    (ivec3)
    (ivec4)
    (outPrimvars)
    (vec2)
    (vec3)
    (vec4)
    (dvec2)
    (dvec3)
    (dvec4)
    (mat3)
    (mat4)
    (dmat3)
    (dmat4)
    (packed_2_10_10_10)
    ((ptexTextureSampler, "ptexTextureSampler"))
    (isamplerBuffer)
    (samplerBuffer)
);

HdSt_CodeGen::HdSt_CodeGen(HdSt_GeometricShaderPtr const &geometricShader,
                       HdStShaderCodeSharedPtrVector const &shaders)
    : _geometricShader(geometricShader), _shaders(shaders)
{
    TF_VERIFY(geometricShader);
}

HdSt_CodeGen::HdSt_CodeGen(HdStShaderCodeSharedPtrVector const &shaders)
    : _geometricShader(), _shaders(shaders)
{
}

HdSt_CodeGen::ID
HdSt_CodeGen::ComputeHash() const
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    ID hash = _geometricShader ? _geometricShader->ComputeHash() : 0;
    boost::hash_combine(hash, _metaData.ComputeHash());
    boost::hash_combine(hash, HdStShaderCode::ComputeHash(_shaders));

    return hash;
}

static
std::string
_GetPtexTextureShaderSource()
{
    static std::string source =
        HioGlslfx(HdStPackagePtexTextureShader()).GetSource(
            _tokens->ptexTextureSampler);
    return source;
}

// TODO: Shuffle code to remove these declarations.
static void _EmitDeclaration(std::stringstream &str,
                             TfToken const &name,
                             TfToken const &type,
                             HdBinding const &binding,
                             int arraySize=0);

static void _EmitStructAccessor(std::stringstream &str,
                                TfToken const &structName,
                                TfToken const &name,
                                TfToken const &type,
                                int arraySize,
                                const char *index);

static void _EmitComputeAccessor(std::stringstream &str,
                                 TfToken const &name,
                                 TfToken const &type,
                                 HdBinding const &binding,
                                 const char *index);

static void _EmitComputeMutator(std::stringstream &str,
                                TfToken const &name,
                                TfToken const &type,
                                HdBinding const &binding,
                                const char *index);

static void _EmitAccessor(std::stringstream &str,
                          TfToken const &name,
                          TfToken const &type,
                          HdBinding const &binding,
                          const char *index=NULL);
/*
  1. If the member is a scalar consuming N basic machine units,
  the base alignment is N.
  2. If the member is a two- or four-component vector with components
  consuming N basic machine units, the base alignment is 2N or 4N,
  respectively.
  3. If the member is a three-component vector with components
  consuming N basic machine units, the base alignment is 4N.
  4. If the member is an array of scalars or vectors, the base
  alignment and array stride are set to match the base alignment of
  a single array element, according to rules (1), (2), and (3), and
  rounded up to the base alignment of a vec4. The array may have
  padding at the end; the base offset of the member following the
  array is rounded up to the next multiple of the base alignment.

  9. If the member is a structure, the base alignment of the structure
  is <N>, where <N> is the largest base alignment value of any of its
  members, and rounded up to the base alignment of a vec4. The
  individual members of this sub-structure are then assigned offsets
  by applying this set of rules recursively, where the base offset of
  the first member of the sub-structure is equal to the aligned offset
  of the structure. The structure may have padding at the end; the
  base offset of the member following the sub-structure is rounded up
  to the next multiple of the base alignment of the structure.

  When using the std430 storage layout, shader storage blocks will be
  laid out in buffer storage identically to uniform and shader storage
  blocks using the std140 layout, except that the base alignment and
  stride of arrays of scalars and vectors in rule 4 and of structures
  in rule 9 are not rounded up a multiple of the base alignment of a
  vec4.

  i.e. rule 3 is still applied in std430. we use an array of 3-element
  struct instead of vec3/dvec3 to avoid this undesirable padding.

  struct instanceData0 {
    float x, y, z;
  }
  buffer buffer0 {
    instanceData0 data[];
  };
*/

static const char *
_GetPackedTypeDefinitions()
{
    return "struct hd_ivec3 { int    x, y, z; };\n"
           "struct hd_vec3  { float  x, y, z; };\n"
           "struct hd_dvec3 { double x, y, z; };\n"
           "struct hd_mat3  { float  m00, m01, m02,\n"
           "                         m10, m11, m12,\n"
           "                         m20, m21, m22; };\n"
           "struct hd_dmat3 { double m00, m01, m02,\n"
           "                         m10, m11, m12,\n"
           "                         m20, m21, m22; };\n"
           "ivec3 hd_ivec3_get(hd_ivec3 v) { return ivec3(v.x, v.y, v.z); }\n"
           "ivec3 hd_ivec3_get(ivec3 v)    { return v; }\n"
           "vec3  hd_vec3_get(hd_vec3 v)   { return vec3(v.x, v.y, v.z); }\n"
           "vec3  hd_vec3_get(vec3 v)      { return v; }\n"
           "dvec3 hd_dvec3_get(hd_dvec3 v) { return dvec3(v.x, v.y, v.z); }\n"
           "dvec3 hd_dvec3_get(dvec3 v)    { return v; }\n"
           "mat3  hd_mat3_get(hd_mat3 v)   { return mat3(v.m00, v.m01, v.m02,\n"
           "                                             v.m10, v.m11, v.m12,\n"
           "                                             v.m20, v.m21, v.m22); }\n"
           "mat3  hd_mat3_get(mat3 v)      { return v; }\n"
           "dmat3 hd_dmat3_get(hd_dmat3 v) { return dmat3(v.m00, v.m01, v.m02,\n"
           "                                              v.m10, v.m11, v.m12,\n"
           "                                              v.m20, v.m21, v.m22); }\n"
           "dmat3 hd_dmat3_get(dmat3 v)    { return v; }\n"
           "hd_ivec3 hd_ivec3_set(hd_ivec3 v) { return v; }\n"
           "hd_ivec3 hd_ivec3_set(ivec3 v)    { return hd_ivec3(v.x, v.y, v.z); }\n"
           "hd_vec3 hd_vec3_set(hd_vec3 v)    { return v; }\n"
           "hd_vec3 hd_vec3_set(vec3 v)       { return hd_vec3(v.x, v.y, v.z); }\n"
           "hd_dvec3 hd_dvec3_set(hd_dvec3 v) { return v; }\n"
           "hd_dvec3 hd_dvec3_set(dvec3 v)    { return hd_dvec3(v.x, v.y, v.z); }\n"
           "hd_mat3  hd_mat3_set(hd_mat3 v)   { return v; }\n"
           "hd_mat3  hd_mat3_set(mat3 v)      { return hd_mat3(v[0][0], v[0][1], v[0][2],\n"
           "                                                   v[1][0], v[1][1], v[1][2],\n"
           "                                                   v[2][0], v[2][1], v[2][2]); }\n"
           "hd_dmat3 hd_dmat3_set(hd_dmat3 v) { return v; }\n"
           "hd_dmat3 hd_dmat3_set(dmat3 v)    { return hd_dmat3(v[0][0], v[0][1], v[0][2],\n"
           "                                                    v[1][0], v[1][1], v[1][2],\n"
           "                                                    v[2][0], v[2][1], v[2][2]); }\n"
        // helper functions for 410 specification
        // applying a swizzle operator on int and float is not allowed in 410.
           "int hd_int_get(int v)          { return v; }\n"
           "int hd_int_get(ivec2 v)        { return v.x; }\n"
           "int hd_int_get(ivec3 v)        { return v.x; }\n"
           "int hd_int_get(ivec4 v)        { return v.x; }\n"
        // udim helper function
            "vec3 hd_sample_udim(vec2 v) {\n"
            "vec2 vf = floor(v);\n"
            "return vec3(v.x - vf.x, v.y - vf.y, clamp(vf.x, 0.0, 10.0) + 10.0 * vf.y);\n"
            "}\n"

        // -------------------------------------------------------------------
        // Packed HdType implementation.

        // XXX: this could be improved!
           "vec4 hd_vec4_2_10_10_10_get(int v) {\n"
           "    ivec4 unpacked = ivec4((v & 0x3ff) << 22, (v & 0xffc00) << 12,\n"
           "                           (v & 0x3ff00000) << 2, (v & 0xc0000000));\n"
           "    return vec4(unpacked) / 2147483647.0; }\n"
           "int hd_vec4_2_10_10_10_set(vec4 v) {\n"
           "    return ( (int(v.x * 511.0) & 0x3ff) |\n"
           "            ((int(v.y * 511.0) & 0x3ff) << 10) |\n"
           "            ((int(v.z * 511.0) & 0x3ff) << 20) |\n"
           "            ((int(v.w) & 0x1) << 30)); }\n";
}

static TfToken const &
_GetPackedType(TfToken const &token, bool packedAlignment)
{
    if (packedAlignment) {
        if (token == _tokens->ivec3) {
            return _tokens->hd_ivec3;
        } else if (token == _tokens->vec3) {
            return _tokens->hd_vec3;
        } else if (token == _tokens->dvec3) {
            return _tokens->hd_dvec3;
        } else if (token == _tokens->mat3) {
            return _tokens->hd_mat3;
        } else if (token == _tokens->dmat3) {
            return _tokens->hd_dmat3;
        }
    }
    if (token == _tokens->packed_2_10_10_10) {
        return _tokens->_int;
    }
    return token;
}

static TfToken const &
_GetUnpackedType(TfToken const &token, bool packedAlignment)
{
    if (token == _tokens->packed_2_10_10_10) {
        return _tokens->vec4;
    }
    return token;
}

static TfToken const &
_GetPackedTypeAccessor(TfToken const &token, bool packedAlignment)
{
    if (packedAlignment) {
        if (token == _tokens->ivec3) {
            return _tokens->hd_ivec3_get;
        } else if (token == _tokens->vec3) {
            return _tokens->hd_vec3_get;
        } else if (token == _tokens->dvec3) {
            return _tokens->hd_dvec3_get;
        } else if (token == _tokens->mat3) {
            return _tokens->hd_mat3_get;
        } else if (token == _tokens->dmat3) {
            return _tokens->hd_dmat3_get;
        }
    }
    if (token == _tokens->packed_2_10_10_10) {
        return _tokens->hd_vec4_2_10_10_10_get;
    }
    return token;
}

static TfToken const &
_GetPackedTypeMutator(TfToken const &token, bool packedAlignment)
{
    if (packedAlignment) {
        if (token == _tokens->ivec3) {
            return _tokens->hd_ivec3_set;
        } else if (token == _tokens->vec3) {
            return _tokens->hd_vec3_set;
        } else if (token == _tokens->dvec3) {
            return _tokens->hd_dvec3_set;
        } else if (token == _tokens->mat3) {
            return _tokens->hd_mat3_set;
        } else if (token == _tokens->dmat3) {
            return _tokens->hd_dmat3_set;
        }
    }
    if (token == _tokens->packed_2_10_10_10) {
        return _tokens->hd_vec4_2_10_10_10_set;
    }
    return token;
}

static TfToken const &
_GetFlatType(TfToken const &token)
{
    if (token == _tokens->ivec2) {
        return _tokens->_int;
    } else if (token == _tokens->ivec3) {
        return _tokens->_int;
    } else if (token == _tokens->ivec4) {
        return _tokens->_int;
    } else if (token == _tokens->vec2) {
        return _tokens->_float;
    } else if (token == _tokens->vec3) {
        return _tokens->_float;
    } else if (token == _tokens->vec4) {
        return _tokens->_float;
    } else if (token == _tokens->dvec2) {
        return _tokens->_double;
    } else if (token == _tokens->dvec3) {
        return _tokens->_double;
    } else if (token == _tokens->dvec4) {
        return _tokens->_double;
    } else if (token == _tokens->mat3) {
        return _tokens->_float;
    } else if (token == _tokens->mat4) {
        return _tokens->_float;
    } else if (token == _tokens->dmat3) {
        return _tokens->_double;
    } else if (token == _tokens->dmat4) {
        return _tokens->_double;
    }
    return token;
}

static TfToken const &
_GetSamplerBufferType(TfToken const &token)
{
    if (token == _tokens->_int  ||
        token == _tokens->ivec2 ||
        token == _tokens->ivec3 ||
        token == _tokens->ivec4 ||
        token == _tokens->packed_2_10_10_10) {
        return _tokens->isamplerBuffer;
    } else {
        return _tokens->samplerBuffer;
    }
}

namespace {
    struct LayoutQualifier {
        LayoutQualifier(HdBinding const &binding) :
            binding(binding) {
        }
        friend std::ostream & operator << (std::ostream & out,
                                           const LayoutQualifier &lq);
        HdBinding binding;
    };
    std::ostream & operator << (std::ostream & out, const LayoutQualifier &lq)
    {
        GlfContextCaps const &caps = GlfContextCaps::GetInstance();
        int location = lq.binding.GetLocation();

        switch (lq.binding.GetType()) {
        case HdBinding::VERTEX_ATTR:
        case HdBinding::DRAW_INDEX:
        case HdBinding::DRAW_INDEX_INSTANCE:
        case HdBinding::DRAW_INDEX_INSTANCE_ARRAY:
            // ARB_explicit_attrib_location is supported since GL 3.3
            out << "layout (location = " << location << ") ";
            break;
        case HdBinding::UNIFORM:
        case HdBinding::UNIFORM_ARRAY:
        case HdBinding::TBO:
        case HdBinding::BINDLESS_UNIFORM:
        case HdBinding::BINDLESS_SSBO_RANGE:
        case HdBinding::TEXTURE_2D:
        case HdBinding::BINDLESS_TEXTURE_2D:
        case HdBinding::TEXTURE_3D:
        case HdBinding::BINDLESS_TEXTURE_3D:
        case HdBinding::TEXTURE_UDIM_ARRAY:
        case HdBinding::BINDLESS_TEXTURE_UDIM_ARRAY:
        case HdBinding::TEXTURE_UDIM_LAYOUT:
        case HdBinding::BINDLESS_TEXTURE_UDIM_LAYOUT:
        case HdBinding::TEXTURE_PTEX_TEXEL:
        case HdBinding::TEXTURE_PTEX_LAYOUT:
            if (caps.explicitUniformLocation) {
                out << "layout (location = " << location << ") ";
            }
            break;
        case HdBinding::SSBO:
            out << "layout (std430, binding = " << location << ") ";
            break;
        case HdBinding::UBO:
            if (caps.shadingLanguage420pack) {
                out << "layout (std140, binding = " << location << ") ";
            } else {
                out << "layout (std140)";
            }
            break;
        default:
            break;
        }
        return out;
    }
}

HdStGLSLProgramSharedPtr
HdSt_CodeGen::Compile()
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // shader sources
    // geometric shader owns main()
    std::string vertexShader =
        _geometricShader->GetSource(HdShaderTokens->vertexShader);
    std::string tessControlShader =
        _geometricShader->GetSource(HdShaderTokens->tessControlShader);
    std::string tessEvalShader =
        _geometricShader->GetSource(HdShaderTokens->tessEvalShader);
    std::string geometryShader =
        _geometricShader->GetSource(HdShaderTokens->geometryShader);
    std::string fragmentShader =
        _geometricShader->GetSource(HdShaderTokens->fragmentShader);

    bool hasVS  = (!vertexShader.empty());
    bool hasTCS = (!tessControlShader.empty());
    bool hasTES = (!tessEvalShader.empty());
    bool hasGS  = (!geometryShader.empty());
    bool hasFS  = (!fragmentShader.empty());

    // create GLSL program.
    HdStGLSLProgramSharedPtr glslProgram(
        new HdStGLSLProgram(HdTokens->drawingShader));

    // initialize autogen source buckets
    _genCommon.str(""); _genVS.str(""); _genTCS.str(""); _genTES.str("");
    _genGS.str(""); _genFS.str(""); _genCS.str("");
    _procVS.str(""); _procTCS.str(""), _procTES.str(""), _procGS.str("");

    // GLSL version.
    GlfContextCaps const &caps = GlfContextCaps::GetInstance();
    _genCommon << "#version " << caps.glslVersion << "\n";

    if (caps.bindlessBufferEnabled) {
        _genCommon << "#extension GL_NV_shader_buffer_load : require\n"
                   << "#extension GL_NV_gpu_shader5 : require\n";
    }
    if (caps.bindlessTextureEnabled) {
        _genCommon << "#extension GL_ARB_bindless_texture : require\n";
    }
    if (caps.glslVersion < 460 && caps.shaderDrawParametersEnabled) {
        _genCommon << "#extension GL_ARB_shader_draw_parameters : require\n";
    }
    if (caps.glslVersion < 430 && caps.explicitUniformLocation) {
        _genCommon << "#extension GL_ARB_explicit_uniform_location : require\n";
    }
    if (caps.glslVersion < 420 && caps.shadingLanguage420pack) {
        _genCommon << "#extension GL_ARB_shading_language_420pack : require\n";
    }

    // Used in glslfx files to determine if it is using new/old
    // imaging system. It can also be used as API guards when
    // we need new versions of Storm shading. 
    _genCommon << "#define HD_SHADER_API " << HD_SHADER_API << "\n";

    // XXX: this is a hacky workaround for experimental support of GL 3.3
    //      the double is used in hd_dvec3 akin, so we are likely able to
    //      refactor that helper functions.
    if (caps.glslVersion < 400) {
        _genCommon << "#define double float\n"
                   << "#define dvec2 vec2\n"
                   << "#define dvec3 vec3\n"
                   << "#define dvec4 vec4\n"
                   << "#define dmat4 mat4\n";
    }

    // XXX: this macro is still used in GlobalUniform.
    _genCommon << "#define MAT4 " <<
        HdStGLConversions::GetGLSLTypename(
            HdVtBufferSource::GetDefaultMatrixType()) << "\n";
    // a trick to tightly pack unaligned data (vec3, etc) into SSBO/UBO.
    _genCommon << _GetPackedTypeDefinitions();

    // ------------------
    // Custom Buffer Bindings
    // ----------------------
    // For custom buffer bindings, more code can be generated; a full spec is
    // emitted based on the binding declaration.
    TF_FOR_ALL(binDecl, _metaData.customBindings) {
        _genCommon << "#define "
                   << binDecl->name << "_Binding " 
                   << binDecl->binding.GetLocation() << "\n";
        _genCommon << "#define HD_HAS_" << binDecl->name << " 1\n";

        // typeless binding doesn't need declaration nor accessor.
        if (binDecl->dataType.IsEmpty()) continue;

        _EmitDeclaration(_genCommon,
                     binDecl->name,
                     binDecl->dataType,
                     binDecl->binding);
        _EmitAccessor(_genCommon,
                      binDecl->name,
                      binDecl->dataType,
                      binDecl->binding,
                      (binDecl->binding.GetType() == HdBinding::UNIFORM)
                      ? NULL : "localIndex");
    }

    std::stringstream declarations;
    std::stringstream accessors;
    TF_FOR_ALL(it, _metaData.customInterleavedBindings) {
        // note: _constantData has been sorted by offset in HdSt_ResourceBinder.
        // XXX: not robust enough, should consider padding and layouting rules
        // to match with the logic in HdInterleavedMemoryManager if we
        // want to use a layouting policy other than default padding.

        HdBinding binding = it->first;
        TfToken typeName(TfStringPrintf("CustomBlockData%d", binding.GetValue()));
        TfToken varName = it->second.blockName;

        declarations << "struct " << typeName << " {\n";

        // dbIt is StructEntry { name, dataType, offset, numElements }
        TF_FOR_ALL (dbIt, it->second.entries) {
            _genCommon << "#define HD_HAS_" << dbIt->name << " 1\n";
            declarations << "  " << _GetPackedType(dbIt->dataType, false)
                         << " " << dbIt->name;
            if (dbIt->arraySize > 1) {
                _genCommon << "#define HD_NUM_" << dbIt->name
                           << " " << dbIt->arraySize << "\n";
                declarations << "[" << dbIt->arraySize << "]";
            }
            declarations <<  ";\n";

            _EmitStructAccessor(accessors, varName, 
                                dbIt->name, dbIt->dataType, dbIt->arraySize,
                                NULL);
        }

        declarations << "};\n";
        _EmitDeclaration(declarations, varName, typeName, binding);
    }
    _genCommon << declarations.str()
               << accessors.str();

    // HD_NUM_PATCH_VERTS, HD_NUM_PRIMTIIVE_VERTS
    if (_geometricShader->IsPrimTypePatches()) {
        _genCommon << "#define HD_NUM_PATCH_VERTS "
                   << _geometricShader->GetPrimitiveIndexSize() << "\n";
    }
    _genCommon << "#define HD_NUM_PRIMITIVE_VERTS "
               << _geometricShader->GetNumPrimitiveVertsForGeometryShader()
               << "\n";

    // include Glf ptex utility (if needed)
    TF_FOR_ALL (it, _metaData.shaderParameterBinding) {
        HdBinding::Type bindingType = it->first.GetType();
        if (bindingType == HdBinding::TEXTURE_PTEX_TEXEL ||
            bindingType == HdBinding::BINDLESS_TEXTURE_PTEX_TEXEL) {
            _genCommon << _GetPtexTextureShaderSource();
            break;
        }
    }

    TF_FOR_ALL (it, _metaData.topologyVisibilityData) {
        TF_FOR_ALL (pIt, it->second.entries) {
            _genCommon << "#define HD_HAS_" << pIt->name  << " 1\n";
        }
    }

    // primvar existence macros

    // XXX: this is temporary, until we implement the fallback value definition
    // for any primvars used in glslfx.
    // Note that this #define has to be considered in the hash computation
    // since it changes the source code. However we have already combined the
    // entries of instanceData into the hash value, so it's not needed to be
    // added separately, at least in current usage.
    TF_FOR_ALL (it, _metaData.constantData) {
        TF_FOR_ALL (pIt, it->second.entries) {
            _genCommon << "#define HD_HAS_" << pIt->name << " 1\n";
        }
    }
    TF_FOR_ALL (it, _metaData.instanceData) {
        _genCommon << "#define HD_HAS_INSTANCE_" << it->second.name << " 1\n";
        _genCommon << "#define HD_HAS_"
                   << it->second.name << "_" << it->second.level << " 1\n";
    }
    _genCommon << "#define HD_INSTANCER_NUM_LEVELS "
               << _metaData.instancerNumLevels << "\n"
               << "#define HD_INSTANCE_INDEX_WIDTH "
               << (_metaData.instancerNumLevels+1) << "\n"; 
   if (!_geometricShader->IsPrimTypePoints()) {
      TF_FOR_ALL (it, _metaData.elementData) {
        _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
      }
      if (hasGS) {
        TF_FOR_ALL (it, _metaData.fvarData) {
           _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
        }
      }
    }
    TF_FOR_ALL (it, _metaData.vertexData) {
        _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
    }
    TF_FOR_ALL (it, _metaData.shaderParameterBinding) {

        // XXX: HdBinding::PRIMVAR_REDIRECT won't define an accessor if it's
        // an alias of like-to-like, so we want to suppress the HD_HAS_* flag
        // as well.
        HdBinding::Type bindingType = it->first.GetType();
        if (bindingType == HdBinding::PRIMVAR_REDIRECT) {
            if (it->second.name != it->second.inPrimvars[0]) {
                _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
            }
        } else {
            _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
        }
    }

    TF_FOR_ALL (it, _metaData.fieldRedirectBinding) {
        HdBinding::Type bindingType = it->first.GetType();
        if (bindingType == HdBinding::FIELD_REDIRECT) {
            _genCommon << "#define HD_HAS_" << it->second.name << " 1\n";
        }
    }

    // mixin shaders
    _genCommon << _geometricShader->GetSource(HdShaderTokens->commonShaderSource);
    TF_FOR_ALL(it, _shaders) {
        _genCommon << (*it)->GetSource(HdShaderTokens->commonShaderSource);
    }

    // prep interstage plumbing function
    _procVS  << "void ProcessPrimvars() {\n";
    _procTCS << "void ProcessPrimvars() {\n";
    _procTES << "void ProcessPrimvars(vec4 basis, int i0, int i1, int i2, int i3) {\n";
    // geometry shader plumbing
    switch(_geometricShader->GetPrimitiveType())
    {
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_QUADS:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_TRIANGLES:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BSPLINE:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BOXSPLINETRIANGLE:
        {
            // patch interpolation
            _procGS << "vec4 GetPatchCoord(int index);\n"
                    << "void ProcessPrimvars(int index) {\n"
                    << "   vec2 localST = GetPatchCoord(index).xy;\n";
            break;
        }

        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_QUADS:
        {
            // quad interpolation
            _procGS  << "void ProcessPrimvars(int index) {\n"
                     << "   vec2 lut[4] = vec2[4](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1));\n"
                     << "   vec2 localST = lut[index];\n";
            break;
        }

        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_TRIANGLES:
        {
            // barycentric interpolation
             _procGS  << "void ProcessPrimvars(int index) {\n"
                      << "   vec2 lut[3] = vec2[3](vec2(0,0), vec2(1,0), vec2(0,1));\n"
                      << "   vec2 localST = lut[index];\n";
            break;
        }

        default: // points, basis curves
            // do nothing. no additional code needs to be generated.
            ;
    }

    // generate drawing coord and accessors
    _GenerateDrawingCoord();

    // generate primvars
    _GenerateConstantPrimvar();
    _GenerateInstancePrimvar();
    _GenerateElementPrimvar();
    _GenerateVertexAndFaceVaryingPrimvar(hasGS);

    //generate shader parameters
    _GenerateShaderParameters();

    _GenerateTopologyVisibilityParameters();

    // finalize buckets
    _procVS  << "}\n";
    _procGS  << "}\n";
    _procTCS << "}\n";
    _procTES << "}\n";

    // insert interstage primvar plumbing procs into genVS/TCS/TES/GS
    _genVS  << _procVS.str();
    _genTCS << _procTCS.str();
    _genTES << _procTES.str();
    _genGS  << _procGS.str();


    // other shaders (renderpass, lighting, surface) first
    TF_FOR_ALL(it, _shaders) {
        HdStShaderCodeSharedPtr const &shader = *it;
        if (hasVS)
            _genVS  << shader->GetSource(HdShaderTokens->vertexShader);
        if (hasTCS)
            _genTCS << shader->GetSource(HdShaderTokens->tessControlShader);
        if (hasTES)
            _genTES << shader->GetSource(HdShaderTokens->tessEvalShader);
        if (hasGS)
            _genGS  << shader->GetSource(HdShaderTokens->geometryShader);
        if (hasFS)
            _genFS  << shader->GetSource(HdShaderTokens->fragmentShader);
    }

    // OpenSubdiv tessellation shader (if required)
    if (tessControlShader.find("OsdPerPatchVertexBezier") != std::string::npos) {
        _genTCS << OpenSubdiv::Osd::GLSLPatchShaderSource::GetCommonShaderSource();
        _genTCS << "MAT4 GetWorldToViewMatrix();\n";
        _genTCS << "MAT4 GetProjectionMatrix();\n";
        _genTCS << "float GetTessLevel();\n";
        // we apply modelview in the vertex shader, so the osd shaders doesn't need
        // to apply again.
        _genTCS << "mat4 OsdModelViewMatrix() { return mat4(1); }\n";
        _genTCS << "mat4 OsdProjectionMatrix() { return mat4(GetProjectionMatrix()); }\n";
        _genTCS << "int OsdPrimitiveIdBase() { return 0; }\n";
        _genTCS << "float OsdTessLevel() { return GetTessLevel(); }\n";
    }
    if (tessEvalShader.find("OsdPerPatchVertexBezier") != std::string::npos) {
        _genTES << OpenSubdiv::Osd::GLSLPatchShaderSource::GetCommonShaderSource();
        _genTES << "mat4 OsdModelViewMatrix() { return mat4(1); }\n";
    }
    if (geometryShader.find("OsdInterpolatePatchCoord") != std::string::npos) {
        _genGS <<  OpenSubdiv::Osd::GLSLPatchShaderSource::GetCommonShaderSource();
    }

    // geometric shader
    _genVS  << vertexShader;
    _genTCS << tessControlShader;
    _genTES << tessEvalShader;
    _genGS  << geometryShader;
    _genFS  << fragmentShader;

    // Sanity check that if you provide a control shader, you have also provided
    // an evaluation shader (and vice versa)
    if (hasTCS ^ hasTES) {
        TF_CODING_ERROR(
            "tessControlShader and tessEvalShader must be provided together.");
        hasTCS = hasTES = false;
    };

    bool shaderCompiled = false;
    // compile shaders
    // note: _vsSource, _fsSource etc are used for diagnostics (see header)
    if (hasVS) {
        _vsSource = _genCommon.str() + _genVS.str();
        if (!glslProgram->CompileShader(GL_VERTEX_SHADER, _vsSource)) {
            return HdStGLSLProgramSharedPtr();
        }
        shaderCompiled = true;
    }
    if (hasFS) {
        _fsSource = _genCommon.str() + _genFS.str();
        if (!glslProgram->CompileShader(GL_FRAGMENT_SHADER, _fsSource)) {
            return HdStGLSLProgramSharedPtr();
        }
        shaderCompiled = true;
    }
    if (hasTCS) {
        _tcsSource = _genCommon.str() + _genTCS.str();
        if (!glslProgram->CompileShader(GL_TESS_CONTROL_SHADER, _tcsSource)) {
            return HdStGLSLProgramSharedPtr();
        }
        shaderCompiled = true;
    }
    if (hasTES) {
        _tesSource = _genCommon.str() + _genTES.str();
        if (!glslProgram->CompileShader(
                    GL_TESS_EVALUATION_SHADER, _tesSource)) {
            return HdStGLSLProgramSharedPtr();
        }
        shaderCompiled = true;
    }
    if (hasGS) {
        _gsSource = _genCommon.str() + _genGS.str();
        if (!glslProgram->CompileShader(GL_GEOMETRY_SHADER, _gsSource)) {
            return HdStGLSLProgramSharedPtr();
        }
        shaderCompiled = true;
    }

    if (!shaderCompiled) {
        return HdStGLSLProgramSharedPtr();
    }

    return glslProgram;
}

HdStGLSLProgramSharedPtr
HdSt_CodeGen::CompileComputeProgram()
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // initialize autogen source buckets
    _genCommon.str(""); _genVS.str(""); _genTCS.str(""); _genTES.str("");
    _genGS.str(""); _genFS.str(""); _genCS.str("");
    _procVS.str(""); _procTCS.str(""), _procTES.str(""), _procGS.str("");
    
    // GLSL version.
    GlfContextCaps const &caps = GlfContextCaps::GetInstance();
    _genCommon << "#version " << caps.glslVersion << "\n";

    if (caps.bindlessBufferEnabled) {
        _genCommon << "#extension GL_NV_shader_buffer_load : require\n"
                   << "#extension GL_NV_gpu_shader5 : require\n";
    }
    if (caps.bindlessTextureEnabled) {
        _genCommon << "#extension GL_ARB_bindless_texture : require\n";
    }
    if (caps.glslVersion < 430 && caps.explicitUniformLocation) {
        _genCommon << "#extension GL_ARB_explicit_uniform_location : require\n";
    }
    if (caps.glslVersion < 420 && caps.shadingLanguage420pack) {
        _genCommon << "#extension GL_ARB_shading_language_420pack : require\n";
    }

    // default workgroup size (must follow #extension directives)
    _genCommon << "layout(local_size_x = 1, local_size_y = 1) in;\n";

    // Used in glslfx files to determine if it is using new/old
    // imaging system. It can also be used as API guards when
    // we need new versions of Storm shading. 
    _genCommon << "#define HD_SHADER_API " << HD_SHADER_API << "\n";    

    // a trick to tightly pack unaligned data (vec3, etc) into SSBO/UBO.
    _genCommon << _GetPackedTypeDefinitions();
    
    std::stringstream uniforms;
    std::stringstream declarations;
    std::stringstream accessors;
    
    uniforms << "// Uniform block\n";

    HdBinding uboBinding(HdBinding::UBO, 0);
    uniforms << LayoutQualifier(uboBinding);
    uniforms << "uniform ubo_" << uboBinding.GetLocation() << " {\n";

    accessors << "// Read-Write Accessors & Mutators\n";
    uniforms << "    int vertexOffset;       // offset in aggregated buffer\n";
    TF_FOR_ALL(it, _metaData.computeReadWriteData) {
        TfToken const &name = it->second.name;
        HdBinding const &binding = it->first;
        TfToken const &dataType = it->second.dataType;

        // For now, SSBO bindings use a flat type encoding.
        TfToken declDataType =
            (binding.GetType() == HdBinding::SSBO
                ? _GetFlatType(dataType) : dataType);
        
        uniforms << "    int " << name << "Offset;\n";
        uniforms << "    int " << name << "Stride;\n";
        
        _EmitDeclaration(declarations,
                name,
                declDataType,
                binding, 0);
        // getter & setter
        {
            std::stringstream indexing;
            indexing << "(localIndex + vertexOffset)"
                     << " * " << name << "Stride"
                     << " + " << name << "Offset";
            _EmitComputeAccessor(accessors, name, dataType, binding,
                    indexing.str().c_str());
            _EmitComputeMutator(accessors, name, dataType, binding,
                    indexing.str().c_str());
        }
    }
    accessors << "// Read-Only Accessors\n";
    // no vertex offset for constant data
    TF_FOR_ALL(it, _metaData.computeReadOnlyData) {
        TfToken const &name = it->second.name;
        HdBinding const &binding = it->first;
        TfToken const &dataType = it->second.dataType;
        
        // For now, SSBO bindings use a flat type encoding.
        TfToken declDataType =
            (binding.GetType() == HdBinding::SSBO
                ? _GetFlatType(dataType) : dataType);

        uniforms << "    int " << name << "Offset;\n";
        uniforms << "    int " << name << "Stride;\n";

        _EmitDeclaration(declarations,
                name,
                declDataType,
                binding, 0);
        // getter
        {
            std::stringstream indexing;
            // no vertex offset for constant data
            indexing << "(localIndex)"
                     << " * " << name << "Stride"
                     << " + " << name << "Offset";
            _EmitComputeAccessor(accessors, name, dataType, binding,
                    indexing.str().c_str());
        }
    }
    uniforms << "};\n";
    
    _genCommon << uniforms.str()
               << declarations.str()
               << accessors.str();
    
    // other shaders (renderpass, lighting, surface) first
    TF_FOR_ALL(it, _shaders) {
        HdStShaderCodeSharedPtr const &shader = *it;
        _genCS  << shader->GetSource(HdShaderTokens->computeShader);
    }

    // main
    _genCS << "void main() {\n";
    _genCS << "  int computeCoordinate = int(gl_GlobalInvocationID.x);\n";
    _genCS << "  compute(computeCoordinate);\n";
    _genCS << "}\n";
    
    // create GLSL program.
    HdStGLSLProgramSharedPtr glslProgram(
        new HdStGLSLProgram(HdTokens->computeShader));
    
    // compile shaders
    {
        _csSource = _genCommon.str() + _genCS.str();
        if (!glslProgram->CompileShader(GL_COMPUTE_SHADER, _csSource)) {
            const char *shaderSources[1];
            shaderSources[0] = _csSource.c_str();
            GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(shader, 1, shaderSources, NULL);
            glCompileShader(shader);

            std::string logString;
            HdStGLUtils::GetShaderCompileStatus(shader, &logString);
            TF_WARN("Failed to compile compute shader: %s",
                    logString.c_str());
            glDeleteShader(shader);
            return HdStGLSLProgramSharedPtr();
        }
    }
    
    return glslProgram;
}

static void _EmitDeclaration(std::stringstream &str,
                             TfToken const &name,
                             TfToken const &type,
                             HdBinding const &binding,
                             int arraySize)
{
    /*
      [vertex attribute]
         layout (location = <location>) in <type> <name>;
      [uniform]
         layout (location = <location>) uniform <type> <name>;
      [SSBO]
         layout (std430, binding = <location>) buffer buffer_<location> {
            <type> <name>[];
         };
      [Bindless Uniform]
         layout (location = <location>) uniform <type> *<name>;

     */
    HdBinding::Type bindingType = binding.GetType();

    if (!TF_VERIFY(!name.IsEmpty())) return;
    if (!TF_VERIFY(!type.IsEmpty(),
                      "Unknown dataType for %s",
                      name.GetText())) return;

    if (arraySize > 0) {
        if (!TF_VERIFY(bindingType == HdBinding::UNIFORM_ARRAY                ||
                          bindingType == HdBinding::DRAW_INDEX_INSTANCE_ARRAY ||
                          bindingType == HdBinding::UBO                       ||
                          bindingType == HdBinding::SSBO                      ||
                          bindingType == HdBinding::BINDLESS_SSBO_RANGE       ||
                          bindingType == HdBinding::BINDLESS_UNIFORM)) {
            // XXX: SSBO and BINDLESS_UNIFORM don't need arraySize, but for the
            // workaround of UBO allocation we're passing arraySize = 2
            // for all bindingType.
            return;
        }
    }

    // layout qualifier (if exists)
    str << LayoutQualifier(binding);

    switch (bindingType) {
    case HdBinding::VERTEX_ATTR:
    case HdBinding::DRAW_INDEX:
    case HdBinding::DRAW_INDEX_INSTANCE:
        str << "in " << _GetPackedType(type, false) << " " << name << ";\n";
        break;
    case HdBinding::DRAW_INDEX_INSTANCE_ARRAY:
        str << "in " << _GetPackedType(type, false) << " " << name
            << "[" << arraySize << "];\n";
        break;
    case HdBinding::UNIFORM:
        str << "uniform " << _GetPackedType(type, false) << " " << name << ";\n";
        break;
    case HdBinding::UNIFORM_ARRAY:
        str << "uniform " << _GetPackedType(type, false) << " " << name
            << "[" << arraySize << "];\n";
        break;
    case HdBinding::UBO:
        // note: ubo_ prefix is used in HdResourceBinder::IntrospectBindings.
        str << "uniform ubo_" << name <<  " {\n"
            << "  " << _GetPackedType(type, true)
            << " " << name;
        if (arraySize > 0) {
            str << "[" << arraySize << "];\n";
        } else {
            str << ";\n";
        }
        str << "};\n";
        break;
    case HdBinding::SSBO:
        str << "buffer buffer_" << binding.GetLocation() << " {\n"
            << "  " << _GetPackedType(type, true)
            << " " << name << "[];\n"
            << "};\n";
        break;
    case HdBinding::BINDLESS_SSBO_RANGE:
        str << "uniform " << _GetPackedType(type, true)
            << " *" << name << ";\n";
        break;
    case HdBinding::TBO:
        str << "uniform " << _GetSamplerBufferType(type)
            << " " << name << ";\n";
        break;
    case HdBinding::BINDLESS_UNIFORM:
        str << "uniform " << _GetPackedType(type, true)
            << " *" << name << ";\n";
        break;
    case HdBinding::TEXTURE_2D:
    case HdBinding::BINDLESS_TEXTURE_2D:
        str << "uniform sampler2D " << name << ";\n";
        break;
    case HdBinding::TEXTURE_3D:
    case HdBinding::BINDLESS_TEXTURE_3D:
        str << "uniform sampler3D " << name << ";\n";
        break;
    case HdBinding::TEXTURE_UDIM_ARRAY:
    case HdBinding::BINDLESS_TEXTURE_UDIM_ARRAY:
        str << "uniform sampler2DArray " << name.GetText() << "_Images;\n";
        break;
    case HdBinding::TEXTURE_UDIM_LAYOUT:
    case HdBinding::BINDLESS_TEXTURE_UDIM_LAYOUT:
        str << "uniform sampler1D " << name.GetText() << "_Layout;\n";
        break;
    case HdBinding::TEXTURE_PTEX_TEXEL:
        str << "uniform sampler2DArray " << name << "_Data;\n";
        break;
    case HdBinding::TEXTURE_PTEX_LAYOUT:
        str << "uniform isamplerBuffer " << name << "_Packing;\n";
        break;
    default:
        TF_CODING_ERROR("Unknown binding type %d, for %s\n",
                        binding.GetType(), name.GetText());
        break;
    }
}

static void _EmitDeclaration(
    std::stringstream &str,
    HdSt_ResourceBinder::MetaData::BindingDeclaration const &bindingDeclaration,
    int arraySize=0)
{
    _EmitDeclaration(str,
         bindingDeclaration.name,
         bindingDeclaration.dataType,
         bindingDeclaration.binding,
         arraySize);
}

static void _EmitStructAccessor(std::stringstream &str,
                                TfToken const &structName,
                                TfToken const &name,
                                TfToken const &type,
                                int arraySize,
                                const char *index = NULL)
{
    // index != NULL  if the struct is an array
    // arraySize > 1  if the struct entry is an array.
    if (index) {
        if (arraySize > 1) {
            str << _GetUnpackedType(type, false) << " HdGet_" << name
                << "(int arrayIndex, int localIndex) {\n"
                // storing to a local variable to avoid the nvidia-driver
                // bug #1561110 (fixed in 346.59)
                << "  int index = " << index << ";\n"
                << "  return "
                << _GetPackedTypeAccessor(type, false) << "("
                << structName << "[index]." << name << "[arrayIndex]);\n}\n";
        } else {
            str << _GetUnpackedType(type, false) << " HdGet_" << name
                << "(int localIndex) {\n"
                << "  int index = " << index << ";\n"
                << "  return "
                << _GetPackedTypeAccessor(type, false) << "("
                << structName << "[index]." << name << ");\n}\n";
        }
    } else {
        if (arraySize > 1) {
            str << _GetUnpackedType(type, false) << " HdGet_" << name
                << "(int arrayIndex, int localIndex) { return "
                << _GetPackedTypeAccessor(type, false) << "("
                << structName << "." << name << "[arrayIndex]);}\n";
        } else {
            str << _GetUnpackedType(type, false) << " HdGet_" << name
                << "(int localIndex) { return "
                << _GetPackedTypeAccessor(type, false) << "("
                << structName << "." << name << ");}\n";
        }
    }
    // GLSL spec doesn't allow default parameter. use function overload instead.
    // default to localIndex=0
    if (arraySize > 1) {
        str << _GetUnpackedType(type, false) << " HdGet_" << name
            << "(int arrayIndex)"
            << " { return HdGet_" << name << "(arrayIndex, 0); }\n";
    } else {
        str << _GetUnpackedType(type, false) << " HdGet_" << name << "()"
            << " { return HdGet_" << name << "(0); }\n";
    }
}

static std::string _GetSwizzleString(TfToken const& type)
{
    std::string swizzle = "";
    if (type == _tokens->vec4 || type == _tokens->ivec4) {
        // nothing
    } else if (type == _tokens->vec3 || type == _tokens->ivec3) {
        swizzle = ".xyz";
    } else if (type == _tokens->vec2 || type == _tokens->ivec2) {
        swizzle = ".xy";
    } else if (type == _tokens->_float || type == _tokens->_int) {
        swizzle = ".x";
    } else if (type == _tokens->packed_2_10_10_10) {
        swizzle = ".x";
    }

    return swizzle;
}

static int _GetNumComponents(TfToken const& type)
{
    int numComponents = 1;
    if (type == _tokens->vec2 || type == _tokens->ivec2) {
        numComponents = 2;
    } else if (type == _tokens->vec3 || type == _tokens->ivec3) {
        numComponents = 3;
    } else if (type == _tokens->vec4 || type == _tokens->ivec4) {
        numComponents = 4;
    } else if (type == _tokens->mat3 || type == _tokens->dmat3) {
        numComponents = 9;
    } else if (type == _tokens->mat4 || type == _tokens->dmat4) {
        numComponents = 16;
    }

    return numComponents;
}

static void _EmitComputeAccessor(
                    std::stringstream &str,
                    TfToken const &name,
                    TfToken const &type,
                    HdBinding const &binding,
                    const char *index)
{
    if (index) {
        str << _GetUnpackedType(type, false)
            << " HdGet_" << name << "(int localIndex) {\n";
        if (binding.GetType() == HdBinding::TBO) {
            str << "  int index = " << index << ";\n";
            str << "  return "
                << _GetPackedTypeAccessor(type, false)
                << "(texelFetch(" << name << ", index)"
                << _GetSwizzleString(type) << ");\n}\n";
        } else if (binding.GetType() == HdBinding::SSBO) {
            str << "  int index = " << index << ";\n";
            str << "  return " << _GetPackedTypeAccessor(type, false) << "("
                << _GetPackedType(type, false) << "(";
            int numComponents = _GetNumComponents(type);
            for (int c = 0; c < numComponents; ++c) {
                if (c > 0) {
                    str << ",\n              ";
                }
                str << name << "[index + " << c << "]";
            }
            str << "));\n}\n";
        } else if (binding.GetType() == HdBinding::BINDLESS_SSBO_RANGE) {
            str << "  return " << _GetPackedTypeAccessor(type, true) << "("
                << name << "[localIndex]);\n}\n";
        } else {
            str << "  return " << _GetPackedTypeAccessor(type, true) << "("
                << name << "[localIndex]);\n}\n";
        }
    } else {
        // non-indexed, only makes sense for uniform or vertex.
        if (binding.GetType() == HdBinding::UNIFORM || 
            binding.GetType() == HdBinding::VERTEX_ATTR) {
            str << _GetUnpackedType(type, false)
                << " HdGet_" << name << "(int localIndex) { return ";
            str << _GetPackedTypeAccessor(type, true) << "(" << name << ");}\n";
        }
    }
    // GLSL spec doesn't allow default parameter. use function overload instead.
    // default to locaIndex=0
    str << _GetUnpackedType(type, false) << " HdGet_" << name << "()"
        << " { return HdGet_" << name << "(0); }\n";
    
}

static void _EmitComputeMutator(
                    std::stringstream &str,
                    TfToken const &name,
                    TfToken const &type,
                    HdBinding const &binding,
                    const char *index)
{
    if (index) {
        str << "void"
            << " HdSet_" << name << "(int localIndex, "
            << _GetUnpackedType(type, false) << " value) {\n";
        if (binding.GetType() == HdBinding::SSBO) {
            str << "  int index = " << index << ";\n";
            str << "  " << _GetPackedType(type, false) << " packedValue = "
                << _GetPackedTypeMutator(type, false) << "(value);\n";
            int numComponents = _GetNumComponents(_GetPackedType(type, false));
            if (numComponents == 1) {
                str << "  "
                    << name << "[index] = packedValue;\n";
            } else {
                for (int c = 0; c < numComponents; ++c) {
                    str << "  "
                        << name << "[index + " << c << "] = "
                        << "packedValue[" << c << "];\n";
                }
            }
        } else if (binding.GetType() == HdBinding::BINDLESS_SSBO_RANGE) {
            str << name << "[localIndex] = "
                << _GetPackedTypeMutator(type, true) << "(value);\n";
        } else {
            TF_WARN("mutating non-SSBO not supported");
        }
        str << "}\n";
    } else {
        TF_WARN("mutating non-indexed data not supported");
    }
    // XXX Don't output a default mutator as we don't want accidental overwrites
    // of compute read-write data.
    // GLSL spec doesn't allow default parameter. use function overload instead.
    // default to locaIndex=0
    //str << "void HdSet_" << name << "(" << type << " value)"
    //    << " { HdSet_" << name << "(0, value); }\n";
    
}

static void _EmitAccessor(std::stringstream &str,
                          TfToken const &name,
                          TfToken const &type,
                          HdBinding const &binding,
                          const char *index)
{
    if (index) {
        str << _GetUnpackedType(type, false)
            << " HdGet_" << name << "(int localIndex) {\n"
            << "  int index = " << index << ";\n";
        if (binding.GetType() == HdBinding::TBO) {
            str << "  return "
                << _GetPackedTypeAccessor(type, false)
                << "(texelFetch(" << name << ", index)"
                << _GetSwizzleString(type) << ");\n}\n";
        } else {
            str << "  return " << _GetPackedTypeAccessor(type, true) << "("
                << name << "[index]);\n}\n";
        }
    } else {
        // non-indexed, only makes sense for uniform or vertex.
        if (binding.GetType() == HdBinding::UNIFORM || 
            binding.GetType() == HdBinding::VERTEX_ATTR) {
            str << _GetUnpackedType(type, false)
                << " HdGet_" << name << "(int localIndex) { return ";
            str << _GetPackedTypeAccessor(type, true) << "(" << name << ");}\n";
        }
    }
    // GLSL spec doesn't allow default parameter. use function overload instead.
    // default to locaIndex=0
    str << _GetUnpackedType(type, false) << " HdGet_" << name << "()"
        << " { return HdGet_" << name << "(0); }\n";
    
}


// Accessing face varying primvar data of a vertex in the GS requires special
// case handling for refinement while providing a branchless solution.
// When dealing with vertices on a refined face, we use the patch coord to get
// its parametrization on the sanitized (coarse) "ptex" face, and interpolate
// based on the face primitive type (bilinear for quad faces, barycentric for
// tri faces)
static void _EmitFVarGSAccessor(
                std::stringstream &str,
                TfToken const &name,
                TfToken const &type,
                HdBinding const &binding,
                HdSt_GeometricShader::PrimitiveType const& primType)
{
    // emit an internal getter for accessing the coarse fvar data (corresponding
    // to the refined face, in the case of refinement)
    str << _GetUnpackedType(type, false)
        << " HdGet_" << name << "_Coarse(int localIndex) {\n"
        << "  int fvarIndex = GetFVarIndex(localIndex);\n";

        if (binding.GetType() == HdBinding::TBO) {
            str << "  return "
                << _GetPackedTypeAccessor(type, false)
                << "(texelFetch(" << name << ", fvarIndex)"
                << _GetSwizzleString(type) << ");\n}\n";
        } else {
            str << "  return " << _GetPackedTypeAccessor(type, true) << "("
                << name << "[fvarIndex]);\n}\n";
        }

    // emit the (public) accessor for the fvar data, accounting for refinement
    // interpolation
    str << "vec4 GetPatchCoord(int index);\n"; // forward decl
    str << _GetUnpackedType(type, false)
        << " HdGet_" << name << "(int localIndex) {\n"
        << "  vec2 localST = GetPatchCoord(localIndex).xy;\n";

    switch(primType)
    {
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_QUADS:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_QUADS:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BSPLINE:
        {
            // linear interpolation within a quad.
            str << "  return mix("
                << "mix(" << "HdGet_" << name << "_Coarse(0),"
                <<           "HdGet_" << name << "_Coarse(1), localST.x),"
                << "mix(" << "HdGet_" << name << "_Coarse(3),"
                <<           "HdGet_" << name << "_Coarse(2), localST.x), localST.y);\n}\n";
            break;
        }

        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_TRIANGLES:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_TRIANGLES:
        case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BOXSPLINETRIANGLE:
        {
            // barycentric interpolation within a triangle.
            str << "  return ("
                << "HdGet_" << name << "_Coarse(0) * (1-localST.x-localST.y)"
                << " + HdGet_" << name << "_Coarse(1) * localST.x"
                << " + HdGet_" << name << "_Coarse(2) * localST.y);\n}\n";
            break;
        }

        case HdSt_GeometricShader::PrimitiveType::PRIM_POINTS:
        {
            str << "  return HdGet_" << name << "_Coarse(localIndex);\n}\n";
            break;
        }

        default:
        {
            // emit a default version for compilation sake
            str << "  return HdGet_" << name << "_Coarse(localIndex);\n}\n";

            TF_CODING_ERROR("Face varing bindings for unexpected for" 
                            " HdSt_GeometricShader::PrimitiveType %d", 
                            (int)primType);
        }
    }

    // XXX: We shouldn't emit the default (argument free) accessor version,
    // since that doesn't make sense within a GS. Once we fix the XXX in
    // _GenerateShaderParameters, we should remove this.
    str << _GetUnpackedType(type, false) << " HdGet_" << name << "()"
        << " { return HdGet_" << name << "(0); }\n";
}

void
HdSt_CodeGen::_GenerateDrawingCoord()
{
    TF_VERIFY(_metaData.drawingCoord0Binding.binding.IsValid());
    TF_VERIFY(_metaData.drawingCoord1Binding.binding.IsValid());
    TF_VERIFY(_metaData.drawingCoord2Binding.binding.IsValid());

    /*
       hd_drawingCoord is a struct of integer offsets to locate the primvars
       in buffer arrays at the current rendering location.

       struct hd_drawingCoord {
           int modelCoord;          // (reserved) model parameters
           int constantCoord;       // constant primvars (per object)
           int vertexCoord;         // vertex primvars   (per vertex)
           int elementCoord;        // element primvars  (per face/curve)
           int primitiveCoord;      // primitive ids     (per tri/quad/line)
           int fvarCoord;           // fvar primvars     (per face-vertex)
           int shaderCoord;         // shader parameters (per shader/object)
           int instanceIndex[];     // (see below)
           int instanceCoords[];    // (see below)
       };

          instanceIndex[0]  : global instance ID (used for ID rendering)
                       [1]  : instance index for level = 0
                       [2]  : instance index for level = 1
                       ...
          instanceCoords[0] : instanceDC for level = 0
          instanceCoords[1] : instanceDC for level = 1
                       ...

       We also have a drawingcoord for vertex primvars. Currently it's not
       being passed into shader since the vertex shader takes pre-offsetted
       vertex arrays and no needs to apply offset in shader (except gregory
       patch drawing etc. In that case gl_BaseVertexARB can be used under
       GL_ARB_shader_draw_parameters extention)

       gl_InstanceID is available only in vertex shader, so codegen
       takes care of applying an offset for each instance for the later
       stage. On the other hand, gl_PrimitiveID is available in all stages
       except vertex shader, and since tess/geometry shaders may or may not
       exist, we don't apply an offset of primitiveID during interstage
       plumbing to avoid overlap. Instead, GetDrawingCoord() applies
       primitiveID if necessary.

       XXX:
       Ideally we should use an interface block like:

         in DrawingCoord {
             flat hd_drawingCoord drawingCoord;
         } inDrawingCoord;
         out DrawingCoord {
             flat hd_drawingCoord drawingCoord;
         } outDrawingCoord;

      then the fragment shader can take the same input regardless the
      existence of tess/geometry shaders. However it seems the current
      driver (331.79) doesn't handle multiple interface blocks
      appropriately, it fails matching and ends up undefined results at
      consuming shader.

      > OpenGL 4.4 Core profile
      > 7.4.1 Shader Interface Matching
      >
      > When multiple shader stages are active, the outputs of one stage form
      > an interface with the inputs of the next stage. At each such
      > interface, shader inputs are matched up against outputs from the
      > previous stage:
      >
      > An output block is considered to match an input block in the
      > subsequent shader if the two blocks have the same block name, and
      > the members of the block match exactly in name, type, qualification,
      > and declaration order.
      >
      > An output variable is considered to match an input variable in the
      > subsequent shader if:
      >  - the two variables match in name, type, and qualification; or
      >  - the two variables are declared with the same location and
      >     component layout qualifiers and match in type and qualification.

      We use non-block variable for drawingCoord as a workaround of this
      problem for now. There is a caveat we can't use the same name for input
      and output, the subsequent shader has to be aware which stage writes
      the drawingCoord.

      for example:
        drawingCoord--(VS)--vsDrawingCoord--(GS)--gsDrawingCoord--(FS)
        drawingCoord--(VS)------------------------vsDrawingCoord--(FS)

      Fortunately the compiler is smart enough to optimize out unused
      attributes. If the VS writes the same value into two attributes:

        drawingCoord--(VS)--vsDrawingCoord--(GS)--gsDrawingCoord--(FS)
                      (VS)--gsDrawingCoord--------gsDrawingCoord--(FS)

      The fragment shader can always take gsDrawingCoord. The following code
      does such a plumbing work.

     */

    // common
    //
    // note: instanceCoords should be [HD_INSTANCER_NUM_LEVELS], but since
    //       GLSL doesn't allow [0] declaration, we use +1 value (WIDTH)
    //       for the sake of simplicity.
    _genCommon << "struct hd_drawingCoord {                       \n"
               << "  int modelCoord;                              \n"
               << "  int constantCoord;                           \n"
               << "  int vertexCoord;                             \n"
               << "  int elementCoord;                            \n"
               << "  int primitiveCoord;                          \n"
               << "  int fvarCoord;                               \n"
               << "  int shaderCoord;                             \n"
               << "  int topologyVisibilityCoord;                 \n"
               << "  int instanceIndex[HD_INSTANCE_INDEX_WIDTH];  \n"
               << "  int instanceCoords[HD_INSTANCE_INDEX_WIDTH]; \n"
               << "};\n";

    _genCommon << "hd_drawingCoord GetDrawingCoord();\n"; // forward declaration

    // vertex shader

    // [immediate]
    //   layout (location=x) uniform ivec4 drawingCoord0;
    //   layout (location=y) uniform ivec4 drawingCoord1;
    //   layout (location=z) uniform int   drawingCoordI[N];
    // [indirect]
    //   layout (location=x) in ivec4 drawingCoord0
    //   layout (location=y) in ivec4 drawingCoord1
    //   layout (location=z) in int   drawingCoordI[N]
    _EmitDeclaration(_genVS, _metaData.drawingCoord0Binding);
    _EmitDeclaration(_genVS, _metaData.drawingCoord1Binding);
    _EmitDeclaration(_genVS, _metaData.drawingCoord2Binding);
    if (_metaData.drawingCoordIBinding.binding.IsValid()) {
        _EmitDeclaration(_genVS, _metaData.drawingCoordIBinding,
                         /*arraySize=*/std::max(1, _metaData.instancerNumLevels));
    }

    // instance index indirection
    _genCommon << "struct hd_instanceIndex { int indices[HD_INSTANCE_INDEX_WIDTH]; };\n";

    if (_metaData.instanceIndexArrayBinding.binding.IsValid()) {
        // << layout (location=x) uniform (int|ivec[234]) *instanceIndices;
        _EmitDeclaration(_genCommon, _metaData.instanceIndexArrayBinding);

        // << layout (location=x) uniform (int|ivec[234]) *culledInstanceIndices;
        _EmitDeclaration(_genCommon,  _metaData.culledInstanceIndexArrayBinding);

        /// if \p cullingPass is true, CodeGen generates GetInstanceIndex()
        /// such that it refers instanceIndices buffer (before culling).
        /// Otherwise, GetInstanceIndex() looks up culledInstanceIndices.

        _genVS << "int GetInstanceIndexCoord() {\n"
               << "  return drawingCoord1.y + gl_InstanceID * HD_INSTANCE_INDEX_WIDTH; \n"
               << "}\n";

        if (_geometricShader->IsCullingPass()) {
            // for frustum culling:  use instanceIndices.
            _genVS << "hd_instanceIndex GetInstanceIndex() {\n"
                   << "  int offset = GetInstanceIndexCoord();\n"
                   << "  hd_instanceIndex r;\n"
                   << "  for (int i = 0; i < HD_INSTANCE_INDEX_WIDTH; ++i)\n"
                   << "    r.indices[i] = instanceIndices[offset+i];\n"
                   << "  return r;\n"
                   << "}\n";
            _genVS << "void SetCulledInstanceIndex(uint instanceID) {\n"
                   << "  for (int i = 0; i < HD_INSTANCE_INDEX_WIDTH; ++i)\n"
                   << "    culledInstanceIndices[drawingCoord1.y + instanceID*HD_INSTANCE_INDEX_WIDTH+i]"
                   << "        = instanceIndices[drawingCoord1.y + gl_InstanceID*HD_INSTANCE_INDEX_WIDTH+i];\n"
                   << "}\n";
        } else {
            // for drawing:  use culledInstanceIndices.
            _EmitAccessor(_genVS, _metaData.culledInstanceIndexArrayBinding.name,
                          _metaData.culledInstanceIndexArrayBinding.dataType,
                          _metaData.culledInstanceIndexArrayBinding.binding,
                          "GetInstanceIndexCoord()+localIndex");
            _genVS << "hd_instanceIndex GetInstanceIndex() {\n"
                   << "  int offset = GetInstanceIndexCoord();\n"
                   << "  hd_instanceIndex r;\n"
                   << "  for (int i = 0; i < HD_INSTANCE_INDEX_WIDTH; ++i)\n"
                   << "    r.indices[i] = HdGet_culledInstanceIndices(/*localIndex=*/i);\n"
                   << "  return r;\n"
                   << "}\n";
        }
    } else {
        _genVS << "hd_instanceIndex GetInstanceIndex() {"
               << "  hd_instanceIndex r; r.indices[0] = 0; return r; }\n";
        if (_geometricShader->IsCullingPass()) {
            _genVS << "void SetCulledInstanceIndex(uint instance) "
                      "{ /*no-op*/ }\n";
        }
    }

    _genVS << "flat out hd_drawingCoord vsDrawingCoord;\n"
           // XXX: see the comment above why we need both vs and gs outputs.
           << "flat out hd_drawingCoord gsDrawingCoord;\n";

    _genVS << "hd_drawingCoord GetDrawingCoord() { hd_drawingCoord dc; \n"
           << "  dc.modelCoord              = drawingCoord0.x; \n"
           << "  dc.constantCoord           = drawingCoord0.y; \n"
           << "  dc.elementCoord            = drawingCoord0.z; \n"
           << "  dc.primitiveCoord          = drawingCoord0.w; \n"
           << "  dc.fvarCoord               = drawingCoord1.x; \n"
           << "  dc.shaderCoord             = drawingCoord1.z; \n"
           << "  dc.vertexCoord             = drawingCoord1.w; \n"
           << "  dc.topologyVisibilityCoord = drawingCoord2; \n"
           << "  dc.instanceIndex           = GetInstanceIndex().indices;\n";

    if (_metaData.drawingCoordIBinding.binding.IsValid()) {
        _genVS << "  for (int i = 0; i < HD_INSTANCER_NUM_LEVELS; ++i) {\n"
               << "    dc.instanceCoords[i] = drawingCoordI[i] \n"
               << "      + dc.instanceIndex[i+1]; \n"
               << "  }\n";
    }

    _genVS << "  return dc;\n"
           << "}\n";

    // note: GL spec says tessellation input array size must be equal to
    //       gl_MaxPatchVertices, which is used for intrinsic declaration
    //       of built-in variables:
    //       in gl_PerVertex {} gl_in[gl_MaxPatchVertices];

    // tess control shader
    _genTCS << "flat in hd_drawingCoord vsDrawingCoord[gl_MaxPatchVertices];\n"
            << "flat out hd_drawingCoord tcsDrawingCoord[HD_NUM_PATCH_VERTS];\n"
            << "hd_drawingCoord GetDrawingCoord() { \n"
            << "  hd_drawingCoord dc = vsDrawingCoord[gl_InvocationID];\n"
            << "  dc.primitiveCoord += gl_PrimitiveID;\n"
            << "  return dc;\n"
            << "}\n";
    // tess eval shader
    _genTES << "flat in hd_drawingCoord tcsDrawingCoord[gl_MaxPatchVertices];\n"
            << "flat out hd_drawingCoord vsDrawingCoord;\n"
            << "flat out hd_drawingCoord gsDrawingCoord;\n"
            << "hd_drawingCoord GetDrawingCoord() { \n"
            << "  hd_drawingCoord dc = tcsDrawingCoord[0]; \n"
            << "  dc.primitiveCoord += gl_PrimitiveID; \n"
            << "  return dc;\n"
            << "}\n";

    // geometry shader ( VSdc + gl_PrimitiveIDIn )
    _genGS << "flat in hd_drawingCoord vsDrawingCoord[HD_NUM_PRIMITIVE_VERTS];\n"
           << "flat out hd_drawingCoord gsDrawingCoord;\n"
           << "hd_drawingCoord GetDrawingCoord() { \n"
           << "  hd_drawingCoord dc = vsDrawingCoord[0]; \n"
           << "  dc.primitiveCoord += gl_PrimitiveIDIn; \n"
           << "  return dc; \n"
           << "}\n";

    // fragment shader ( VSdc + gl_PrimitiveID )
    // note that gsDrawingCoord isn't offsetted by gl_PrimitiveIDIn
    _genFS << "flat in hd_drawingCoord gsDrawingCoord;\n"
           << "hd_drawingCoord GetDrawingCoord() { \n"
           << "  hd_drawingCoord dc = gsDrawingCoord; \n"
           << "  dc.primitiveCoord += gl_PrimitiveID; \n"
           << "  return dc; \n"
           << "}\n";

    // drawing coord plumbing.
    // Note that copying from [0] for multiple input source since the
    // drawingCoord is flat (no interpolation required).
    _procVS  << "  vsDrawingCoord = GetDrawingCoord();\n"
             << "  gsDrawingCoord = GetDrawingCoord();\n";
    _procTCS << "  tcsDrawingCoord[gl_InvocationID] = "
             << "  vsDrawingCoord[gl_InvocationID];\n";
    _procTES << "  vsDrawingCoord = tcsDrawingCoord[0];\n"
             << "  gsDrawingCoord = tcsDrawingCoord[0];\n";
    _procGS  << "  gsDrawingCoord = vsDrawingCoord[0];\n";

}
void
HdSt_CodeGen::_GenerateConstantPrimvar()
{
    /*
      // --------- constant data declaration ----------
      struct ConstantData0 {
          mat4 transform;
          mat4 transformInverse;
          mat4 instancerTransform[2];
          vec3 displayColor;
          vec4 primID;
      };
      // bindless
      layout (location=0) uniform ConstantData0 *constantData0;
      // not bindless
      layout (std430, binding=0) buffer {
          constantData0 constantData0[];
      };

      // --------- constant data accessors ----------
      mat4 HdGet_transform(int localIndex) {
          return constantData0[GetConstantCoord()].transform;
      }
      vec3 HdGet_displayColor(int localIndex) {
          return constantData0[GetConstantCoord()].displayColor;
      }

    */

    std::stringstream declarations;
    std::stringstream accessors;
    TF_FOR_ALL (it, _metaData.constantData) {
        // note: _constantData has been sorted by offset in HdSt_ResourceBinder.
        // XXX: not robust enough, should consider padding and layouting rules
        // to match with the logic in HdInterleavedMemoryManager if we
        // want to use a layouting policy other than default padding.

        HdBinding binding = it->first;
        TfToken typeName(TfStringPrintf("ConstantData%d", binding.GetValue()));
        TfToken varName = it->second.blockName;

        declarations << "struct " << typeName << " {\n";

        TF_FOR_ALL (dbIt, it->second.entries) {
            if (!TF_VERIFY(!dbIt->dataType.IsEmpty(),
                              "Unknown dataType for %s",
                              dbIt->name.GetText())) {
                continue;
            }

            declarations << "  " << _GetPackedType(dbIt->dataType, false)
                         << " " << dbIt->name;
            if (dbIt->arraySize > 1) {
                declarations << "[" << dbIt->arraySize << "]";
            }

            declarations << ";\n";

            _EmitStructAccessor(accessors, varName, dbIt->name, dbIt->dataType,
                                dbIt->arraySize,
                                "GetDrawingCoord().constantCoord");
        }
        declarations << "};\n";

        // XXX: passing arraySize=2 to cheat driver to not tell actual size.
        //      we should compute the actual size or maximum size if possible.
        _EmitDeclaration(declarations, varName, typeName, binding, /*arraySize=*/1);
    }
    _genCommon << declarations.str()
               << accessors.str();
}

void
HdSt_CodeGen::_GenerateInstancePrimvar()
{
    /*
      // --------- instance data declaration ----------
      // bindless
      layout (location=X) uniform vec4 *data;
      // not bindless
      layout (std430, binding=X) buffer buffer_X {
          vec4 data[];
      };

      // --------- instance data accessors ----------
      vec3 HdGet_translate(int localIndex=0) {
          return instanceData0[GetInstanceCoord()].translate;
      }
    */

    std::stringstream declarations;
    std::stringstream accessors;

    struct LevelEntries {
        TfToken dataType;
        std::vector<int> levels;
    };
    std::map<TfToken, LevelEntries> nameAndLevels;

    TF_FOR_ALL (it, _metaData.instanceData) {
        HdBinding binding = it->first;
        TfToken const &dataType = it->second.dataType;
        int level = it->second.level;

        nameAndLevels[it->second.name].dataType = dataType;
        nameAndLevels[it->second.name].levels.push_back(level);

        std::stringstream n;
        n << it->second.name << "_" << level;
        TfToken name(n.str());
        n.str("");
        n << "GetDrawingCoord().instanceCoords[" << level << "]";

        // << layout (location=x) uniform float *translate_0;
        _EmitDeclaration(declarations, name, dataType, binding);
        _EmitAccessor(accessors, name, dataType, binding, n.str().c_str());

    }

    /*
      accessor taking level as a parameter.
      note that instance primvar may or may not be defined for each level.
      we expect level is an unrollable constant to optimize out branching.

      vec3 HdGetInstance_translate(int level, vec3 defaultValue) {
          if (level == 0) return HdGet_translate_0();
          // level==1 is not defined. use default
          if (level == 2) return HdGet_translate_2();
          if (level == 3) return HdGet_translate_3();
          return defaultValue;
      }
    */
    TF_FOR_ALL (it, nameAndLevels) {
        accessors << _GetUnpackedType(it->second.dataType, false)
                  << " HdGetInstance_" << it->first << "(int level, "
                  << _GetUnpackedType(it->second.dataType, false)
                  << " defaultValue) {\n";
        TF_FOR_ALL (levelIt, it->second.levels) {
            accessors << "  if (level == " << *levelIt << ") "
                      << "return HdGet_" << it->first << "_" << *levelIt << "();\n";
        }

        accessors << "  return defaultValue;\n"
                  << "}\n";
    }
    /*
      common accessor, if the primvar is defined on the instancer but not
      the rprim.

      #if !defined(HD_HAS_translate)
      #define HD_HAS_translate 1
      vec3 HdGet_translate(int localIndex) {
          // 0 is the lowest level for which this is defined
          return HdGet_translate_0();
      }
      vec3 HdGet_translate() {
          return HdGet_translate(0);
      }
      #endif
    */
    TF_FOR_ALL (it, nameAndLevels) {
        accessors << "#if !defined(HD_HAS_" << it->first << ")\n"
                  << "#define HD_HAS_" << it->first << " 1\n"
                  << _GetUnpackedType(it->second.dataType, false)
                  << " HdGet_" << it->first << "(int localIndex) {\n"
                  << "  return HdGet_" << it->first << "_"
                                       << it->second.levels.front() << "();\n"
                  << "}\n"
                  << _GetUnpackedType(it->second.dataType, false)
                  << " HdGet_" << it->first << "() { return HdGet_"
                  << it->first << "(0); }\n"
                  << "#endif\n";
    }
        

    _genCommon << declarations.str()
               << accessors.str();
}

void
HdSt_CodeGen::_GenerateElementPrimvar()
{
    /*
    Accessing uniform primvar data:
    ===============================
    Uniform primvar data is authored at the subprimitive (also called element or
    face below) granularity.
    To access uniform primvar data (say color), there are two indirections in
    the lookup because of aggregation in the buffer layout.
          ----------------------------------------------------
    color | prim0 colors | prim1 colors | .... | primN colors|
          ----------------------------------------------------
    For each prim, GetDrawingCoord().elementCoord holds the start index into
    this buffer.

    For an unrefined prim, the subprimitive ID s simply the gl_PrimitiveID.
    For a refined prim, gl_PrimitiveID corresponds to the refined element ID.

    To map a refined face to its coarse face, Storm builds a "primitive param"
    buffer (more details in the section below). This buffer is also aggregated,
    and for each subprimitive, GetDrawingCoord().primitiveCoord gives us the
    index into this buffer (meaning it has already added the gl_PrimitiveID)

    To have a single codepath for both cases, we build the primitive param
    buffer for unrefined prims as well, and effectively index the uniform
    primvar using:
    drawCoord.elementCoord + primitiveParam[ drawCoord.primitiveCoord ]

    The code generated looks something like:

      // --------- primitive param declaration ----------
      struct PrimitiveData { int elementID; }
      layout (std430, binding=?) buffer PrimitiveBuffer {
          PrimitiveData primitiveData[];
      };

      // --------- indirection accessors ---------
      // Gives us the "coarse" element ID
      int GetElementID() {
          return primitiveData[GetPrimitiveCoord()].elementID;
      }
      
      // Adds the offset to the start of the uniform primvar data for the prim
      int GetAggregatedElementID() {
          return GetElementID() + GetDrawingCoord().elementCoord;\n"
      }

      // --------- uniform primvar declaration ---------
      struct ElementData0 {
          vec3 displayColor;
      };
      layout (std430, binding=?) buffer buffer0 {
          ElementData0 elementData0[];
      };

      // ---------uniform primvar data accessor ---------
      vec3 HdGet_displayColor(int localIndex) {
          return elementData0[GetAggregatedElementID()].displayColor;
      }

    */

    // Primitive Param buffer layout:
    // ==============================
    // Depending on the prim, one of following is used:
    // 
    // 1. basis curves
    //     1 int  : curve index 
    //     
    //     This lets us translate a basis curve segment to its curve id.
    //     A basis curve is made up for 'n' curves, each of which have a varying
    //     number of segments.
    //     (see hdSt/basisCurvesComputations.cpp)
    //     
    // 2. mesh specific
    // a. tris
    //     1 int  : coarse face index + edge flag
    //     (see hd/meshUtil.h,cpp)
    //     
    // b. quads coarse
    //     2 ints : coarse face index + edge flag
    //              ptex index
    //     (see hd/meshUtil.h,cpp)
    //
    // c. tris & quads uniformly refined
    //     3 ints : coarse face index + edge flag
    //              Far::PatchParam::field0 (includes ptex index)
    //              Far::PatchParam::field1
    //     (see hdSt/subdivision3.cpp)
    //
    // d. patch adaptively refined
    //     4 ints : coarse face index + edge flag
    //              Far::PatchParam::field0 (includes ptex index)
    //              Far::PatchParam::field1
    //              sharpness (float)
    //     (see hdSt/subdivision3.cpp)
    // -----------------------------------------------------------------------
    // note: decoding logic of primitiveParam has to match with
    // HdMeshTopology::DecodeFaceIndexFromPrimitiveParam()
    //
    // PatchParam is defined as ivec3 (see opensubdiv/far/patchParam.h)
    //  Field0     | Bits | Content
    //  -----------|:----:|---------------------------------------------------
    //  faceId     | 28   | the faceId of the patch (Storm uses ptexIndex)
    //  transition | 4    | transition edge mask encoding
    //
    //  Field1     | Bits | Content
    //  -----------|:----:|---------------------------------------------------
    //  level      | 4    | the subdivision level of the patch
    //  nonquad    | 1    | whether the patch is the child of a non-quad face
    //  unused     | 3    | unused
    //  boundary   | 4    | boundary edge mask encoding
    //  v          | 10   | log2 value of u parameter at first patch corner
    //  u          | 10   | log2 value of v parameter at first patch corner
    //
    //  Field2     (float)  sharpness
    //
    // whereas adaptive patches have PatchParams computed by OpenSubdiv,
    // we need to construct PatchParams for coarse tris and quads.
    // Currently it's enough to fill just faceId for coarse quads for
    // ptex shading.

    std::stringstream declarations;
    std::stringstream accessors;

    if (_metaData.primitiveParamBinding.binding.IsValid()) {

        HdBinding binding = _metaData.primitiveParamBinding.binding;
        _EmitDeclaration(declarations, _metaData.primitiveParamBinding);
        _EmitAccessor(accessors, _metaData.primitiveParamBinding.name,
                        _metaData.primitiveParamBinding.dataType, binding,
                        "GetDrawingCoord().primitiveCoord");

        if (_geometricShader->IsPrimTypePoints()) {
            // do nothing. 
            // e.g. if a prim's geomstyle is points and it has a valid
            // primitiveParamBinding, we don't generate any of the 
            // accessor methods.
            ;            
        }
        else if (_geometricShader->IsPrimTypeBasisCurves()) {
            // straight-forward indexing to get the segment's curve id
            accessors
                << "int GetElementID() {\n"
                << "  return (hd_int_get(HdGet_primitiveParam()));\n"
                << "}\n";
            accessors
                << "int GetAggregatedElementID() {\n"
                << "  return GetElementID()\n"
                << "  + GetDrawingCoord().elementCoord;\n"
                << "}\n";
        }
        else if (_geometricShader->IsPrimTypeMesh()) {
            // GetPatchParam, GetEdgeFlag
            switch (_geometricShader->GetPrimitiveType()) {
                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_QUADS:
                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_REFINED_TRIANGLES:
                {
                    // refined quads ("uniform" subdiv) or
                    // refined tris (loop subdiv)
                    accessors
                        << "ivec3 GetPatchParam() {\n"
                        << "  return ivec3(HdGet_primitiveParam().y, \n"
                        << "               HdGet_primitiveParam().z, 0);\n"
                        << "}\n";
                    // XXX: Is the edge flag returned actually used?
                    accessors
                        << "int GetEdgeFlag(int localIndex) {\n"
                        << "  return (HdGet_primitiveParam().x & 3);\n"
                        << "}\n";
                    break;
                }

                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BSPLINE:
                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BOXSPLINETRIANGLE:
                {
                    // "adaptive" subdivision generates refined patches 
                    // (tessellated triangles)
                    accessors
                        << "ivec3 GetPatchParam() {\n"
                        << "  return ivec3(HdGet_primitiveParam().y, \n"
                        << "               HdGet_primitiveParam().z, \n"
                        << "               HdGet_primitiveParam().w);\n"
                        << "}\n";
                    // use the edge flag calculated in the geometry shader
                    // (i.e., not from primitiveParam)
                    // see mesh.glslfx Mesh.Geometry.Triangle
                    accessors
                        << "int GetEdgeFlag(int localIndex) {\n"
                        << "  return localIndex;\n"
                        << "}\n";
                    break;
                }

                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_QUADS:
                {
                    // coarse quads (for ptex)
                    // put ptexIndex into the first element of PatchParam.
                    // (transition flags in MSB can be left as 0)
                    accessors
                        << "ivec3 GetPatchParam() {\n"
                        << "  return ivec3(HdGet_primitiveParam().y, 0, 0);\n"
                        << "}\n";
                    // the edge flag for coarse quads tells us if the quad face
                    // is the result of quadrangulation (1) or from the authored
                    // topology (0).
                    accessors
                        << "int GetEdgeFlag(int localIndex) {\n"
                        << "  return (HdGet_primitiveParam().x & 3); \n"
                        << "}\n";
                    break;
                }

                case HdSt_GeometricShader::PrimitiveType::PRIM_MESH_COARSE_TRIANGLES:
                {
                    // coarse triangles                
                    // note that triangulated meshes don't have ptexIndex.
                    // Here we're passing primitiveID as ptexIndex PatchParam
                    // since Hd_TriangulateFaceVaryingComputation unrolls facevaring
                    // primvars for each triangles.
                    accessors
                        << "ivec3 GetPatchParam() {\n"
                        << "  return ivec3(gl_PrimitiveID, 0, 0);\n"
                        << "}\n";
                    accessors
                        << "int GetEdgeFlag(int localIndex) {\n"
                        << "  return HdGet_primitiveParam() & 3;\n"
                        << "}\n";
                    break;
                }

                default:
                {
                    TF_CODING_ERROR("HdSt_GeometricShader::PrimitiveType %d is "
                      "unexpected in _GenerateElementPrimvar().",
                      (int)_geometricShader->GetPrimitiveType());
                }
            }

            // GetFVarIndex
            if (_geometricShader->IsPrimTypeTriangles() ||
                (_geometricShader->GetPrimitiveType() ==
                 HdSt_GeometricShader::PrimitiveType::PRIM_MESH_BOXSPLINETRIANGLE)) {
                // note that triangulated meshes don't have ptexIndex.
                // Here we're passing primitiveID as ptexIndex PatchParam
                // since Hd_TriangulateFaceVaryingComputation unrolls facevaring
                // primvars for each triangles.
                accessors
                    << "int GetFVarIndex(int localIndex) {\n"
                    << "  int fvarCoord = GetDrawingCoord().fvarCoord;\n"
                    << "  int ptexIndex = GetPatchParam().x & 0xfffffff;\n"
                    << "  return fvarCoord + ptexIndex * 3 + localIndex;\n"
                    << "}\n";    
            }
            else {
                accessors
                    << "int GetFVarIndex(int localIndex) {\n"
                    << "  int fvarCoord = GetDrawingCoord().fvarCoord;\n"
                    << "  int ptexIndex = GetPatchParam().x & 0xfffffff;\n"
                    << "  return fvarCoord + ptexIndex * 4 + localIndex;\n"
                    << "}\n";
            }

            // ElementID getters
            accessors
                << "int GetElementID() {\n"
                << "  return (hd_int_get(HdGet_primitiveParam()) >> 2);\n"
                << "}\n";

            accessors
                << "int GetAggregatedElementID() {\n"
                << "  return GetElementID()\n"
                << "  + GetDrawingCoord().elementCoord;\n"
                << "}\n";
        }
        else {
            TF_CODING_ERROR("HdSt_GeometricShader::PrimitiveType %d is "
                  "unexpected in _GenerateElementPrimvar().",
                  (int)_geometricShader->GetPrimitiveType());
        }
    } else {
        // no primitiveParamBinding

        // XXX: this is here only to keep the compiler happy, we don't expect
        // users to call them -- we really should restructure whatever is
        // necessary to avoid having to do this and thus guarantee that users
        // can never call bogus versions of these functions.

        // Use a fallback of -1, so that points aren't selection highlighted
        // when face 0 is selected. This would be the case if we returned 0,
        // since the selection highlighting code is repr-agnostic.
        // It is safe to do this for points, since  we don't generate accessors 
        // for element primvars, and thus don't use it as an index into
        // elementCoord.
        if (_geometricShader->IsPrimTypePoints()) {
            accessors
              << "int GetElementID() {\n"
              << "  return -1;\n"
              << "}\n";  
        } else {
            accessors
                << "int GetElementID() {\n"
                << "  return 0;\n"
                << "}\n";
        }
        accessors
            << "int GetAggregatedElementID() {\n"
            << "  return GetElementID();\n"
            << "}\n";
        accessors
            << "int GetEdgeFlag(int localIndex) {\n"
            << "  return 0;\n"
            << "}\n";
        accessors
            << "ivec3 GetPatchParam() {\n"
            << "  return ivec3(0, 0, 0);\n"
            << "}\n";
        accessors
            << "int GetFVarIndex(int localIndex) {\n"
            << "  return 0;\n"
            << "}\n";
    }
    declarations
        << "int GetElementID();\n"
        << "int GetAggregatedElementID();\n";


    if (_metaData.edgeIndexBinding.binding.IsValid()) {

        HdBinding binding = _metaData.edgeIndexBinding.binding;

        _EmitDeclaration(declarations, _metaData.edgeIndexBinding);
        _EmitAccessor(accessors, _metaData.edgeIndexBinding.name,
                    _metaData.edgeIndexBinding.dataType, binding,
                    "GetDrawingCoord().primitiveCoord");

        // Authored EdgeID getter
        // abs() is needed below, since both branches may get executed, and
        // we need to guard against array oob indexing.
        accessors
            << "int GetAuthoredEdgeId(int primitiveEdgeID) {\n"
            << "  if (primitiveEdgeID == -1) {\n"
            << "    return -1;\n"
            << "  }\n"
            << "  "
            << _GetUnpackedType(_metaData.edgeIndexBinding.dataType, false)
            << " edgeIndices = HdGet_edgeIndices();\n"
            << "  int coord = abs(primitiveEdgeID);\n"
            << "  return edgeIndices[coord];\n"
            << "}\n";

        // Primitive EdgeID getter
        if (_geometricShader->IsPrimTypePoints()) {
            // we get here only if we're rendering a mesh with the edgeIndices
            // binding and using a points repr. since there is no GS stage, we
            // generate fallback versions.
            // note: this scenario can't be handled in meshShaderKey, since it
            // doesn't know whether an edgeIndices binding exists.
            accessors
                << "int GetPrimitiveEdgeId() {\n"
                << "  return -1;\n"
                << "}\n";
            accessors
                << "bool IsFragmentOnEdge() {\n"
                << "  return false;\n"
                << "}\n";
        }
        else if (_geometricShader->IsPrimTypeBasisCurves()) {
            // basis curves don't have an edge indices buffer bound, so we 
            // shouldn't ever get here.
            TF_VERIFY(false, "edgeIndexBinding shouldn't be found on a "
                             "basis curve");
        }
        else if (_geometricShader->IsPrimTypeMesh()) {
            // nothing to do. meshShaderKey takes care of it.
        }
    } else {
        // The functions below are used in picking (id render) and/or selection
        // highlighting, and are expected to be defined. Generate fallback
        // versions when we don't bind an edgeIndices buffer.
        accessors
            << "int GetAuthoredEdgeId(int primitiveEdgeID) {\n"
            << "  return -1;\n"
            << "}\n";
        accessors
            << "int GetPrimitiveEdgeId() {\n"
            << "  return -1;\n"
            << "}\n";
        accessors
            << "bool IsFragmentOnEdge() {\n"
            << "  return false;\n"
            << "}\n";
        accessors
            << "float GetSelectedEdgeOpacity() {\n"
            << "  return 0.0;\n"
            << "}\n";
    }
    declarations
        << "int GetAuthoredEdgeId(int primitiveEdgeID);\n"
        << "int GetPrimitiveEdgeId();\n"
        << "bool IsFragmentOnEdge();\n"
        << "float GetSelectedEdgeOpacity();\n";

    // Uniform primvar data declarations & accessors
    if (!_geometricShader->IsPrimTypePoints()) {
        TF_FOR_ALL (it, _metaData.elementData) {
            HdBinding binding = it->first;
            TfToken const &name = it->second.name;
            TfToken const &dataType = it->second.dataType;

            _EmitDeclaration(declarations, name, dataType, binding);
            // AggregatedElementID gives us the buffer index post batching, which
            // is what we need for accessing element (uniform) primvar data.
            _EmitAccessor(accessors, name, dataType, binding,"GetAggregatedElementID()");
        }
    }

    // Emit primvar declarations and accessors.
    _genTCS << declarations.str()
            << accessors.str();
    _genTES << declarations.str()
            << accessors.str();
    _genGS << declarations.str()
           << accessors.str();
    _genFS << declarations.str()
           << accessors.str();
}

void
HdSt_CodeGen::_GenerateVertexAndFaceVaryingPrimvar(bool hasGS)
{
    // Vertex and FVar primvar flow into the fragment shader as per-fragment
    // attribute data that has been interpolated by the rasterizer, and hence
    // have similarities for code gen.
    // While vertex primvar are authored per vertex and require plumbing
    // through all shader stages, fVar is emitted only in the GS stage.
    /*
      // --------- vertex data declaration (VS) ----------
      layout (location = 0) in vec3 normals;
      layout (location = 1) in vec3 points;

      out Primvars {
          vec3 normals;
          vec3 points;
      } outPrimvars;

      void ProcessPrimvars() {
          outPrimvars.normals = normals;
          outPrimvars.points = points;
      }

      // --------- geometry stage plumbing -------
      in Primvars {
          vec3 normals;
          vec3 points;
      } inPrimvars[];
      out Primvars {
          vec3 normals;
          vec3 points;
      } outPrimvars;

      void ProcessPrimvars(int index) {
          outPrimvars = inPrimvars[index];
      }

      // --------- vertex data accessors (used in geometry/fragment shader) ---
      in Primvars {
          vec3 normals;
          vec3 points;
      } inPrimvars;
      vec3 HdGet_normals(int localIndex=0) {
          return inPrimvars.normals;
      }
    */

    std::stringstream vertexInputs;
    std::stringstream interstageVertexData;
    std::stringstream accessorsVS, accessorsTCS, accessorsTES,
        accessorsGS, accessorsFS;

    // vertex varying
    TF_FOR_ALL (it, _metaData.vertexData) {
        HdBinding binding = it->first;
        TfToken const &name = it->second.name;
        TfToken const &dataType = it->second.dataType;

        // future work:
        // with ARB_enhanced_layouts extention, it's possible
        // to use "component" qualifier to declare offsetted primvars
        // in interleaved buffer.
        _EmitDeclaration(vertexInputs, name, dataType, binding);

        interstageVertexData << "  " << _GetPackedType(dataType, false)
                             << " " << name << ";\n";

        // primvar accessors
        _EmitAccessor(accessorsVS, name, dataType, binding);

        _EmitStructAccessor(accessorsTCS, _tokens->inPrimvars,
                            name, dataType, /*arraySize=*/1, "gl_InvocationID");
        _EmitStructAccessor(accessorsTES, _tokens->inPrimvars,
                            name, dataType, /*arraySize=*/1, "localIndex");
        _EmitStructAccessor(accessorsGS,  _tokens->inPrimvars,
                            name, dataType, /*arraySize=*/1, "localIndex");
        _EmitStructAccessor(accessorsFS,  _tokens->inPrimvars,
                            name, dataType, /*arraySize=*/1);

        // interstage plumbing
        _procVS << "  outPrimvars." << name
                << " = " << name << ";\n";
        _procTCS << "  outPrimvars[gl_InvocationID]." << name
                 << " = inPrimvars[gl_InvocationID]." << name << ";\n";
        _procTES << "  outPrimvars." << name
                 << " = basis[0] * inPrimvars[i0]." << name
                 << " + basis[1] * inPrimvars[i1]." << name
                 << " + basis[2] * inPrimvars[i2]." << name
                 << " + basis[3] * inPrimvars[i3]." << name << ";\n";

        _procGS  << "  outPrimvars." << name
                 << " = inPrimvars[index]." << name << ";\n";
    }

    /*
      // --------- facevarying data declaration ----------------
      layout (std430, binding=?) buffer buffer0 {
          vec2 map1[];
      };
      layout (std430, binding=?) buffer buffer1 {
          float map2_u[];
      };

      // --------- geometry stage plumbing -------
      out Primvars {
          ...
          vec2 map1;
          float map2_u;
      } outPrimvars;

      void ProcessPrimvars(int index) {
          outPrimvars.map1 = HdGet_map1(index);
          outPrimvars.map2_u = HdGet_map2_u(index);
      }

      // --------- fragment stage plumbing -------
      in Primvars {
          ...
          vec2 map1;
          float map2_u;
      } inPrimvars;

      // --------- facevarying data accessors ----------
      // in geometry shader (internal accessor)
      vec2 HdGet_map1_Coarse(int localIndex) {
          int fvarIndex = GetFVarIndex(localIndex);
          return vec2(map1[fvarIndex]);
      }
      // in geometry shader (public accessor)
      vec2 HdGet_map1(int localIndex) {
          int fvarIndex = GetFVarIndex(localIndex);
          return (HdGet_map1_Coarse(0) * ...);
      }
      // in fragment shader
      vec2 HdGet_map1() {
          return inPrimvars.map1;
      }

    */

    // face varying
    std::stringstream fvarDeclarations;
    std::stringstream interstageFVarData;

     if (hasGS) {
        // FVar primvars are emitted only by the GS.
        // If the GS isn't active, we can skip processing them.
        TF_FOR_ALL (it, _metaData.fvarData) {
            HdBinding binding = it->first;
            TfToken const &name = it->second.name;
            TfToken const &dataType = it->second.dataType;

            _EmitDeclaration(fvarDeclarations, name, dataType, binding);

            interstageFVarData << "  " << _GetPackedType(dataType, false)
                               << " " << name << ";\n";

            // primvar accessors (only in GS and FS)
            _EmitFVarGSAccessor(accessorsGS, name, dataType, binding,
                                _geometricShader->GetPrimitiveType());
            _EmitStructAccessor(accessorsFS, _tokens->inPrimvars, name, dataType,
                                /*arraySize=*/1, NULL);

            _procGS << "  outPrimvars." << name 
                                        <<" = HdGet_" << name << "(index);\n";
        }
    }

    if (!interstageVertexData.str().empty()) {
        _genVS  << vertexInputs.str()
                << "out Primvars {\n"
                << interstageVertexData.str()
                << "} outPrimvars;\n"
                << accessorsVS.str();

        _genTCS << "in Primvars {\n"
                << interstageVertexData.str()
                << "} inPrimvars[gl_MaxPatchVertices];\n"
                << "out Primvars {\n"
                << interstageVertexData.str()
                << "} outPrimvars[HD_NUM_PATCH_VERTS];\n"
                << accessorsTCS.str();

        _genTES << "in Primvars {\n"
                << interstageVertexData.str()
                << "} inPrimvars[gl_MaxPatchVertices];\n"
                << "out Primvars {\n"
                << interstageVertexData.str()
                << "} outPrimvars;\n"
                << accessorsTES.str();

        _genGS  << fvarDeclarations.str()
                << "in Primvars {\n"
                << interstageVertexData.str()
                << "} inPrimvars[HD_NUM_PRIMITIVE_VERTS];\n"
                << "out Primvars {\n"
                << interstageVertexData.str()
                << interstageFVarData.str()
                << "} outPrimvars;\n"
                << accessorsGS.str();

        _genFS  << "in Primvars {\n"
                << interstageVertexData.str()
                << interstageFVarData.str()
                << "} inPrimvars;\n"
                << accessorsFS.str();
    }

    // ---------
    _genFS << "vec4 GetPatchCoord(int index);\n";
    _genFS << "vec4 GetPatchCoord() { return GetPatchCoord(0); }\n";

    _genGS << "vec4 GetPatchCoord(int localIndex);\n";

    // VS specific accessor for the "vertex drawing coordinate"
    // Even though we currently always plumb vertexCoord as part of the drawing
    // coordinate, we expect clients to use this accessor when querying the base
    // vertex offset for a draw call.
    GlfContextCaps const &caps = GlfContextCaps::GetInstance();
    _genVS << "int GetBaseVertexOffset() {\n";
    if (caps.shaderDrawParametersEnabled) {
        if (caps.glslVersion < 460) { // use ARB extension
            _genVS << "  return gl_BaseVertexARB;\n";
        } else {
            _genVS << "  return gl_BaseVertex;\n";
        }
    } else {
        _genVS << "  return GetDrawingCoord().vertexCoord;\n";
    }
    _genVS << "}\n";
}

void
HdSt_CodeGen::_GenerateShaderParameters()
{
    /*
      ------------- Declarations -------------

      // shader parameter buffer
      struct ShaderData {
          <type>          <name>;
          vec4            diffuseColor;     // fallback uniform
          sampler2D       kdTexture;        // uv texture    (bindless texture)
          sampler2DArray  ptexTexels;       // ptex texels   (bindless texture)
          isamplerBuffer  ptexLayouts;      // ptex layouts  (bindless texture)
      };

      // bindless buffer
      layout (location=0) uniform ShaderData *shaderData;
      // not bindless buffer
      layout (std430, binding=0) buffer {
          ShaderData shaderData[];
      };

      // non bindless textures
      uniform sampler2D      samplers_2d[N];
      uniform sampler2DArray samplers_2darray[N];
      uniform isamplerBuffer isamplerBuffers[N];

      ------------- Accessors -------------

      * fallback value
      <type> HdGet_<name>(int localIndex=0) {
          return shaderData[GetDrawingCoord().shaderCoord].<name>
      }

      * primvar redirect
      <type> HdGet_<name>(int localIndex=0) {
          return HdGet_<inPrimvars>().xxx;
      }

      * bindless 2D texture
      <type> HdGet_<name>(int localIndex=0) {
          return texture(sampler2D(shaderData[GetDrawingCoord().shaderCoord].<name>), <inPrimvars>).xxx;
      }

      * non-bindless 2D texture
      <type> HdGet_<name>(int localIndex=0) {
          return texture(samplers_2d[<offset> + drawIndex * <stride>], <inPrimvars>).xxx;
      }

      * bindless Ptex texture
      <type> HdGet_<name>(int localIndex=0) {
          return GlopPtexTextureLookup(<name>_Data, <name>_Packing, GetPatchCoord()).xxx;
      }

      * non-bindless Ptex texture
      <type> HdGet_<name>(int localIndex=0) {
          return GlopPtexTextureLookup(
              samplers_2darray[<offset_ptex_texels> + drawIndex * <stride>],
              isamplerBuffers[<offset_ptex_layouts> + drawIndex * <stride>],
              GetPatchCoord()).xxx;
      }

      * bindless Ptex texture with patchcoord
      <type> HdGet_<name>(vec4 patchCoord) {
          return GlopPtexTextureLookup(<name>_Data, <name>_Packing, patchCoord).xxx;
      }

      * non-bindless Ptex texture
      <type> HdGet_<name>(vec4 patchCoord) {
          return GlopPtexTextureLookup(
              samplers_2darray[<offset_ptex_texels> + drawIndex * <stride>],
              isamplerBuffers[<offset_ptex_layouts> + drawIndex * <stride>],
              patchCoord).xxx;
      }

    */

    std::stringstream declarations;
    std::stringstream accessors;

    GlfContextCaps const &caps = GlfContextCaps::GetInstance();

    TfToken typeName("ShaderData");
    TfToken varName("shaderData");

    // for shader parameters, we create declarations and accessors separetely.
    TF_FOR_ALL (it, _metaData.shaderData) {
        HdBinding binding = it->first;

        declarations << "struct " << typeName << " {\n";

        TF_FOR_ALL (dbIt, it->second.entries) {
            declarations << "  " << _GetPackedType(dbIt->dataType, false)
                         << " " << dbIt->name
                         << ";\n";

        }
        declarations << "};\n";

        // for array delaration, SSBO and bindless uniform can use [].
        // UBO requires the size [N].
        // XXX: [1] is a hack to cheat driver not telling the actual size.
        //      may not work some GPUs.
        // XXX: we only have 1 shaderData entry (interleaved).
        int arraySize = (binding.GetType() == HdBinding::UBO) ? 1 : 0;
        _EmitDeclaration(declarations, varName, typeName, binding, arraySize);

        break;
    }

    // accessors.
    TF_FOR_ALL (it, _metaData.shaderParameterBinding) {

        // adjust datatype
        std::string swizzle = _GetSwizzleString(it->second.dataType);

        HdBinding::Type bindingType = it->first.GetType();
        if (bindingType == HdBinding::FALLBACK) {
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "() {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                << "  return "
                << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(shaderData[shaderCoord]." << it->second.name << swizzle
                << ");\n"
                << "}\n";
        } else if (bindingType == HdBinding::BINDLESS_TEXTURE_2D) {
            // a function returning sampler2D is allowed in 430 or later
            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler2D\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                    << "  return sampler2D(shaderData[shaderCoord]." << it->second.name << ");\n"
                    << "  }\n";
            }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "() {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                << "  return "
                << _GetPackedTypeAccessor(it->second.dataType, false) << "("
                << "texture(sampler2D(shaderData[shaderCoord]." << it->second.name << "), ";

            if (!it->second.inPrimvars.empty()) {
                accessors 
                    << "\n"
                    << "#if defined(HD_HAS_" << it->second.inPrimvars[0] << ")\n"
                    << " HdGet_" << it->second.inPrimvars[0] << "().xy\n"
                    << "#else\n"
                    << "vec2(0.0, 0.0)\n"
                    << "#endif\n";
            } else {
            // allow to fetch uv texture without sampler coordinate for convenience.
                accessors
                    << " vec2(0.0, 0.0)";
            }
            accessors
                << ")" << swizzle << ");\n"
                << "}\n";
        } else if (bindingType == HdBinding::TEXTURE_2D) {
            declarations
                << LayoutQualifier(it->first)
                << "uniform sampler2D sampler2d_" << it->second.name << ";\n";
            // a function returning sampler2D is allowed in 430 or later
            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler2D\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  return sampler2d_" << it->second.name << ";"
                    << "}\n";
            }
            // vec4 HdGet_name(vec2 coord) { return texture(sampler2d_name, coord).xyz; }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name
                << "(vec2 coord) { return "
                << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(texture(sampler2d_" << it->second.name << ", coord)"
                << swizzle << ");}\n";
            // vec4 HdGet_name() { return HdGet_name(HdGet_st().xy); }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name
                << "() { return HdGet_" << it->second.name << "(";
            if (!it->second.inPrimvars.empty()) {
                accessors
                    << "\n"
                    << "#if defined(HD_HAS_" << it->second.inPrimvars[0] << ")\n"
                    << "HdGet_" << it->second.inPrimvars[0] << "().xy\n"
                    << "#else\n"
                    << "vec2(0.0, 0.0)\n"
                    << "#endif\n";
            } else {
                accessors
                    << "vec2(0.0, 0.0)";
            }
            accessors << "); }\n";
        } else if (bindingType == HdBinding::BINDLESS_TEXTURE_3D) {
            // a function returning sampler3D is allowed in 430 or later
            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler3D\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                    << "  return sampler3D(shaderData[shaderCoord]." << it->second.name << ");\n"
                    << "  }\n";
            }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "() {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                << "  return "
                << _GetPackedTypeAccessor(it->second.dataType, false) << "("
                << "texture(sampler3D(shaderData[shaderCoord]." << it->second.name << "), ";

            if (!it->second.inPrimvars.empty()) {
                accessors 
                    << "\n"
                    << "#if defined(HD_HAS_" << it->second.inPrimvars[0] << ")\n"
                    << " HdGet_" << it->second.inPrimvars[0] << "().xyz\n"
                    << "#else\n"
                    << "vec3(0.0, 0.0, 0.0)\n"
                    << "#endif\n";
            } else {
            // allow to fetch uv texture without sampler coordinate for convenience.
                accessors
                    << " vec3(0.0, 0.0, 0.0)";
            }
            accessors
                << ")" << swizzle << ");\n"
                << "}\n";
        } else if (bindingType == HdBinding::TEXTURE_3D) {
            declarations
                << LayoutQualifier(it->first)
                << "uniform sampler3D sampler3d_" << it->second.name << ";\n";
            // a function returning sampler3D is allowed in 430 or later
            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler3D\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  return sampler3d_" << it->second.name << ";"
                    << "}\n";
            }
            // vec4 HdGet_name(vec3 coord) { return texture(sampler3d_name, coord).xyz; }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name
                << "(vec3 coord) { return "
                << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(texture(sampler3d_" << it->second.name << ", coord)"
                << swizzle << ");}\n";
            // vec4 HdGet_name() { return HdGet_name(HdGet_st().xyz); }
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name
                << "() { return HdGet_" << it->second.name << "(";
            if (!it->second.inPrimvars.empty()) {
                accessors
                    << "\n"
                    << "#if defined(HD_HAS_" << it->second.inPrimvars[0] << ")\n"
                    << "HdGet_" << it->second.inPrimvars[0] << "().xyz\n"
                    << "#else\n"
                    << "vec3(0.0, 0.0, 0.0)\n"
                    << "#endif\n";
            } else {
                accessors
                    << "vec3(0.0, 0.0, 0.0)";
            }
            accessors << "); }\n";
        } else if (bindingType == HdBinding::BINDLESS_TEXTURE_UDIM_ARRAY) {
            // a function returning sampler2DArray is allowed in 430 or later
            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler2DArray\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                    << "  return sampler2DArray(shaderData[shaderCoord]."
                    << it->second.name << ");\n"
                    << "  }\n";
            }
            accessors
                << it->second.dataType
                << " HdGet_" << it->second.name << "()" << " {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord;\n";

            if (!it->second.inPrimvars.empty()) {
                accessors
                    << "#if defined(HD_HAS_"
                    << it->second.inPrimvars[0] << ")\n"
                    << "  vec3 c = hd_sample_udim(HdGet_"
                    << it->second.inPrimvars[0] << "().xy);\n"
                    << "  c.z = texelFetch(sampler1D(shaderData[shaderCoord]."
                    << it->second.name << "_layout), int(c.z), 0).x - 1;\n"
                    << "#else\n"
                    << "  vec3 c = vec3(0.0, 0.0, 0.0);\n"
                    << "#endif\n";
            } else {
                accessors
                    << "  vec3 c = vec3(0.0, 0.0, 0.0);\n";
            }
            accessors
                << "if (c.z < -0.5) { return vec4(0, 0, 0, 0)" << swizzle
                << "; } else { \n"
                << "  return texture(sampler2DArray(shaderData[shaderCoord]."
                << it->second.name << "), c)" << swizzle << ";}\n}\n";
        } else if (bindingType == HdBinding::TEXTURE_UDIM_ARRAY) {
            declarations
                << LayoutQualifier(it->first)
                << "uniform sampler2DArray sampler2dArray_"
                << it->second.name << ";\n";

            if (caps.glslVersion >= 430) {
                accessors
                    << "sampler2DArray\n"
                    << "HdGetSampler_" << it->second.name << "() {\n"
                    << "  return sampler2dArray_" << it->second.name << ";"
                    << "}\n";
            }
            // vec4 HdGet_name(vec2 coord) { vec3 c = hd_sample_udim(coord);
            // c.z = texelFetch(sampler1d_name_layout, int(c.z), 0).x - 1;
            // if (c.z < -0.5) { return vec4(0, 0, 0, 0).xyz; } else {
            // return texture(sampler2dArray_name, c).xyz;}}
            accessors
                << it->second.dataType
                << " HdGet_" << it->second.name
                << "(vec2 coord) { vec3 c = hd_sample_udim(coord);\n"
                << "  c.z = texelFetch(sampler1d_" << it->second.name
                << "_layout" << ", int(c.z), 0).x - 1;\n"
                << "if (c.z < -0.5) { return vec4(0, 0, 0, 0)"
                << swizzle << "; } else {\n"
                << "  return texture(sampler2dArray_"
                << it->second.name << ", c)" << swizzle << ";}}\n";
            // vec4 HdGet_name() { return HdGet_name(HdGet_st().xy); }
            accessors
                << it->second.dataType
                << " HdGet_" << it->second.name
                << "() { return HdGet_" << it->second.name << "(";
            if (!it->second.inPrimvars.empty()) {
                accessors
                    << "\n"
                    << "#if defined(HD_HAS_"
                    << it->second.inPrimvars[0] << ")\n"
                    << "HdGet_" << it->second.inPrimvars[0] << "().xy\n"
                    << "#else\n"
                    << "vec2(0.0, 0.0)\n"
                    << "#endif\n";
            } else {
                accessors
                    << "vec2(0.0, 0.0)";
            }
            accessors << "); }\n";
        } else if (bindingType == HdBinding::TEXTURE_UDIM_LAYOUT) {
            declarations
                << LayoutQualifier(it->first)
                << "uniform sampler1D sampler1d_" << it->second.name << ";\n";
        } else if (bindingType == HdBinding::BINDLESS_TEXTURE_PTEX_TEXEL) {
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "(int localIndex) {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                << "  return " << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(GlopPtexTextureLookup("
                << "sampler2DArray(shaderData[shaderCoord]." << it->second.name <<"),"
                << "isamplerBuffer(shaderData[shaderCoord]." << it->second.name << "_layout), "
                << "GetPatchCoord(localIndex))" << swizzle << ");\n"
                << "}\n"
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "()"
                << "{ return HdGet_" << it->second.name << "(0); }\n"
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "(vec4 patchCoord) {\n"
                << "  int shaderCoord = GetDrawingCoord().shaderCoord; \n"
                << "  return " << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(GlopPtexTextureLookup("
                << "sampler2DArray(shaderData[shaderCoord]." << it->second.name <<"),"
                << "isamplerBuffer(shaderData[shaderCoord]." << it->second.name << "_layout), "
                << "patchCoord)" << swizzle << ");\n"
                << "}\n";
        } else if (bindingType == HdBinding::TEXTURE_PTEX_TEXEL) {
            // +1 for layout is by convention.
            declarations
                << LayoutQualifier(it->first)
                << "uniform sampler2DArray sampler2darray_" << it->first.GetLocation() << ";\n"
                << LayoutQualifier(HdBinding(it->first.GetType(),
                                             it->first.GetLocation()+1,
                                             it->first.GetTextureUnit()))
                << "uniform isamplerBuffer isamplerbuffer_" << (it->first.GetLocation()+1) << ";\n";
            accessors
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "(int localIndex) {\n"
                << "  return " << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(GlopPtexTextureLookup("
                << "sampler2darray_" << it->first.GetLocation() << ","
                << "isamplerbuffer_" << (it->first.GetLocation()+1) << ","
                << "GetPatchCoord(localIndex))" << swizzle << ");\n"
                << "}\n"
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "()"
                << "{ return HdGet_" << it->second.name << "(0); }\n"
                << _GetUnpackedType(it->second.dataType, false)
                << " HdGet_" << it->second.name << "(vec4 patchCoord) {\n"
                << "  return " << _GetPackedTypeAccessor(it->second.dataType, false)
                << "(GlopPtexTextureLookup("
                << "sampler2darray_" << it->first.GetLocation() << ","
                << "isamplerbuffer_" << (it->first.GetLocation()+1) << ","
                << "patchCoord)" << swizzle << ");\n"
                << "}\n";
        } else if (bindingType == HdBinding::BINDLESS_TEXTURE_PTEX_LAYOUT) {
            //accessors << _GetUnpackedType(it->second.dataType) << "(0)";
        } else if (bindingType == HdBinding::TEXTURE_PTEX_LAYOUT) {
            //accessors << _GetUnpackedType(it->second.dataType) << "(0)";
        } else if (bindingType == HdBinding::PRIMVAR_REDIRECT) {
            // XXX: shader and primvar name collisions are a problem!
            // If this shader and it's connected primvar have the same name, we
            // are good to go, else we must alias the parameter to the primvar
            // accessor.
            if (it->second.name != it->second.inPrimvars[0]) {
                accessors
                    << _GetUnpackedType(it->second.dataType, false)
                    << " HdGet_" << it->second.name << "() {\n"
                    << "#if defined(HD_HAS_" << it->second.inPrimvars[0] << ")\n"
                    << "  return HdGet_" << it->second.inPrimvars[0] << "();\n"
                    << "#else\n"
                    << "  int shaderCoord = GetDrawingCoord().shaderCoord;\n"
                    << "  return "
                    << _GetPackedTypeAccessor(it->second.dataType, false)
                    << "(shaderData[shaderCoord]." << it->second.name 
                    << swizzle <<  ");\n"
                    << "#endif\n"
                    << "\n}\n"
                    ;
            }
        }
    }

    TF_FOR_ALL (it, _metaData.fieldRedirectBinding) {
        HdBinding::Type bindingType = it->first.GetType();
        if (bindingType == HdBinding::FIELD_REDIRECT) {

            accessors
                << "vec3 HdGet_" << it->second.name << "(vec3 p) {\n"
                << "#if defined(HD_HAS_" << it->second.fieldName << "Texture)\n"
                << "\n"
                << "#if defined(HD_HAS_" << it->second.fieldName << "SamplingTransform)\n"
                << "    vec4 q = vec4(HdGet_" << it->second.fieldName << "SamplingTransform() * vec4(p.xyz, 1));\n"
                << "    vec3 s = q.xyz/q.w;\n"
                << "#else\n"
                << "    vec3 s = p;\n"
                << "#endif\n"
                << "\n"
                << "    return vec3(HdGet_" << it->second.fieldName << "Texture(s).xyz);\n"
                << "#else\n"
                << "#if defined(HD_HAS_" << it->second.name << "Fallback)\n"
                << "    return vec3(HdGet_" << it->second.name << "Fallback().xyz);\n"
                << "#else\n"
                << "    return vec3(0.0);\n"
                << "#endif\n"
                << "#endif\n"
                << "};\n"
                ;
        }
    }
    
    _genFS << declarations.str()
           << accessors.str();

    _genGS << declarations.str()
           << accessors.str();
}

void
HdSt_CodeGen::_GenerateTopologyVisibilityParameters()
{
    std::stringstream declarations;
    std::stringstream accessors;
    TF_FOR_ALL (it, _metaData.topologyVisibilityData) {
        // See note in _GenerateConstantPrimvar re: padding.
        HdBinding binding = it->first;
        TfToken typeName(TfStringPrintf("TopologyVisibilityData%d",
                                        binding.GetValue()));
        TfToken varName = it->second.blockName;

        declarations << "struct " << typeName << " {\n";

        TF_FOR_ALL (dbIt, it->second.entries) {
            if (!TF_VERIFY(!dbIt->dataType.IsEmpty(),
                              "Unknown dataType for %s",
                              dbIt->name.GetText())) {
                continue;
            }

            declarations << "  " << _GetPackedType(dbIt->dataType, false)
                         << " " << dbIt->name;
            if (dbIt->arraySize > 1) {
                declarations << "[" << dbIt->arraySize << "]";
            }

            declarations << ";\n";

            _EmitStructAccessor(accessors, varName, dbIt->name, dbIt->dataType,
                                dbIt->arraySize,
                                "GetDrawingCoord().topologyVisibilityCoord");
        }
        declarations << "};\n";

        _EmitDeclaration(declarations, varName, typeName, binding,
                         /*arraySize=*/1);
    }
    _genCommon << declarations.str()
               << accessors.str();
}

PXR_NAMESPACE_CLOSE_SCOPE

