//
// Copyright 2018 Pixar
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

#include "pxr/imaging/hdx/colorCorrectionTask.h"
#include "pxr/imaging/hdx/package.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdSt/glslProgram.h"
#include "pxr/imaging/hdSt/renderBuffer.h"
#include "pxr/imaging/hf/perfLog.h"
#include "pxr/imaging/glf/contextCaps.h"
#include "pxr/imaging/glf/diagnostic.h"
#include "pxr/imaging/hio/glslfx.h"
#include "pxr/imaging/glf/glContext.h"
#include "pxr/base/tf/setenv.h"
#include "pxr/imaging/hgiGL/texture.h"

#ifdef PXR_OCIO_PLUGIN_ENABLED
    #include <OpenColorIO/OpenColorIO.h>
    namespace OCIO = OCIO_NAMESPACE;
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((colorCorrectionVertex,    "ColorCorrectionVertex"))
    ((colorCorrectionFragment,  "ColorCorrectionFragment"))
    (colorCorrectionShader)
);

namespace {
    enum {
        COLOR_IN = 0,
        POSITION = 1,
        UV_IN = 2,
        LUT3D_IN = 3,
    };
}

HdxColorCorrectionTask::HdxColorCorrectionTask(HdSceneDelegate* delegate, 
                                               SdfPath const& id)
    : HdTask(id)
    , _shaderProgram()
    , _texture(0)
    , _texture3dLUT(0)
    , _textureSize(0)
    , _vertexBuffer(0)
    , _copyFramebuffer(0)
    , _framebufferSize(0)
    , _lut3dSizeOCIO(32)
    , _aovBufferPath()
    , _aovBuffer(nullptr)
    , _aovTexture(nullptr)
    , _aovFramebuffer(0)
{
}

HdxColorCorrectionTask::~HdxColorCorrectionTask()
{
    if (_texture != 0) {
        glDeleteTextures(1, &_texture);
    }

    if (_texture3dLUT != 0) {
        glDeleteTextures(1, &_texture3dLUT);
    }

    if (_vertexBuffer != 0) {
        glDeleteBuffers(1, &_vertexBuffer);
    }

    if (_shaderProgram) {
        _shaderProgram.reset();
    }

    if (_copyFramebuffer != 0) {
        glDeleteFramebuffers(1, &_copyFramebuffer);
    }

    if (_aovFramebuffer != 0) {
        glDeleteFramebuffers(1, &_aovFramebuffer);
    }

    GLF_POST_PENDING_GL_ERRORS();
}

std::string
HdxColorCorrectionTask::_CreateOpenColorIOResources()
{
    #ifdef PXR_OCIO_PLUGIN_ENABLED
        // Use client provided OCIO values, or use default fallback values
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();

        const char* display = _displayOCIO.empty() ? 
                              config->getDefaultDisplay() : 
                              _displayOCIO.c_str();

        const char* view = _viewOCIO.empty() ? 
                           config->getDefaultView(display) :
                           _viewOCIO.c_str();

        std::string inputColorSpace = _colorspaceOCIO;
        if (inputColorSpace.empty()) {
            OCIO::ConstColorSpaceRcPtr cs = config->getColorSpace("default");
            if (cs) {
                inputColorSpace = cs->getName();
            } else {
                inputColorSpace = OCIO::ROLE_SCENE_LINEAR;
            }
        }

        // Setup the transformation we need to apply
        OCIO::DisplayTransformRcPtr transform = OCIO::DisplayTransform::Create();
        transform->setDisplay(display);
        transform->setView(view);
        transform->setInputColorSpaceName(inputColorSpace.c_str());
        if (!_looksOCIO.empty()) {
            transform->setLooksOverride(_looksOCIO.c_str());
            transform->setLooksOverrideEnabled(true);
        } else {
            transform->setLooksOverrideEnabled(false);
        }

        OCIO::ConstProcessorRcPtr processor = config->getProcessor(transform);

        // Create a GPU Shader Description
        OCIO::GpuShaderDesc shaderDesc;
        shaderDesc.setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_0);
        shaderDesc.setFunctionName("OCIODisplay");
        shaderDesc.setLut3DEdgeLen(_lut3dSizeOCIO);

        // Compute and the 3D LUT
        int num3Dentries = 3 * _lut3dSizeOCIO*_lut3dSizeOCIO*_lut3dSizeOCIO;
        std::vector<float> lut3d;
        lut3d.resize(num3Dentries);
        processor->getGpuLut3D(&lut3d[0], shaderDesc);

        // Load the data into an OpenGL 3D Texture
        if (_texture3dLUT != 0) {
            glDeleteTextures(1, &_texture3dLUT);
        }
        GLint restoreTexture;
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &restoreTexture);
        glGenTextures(1, &_texture3dLUT);
        glBindTexture(GL_TEXTURE_3D, _texture3dLUT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F,
                     _lut3dSizeOCIO, _lut3dSizeOCIO, _lut3dSizeOCIO,
                     0, GL_RGB, GL_FLOAT, &lut3d[0]);
        glBindTexture(GL_TEXTURE_3D, restoreTexture);

        const char* gpuShaderText = processor->getGpuShaderText(shaderDesc);

        GLF_POST_PENDING_GL_ERRORS();
        return std::string(gpuShaderText);
    #else
        return std::string();
    #endif
}

bool
HdxColorCorrectionTask::_CreateShaderResources()
{
    if (_shaderProgram) {
        return true;
    }

    // Client can choose to use Hydra's build-in sRGB color correction or use
    // OpenColorIO for color correction in which case we insert extra OCIO code.
    #ifdef PXR_OCIO_PLUGIN_ENABLED
        bool useOCIO = 
            _colorCorrectionMode == HdxColorCorrectionTokens->openColorIO;
    #else
        bool useOCIO = false;
    #endif

    _shaderProgram.reset(new HdStGLSLProgram(_tokens->colorCorrectionShader));

    HioGlslfx glslfx(HdxPackageColorCorrectionShader());

    std::string fragCode = "#version 150\n";

    if (useOCIO) {
        fragCode += "#define GLSLFX_USE_OCIO\n";
    }

    fragCode += glslfx.GetSource(_tokens->colorCorrectionFragment);

    if (useOCIO) {
        std::string ocioGpuShaderText = _CreateOpenColorIOResources();
        fragCode += ocioGpuShaderText;
    }

    if (!_shaderProgram->CompileShader(GL_VERTEX_SHADER,
            glslfx.GetSource(_tokens->colorCorrectionVertex)) ||
        !_shaderProgram->CompileShader(GL_FRAGMENT_SHADER, fragCode) ||
        !_shaderProgram->Link()) {
        TF_CODING_ERROR("Failed to load color correction shader");
        _shaderProgram.reset();
        return false;
    }

    GLuint programId = _shaderProgram->GetProgram().GetId();
    _locations[COLOR_IN]  = glGetUniformLocation(programId, "colorIn");
    _locations[POSITION] = glGetAttribLocation(programId, "position");
    _locations[UV_IN]     = glGetAttribLocation(programId, "uvIn");
    
    if (useOCIO) {
        _locations[LUT3D_IN] = glGetUniformLocation(programId, "LUT3dIn");
    }

    GLF_POST_PENDING_GL_ERRORS();
    return true;
}

bool
HdxColorCorrectionTask::_CreateBufferResources()
{
    if (_vertexBuffer) {
        return true;
    }

    // A larger-than screen triangle with UVs made to fit the screen.
    //                                 positions          |   uvs
    static const float vertices[] = { -1,  3, -1, 1,        0, 2,
                                      -1, -1, -1, 1,        0, 0,
                                       3, -1, -1, 1,        2, 0 };

    glGenBuffers(1, &_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
                 &vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLF_POST_PENDING_GL_ERRORS();
    return true;
}

HgiGLTexture*
HdxColorCorrectionTask::_GetAovHgiGLTexture()
{
    if (HdStRenderBuffer* stAovBuffer =
            dynamic_cast<HdStRenderBuffer*>(_aovBuffer)) {
        
        // XXX Since ColorCorrection is doing its own GL calls (not yet via Hgi)
        // we are directly accessing the HgiGLTexture to get the gl resource id.
        // This should be removed once we can express ColorCorrection via Hgi.

        // ColorCorrection is applied to the resolved, non-ms aov texture.
        return static_cast<HgiGLTexture*>(stAovBuffer->GetTextureHandle());
    } else {
        return nullptr;
    }
}

void
HdxColorCorrectionTask::_CopyTexture()
{
    GLint restoreReadFB, restoreDrawFB;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoreReadFB);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoreDrawFB);

    if (_aovTexture) {
        // If we have an AOV we copy it so we can read from it while writing the
        // color corrected pixels back into the AOV.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _aovFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _copyFramebuffer);

    } else {
        // No AOV provided then make a copy of the default FB color attachment
        // so we can read from the copy and write back into it corrected pixels.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreDrawFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _copyFramebuffer);
    }

    int width = _textureSize[0];
    int height = _textureSize[1];

    glBlitFramebuffer(0, 0, width, height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreReadFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFB);

    GLF_POST_PENDING_GL_ERRORS();
}

bool
HdxColorCorrectionTask::_CreateFramebufferResources()
{
    // If framebufferSize is not provided we use the viewport size.
    // This can be incorrect if the client/app has changed the viewport to
    // be different then the render window size. (E.g. UsdView CameraMask mode)
    GfVec2i fboSize = _framebufferSize;
    if (fboSize[0] <= 0 || fboSize[1] <= 0) {
        GLint res[4] = {0};
        glGetIntegerv(GL_VIEWPORT, res);
        fboSize = GfVec2i(res[2], res[3]);
        _framebufferSize = fboSize;
    }

    bool createTexture = (_texture == 0 || fboSize != _textureSize);

    if (createTexture) {
        if (_texture != 0) {
            glDeleteTextures(1, &_texture);
            _texture = 0;
        }

        _textureSize = fboSize;

        GLint restoreTexture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoreTexture);

        glGenTextures(1, &_texture);
        glBindTexture(GL_TEXTURE_2D, _texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // XXX For now we assume we always want R16F. We could perhaps expose
        //     this as client-API in HdxColorCorrectionTaskParams.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, _textureSize[0], 
                     _textureSize[1], 0, GL_RGBA, GL_FLOAT, 0);

        glBindTexture(GL_TEXTURE_2D, restoreTexture);
    }

    bool switchedGLContext = !_owningContext || !_owningContext->IsCurrent();

    if (switchedGLContext) {
        // If we're rendering with a different context than the render pass
        // was created with, recreate the FBO because FB is not shared.
        // XXX we need this since we use a FBO in _CopyTexture(). Ideally we
        // use HdxCompositor to do the copy, but for that we need to know the
        // textureId currently bound to the default framebuffer. However
        // glGetFramebufferAttachmentParameteriv will return and error when
        // trying to query the texture name bound to GL_BACK_LEFT.
        if (_owningContext && _owningContext->IsValid()) {
            GlfGLContextScopeHolder contextHolder(_owningContext);
            glDeleteFramebuffers(1, &_copyFramebuffer);
            glDeleteFramebuffers(1, &_aovFramebuffer);
        }

        _owningContext = GlfGLContext::GetCurrentGLContext();
        if (!TF_VERIFY(_owningContext, "No valid GL context")) {
            return false;
        }

        if (_copyFramebuffer == 0) {
            glGenFramebuffers(1, &_copyFramebuffer);
        }
        if (_aovFramebuffer == 0) {
            glGenFramebuffers(1, &_aovFramebuffer);
        }
    }

    HgiGLTexture* aovTexture = _GetAovHgiGLTexture();

    if (createTexture || switchedGLContext || aovTexture!=_aovTexture) {
        _aovTexture = aovTexture;

        GLint restoreReadFB, restoreDrawFB;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoreReadFB);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoreDrawFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _copyFramebuffer);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                               GL_TEXTURE_2D, _texture, 0);

        // If an AOV is provided we'll use its texture on the read FB during
        // CopyTexture.
        if (aovTexture) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, _aovFramebuffer);
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                GL_TEXTURE_2D, _aovTexture->GetTextureId(), 0);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreReadFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFB);
    }

    GLF_POST_PENDING_GL_ERRORS();
    return true;
}

void
HdxColorCorrectionTask::_ApplyColorCorrection()
{
    // Client can choose to use Hydra's build-in sRGB color correction or use
    // OpenColorIO for color correction in which case we insert extra OCIO code.
    #ifdef PXR_OCIO_PLUGIN_ENABLED
        bool useOCIO = 
            _colorCorrectionMode == HdxColorCorrectionTokens->openColorIO;
    #else
        bool useOCIO = false;
    #endif

    // A note here: colorCorrection is used for all of our plugins and has to be
    // robust to poor GL support.  OSX compatibility profile provides a
    // GL 2.1 API, slightly restricting our choice of API and heavily
    // restricting our shader syntax. See also HdxCompositor.

    // Read from the texture-copy we made of the clients FBO and output the
    // color-corrected pixels into the clients FBO.

    GLuint programId = _shaderProgram->GetProgram().GetId();
    glUseProgram(programId);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glUniform1i(_locations[COLOR_IN], 0);

    if (useOCIO) {
        glEnable(GL_TEXTURE_3D);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, _texture3dLUT);
        glUniform1i(_locations[LUT3D_IN], 1);
    }

    GlfContextCaps::CoreVAO vao;

    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glVertexAttribPointer(_locations[POSITION], 4, GL_FLOAT, GL_FALSE,
                          sizeof(float)*6, 0);
    glEnableVertexAttribArray(_locations[POSITION]);
    glVertexAttribPointer(_locations[UV_IN], 2, GL_FLOAT, GL_FALSE,
            sizeof(float)*6, reinterpret_cast<void*>(sizeof(float)*4));
    glEnableVertexAttribArray(_locations[UV_IN]);

    // We are rendering a full-screen triangle, which would render to depth.
    // Instead we want to preserve the original depth, so disable depth writes.
    GLboolean restoreDepthWriteMask;
    GLboolean restoreStencilWriteMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &restoreDepthWriteMask);
    glGetBooleanv(GL_STENCIL_WRITEMASK, &restoreStencilWriteMask);
    glDepthMask(GL_FALSE);
    glStencilMask(GL_FALSE);

    // Depth test must be ALWAYS instead of disabling the depth_test because
    // we still want to write to the depth buffer. Disabling depth_test disables
    // depth_buffer writes.
    GLint restoreDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &restoreDepthFunc);
    glDepthFunc(GL_ALWAYS);

    GLint restoreViewport[4] = {0};
    glGetIntegerv(GL_VIEWPORT, restoreViewport);
    glViewport(0, 0, _framebufferSize[0], _framebufferSize[1]);

    // The app may have alpha blending enabled.
    // We want to pass-through the alpha values, not alpha-blend on top of dest.
    GLboolean restoreblendEnabled;
    glGetBooleanv(GL_BLEND, &restoreblendEnabled);
    glDisable(GL_BLEND);

    // Alpha to coverage would prevent any pixels that have an alpha of 0.0 from
    // being written. We want to color correct all pixels. Even background
    // pixels that were set with a clearColor alpha of 0.0
    GLboolean restoreAlphaToCoverage;
    glGetBooleanv(GL_SAMPLE_ALPHA_TO_COVERAGE, &restoreAlphaToCoverage);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (restoreAlphaToCoverage) {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    if (restoreblendEnabled) {
        glEnable(GL_BLEND);
    }

    glViewport(restoreViewport[0], restoreViewport[1],
               restoreViewport[2], restoreViewport[3]);

    glDepthFunc(restoreDepthFunc);
    glDepthMask(restoreDepthWriteMask);
    glStencilMask(restoreStencilWriteMask);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(_locations[POSITION]);
    glDisableVertexAttribArray(_locations[UV_IN]);

    glUseProgram(0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (useOCIO) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, 0);
        glDisable(GL_TEXTURE_3D);
    }

    GLF_POST_PENDING_GL_ERRORS();
}

void
HdxColorCorrectionTask::Sync(HdSceneDelegate* delegate,
                             HdTaskContext* ctx,
                             HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
        HdxColorCorrectionTaskParams params;

        if (_GetTaskParams(delegate, &params)) {
            _framebufferSize = params.framebufferSize;
            _colorCorrectionMode = params.colorCorrectionMode;
            _displayOCIO = params.displayOCIO;
            _viewOCIO = params.viewOCIO;
            _colorspaceOCIO = params.colorspaceOCIO;
            _looksOCIO = params.looksOCIO;
            _lut3dSizeOCIO = params.lut3dSizeOCIO;
            _aovName = params.aovName;
            _aovBufferPath = params.aovBufferPath;
            // Rebuild shader with new OCIO settings / shader-code.
            _shaderProgram.reset();
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
HdxColorCorrectionTask::Prepare(HdTaskContext* ctx,
                                HdRenderIndex* renderIndex)
{
    // Aov path may change when visualizing a different aov (usdview)
    if (!_aovBufferPath.IsEmpty()) {
        _aovBuffer = static_cast<HdRenderBuffer*>(renderIndex->GetBprim(
                HdPrimTypeTokens->renderBuffer, _aovBufferPath));
    } else {
        _aovBuffer = nullptr;
    }
}

void
HdxColorCorrectionTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    GLF_GROUP_FUNCTION();

    // We currently only color correct the color aov. Depth aov currently won't
    // work well due to how we use glBlitFramebuffer. Other aovs may work, if
    // they are color buffers, but it isn't currently clear if we want to
    // color correct those or leave them as their raw values for debugging.
    if (!_aovName.IsEmpty() && _aovName != HdAovTokens->color) {
        return;
    }

    if (!_CreateBufferResources()) {
        return;
    }

    if (!_CreateShaderResources()) {
        return;
    }

    _CreateFramebufferResources();

    _CopyTexture();

    // If an Aov is provided, we render the color corrected pixels in the aov.
    // Otherwise, we render the color corrected pixels into bound FB.
    GLint restoreReadFB, restoreDrawFB;
    if (_aovTexture) {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoreReadFB);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoreDrawFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _aovFramebuffer);
    }

    _ApplyColorCorrection();

    if (_aovTexture) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreReadFB);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFB);
    }
}


// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(
    std::ostream& out, 
    const HdxColorCorrectionTaskParams& pv)
{
    out << "ColorCorrectionTask Params: (...) "
        << pv.framebufferSize << " "
        << pv.colorCorrectionMode << " "
        << pv.displayOCIO << " "
        << pv.viewOCIO << " "
        << pv.colorspaceOCIO << " "
        << pv.looksOCIO << " "
        << pv.lut3dSizeOCIO << " "
        << pv.aovName << " "
        << pv.aovBufferPath
    ;
    return out;
}

bool operator==(const HdxColorCorrectionTaskParams& lhs,
                const HdxColorCorrectionTaskParams& rhs)
{
    return lhs.framebufferSize == rhs. framebufferSize &&
           lhs.colorCorrectionMode == rhs.colorCorrectionMode &&
           lhs.displayOCIO == rhs.displayOCIO &&
           lhs.viewOCIO == rhs.viewOCIO &&
           lhs.colorspaceOCIO == rhs.colorspaceOCIO &&
           lhs.looksOCIO == rhs.looksOCIO &&
           lhs.lut3dSizeOCIO == rhs.lut3dSizeOCIO &&
           lhs.aovName == rhs.aovName &&
           lhs.aovBufferPath == rhs.aovBufferPath;
}

bool operator!=(const HdxColorCorrectionTaskParams& lhs,
                const HdxColorCorrectionTaskParams& rhs)
{
    return !(lhs == rhs);
}

PXR_NAMESPACE_CLOSE_SCOPE
