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
#include "pxr/imaging/glf/contextCaps.h"

#include "pxr/imaging/hdx/compositor.h"
#include "pxr/imaging/hdx/package.h"
#include "pxr/imaging/hdSt/glslProgram.h"
#include "pxr/imaging/hf/perfLog.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/glf/diagnostic.h"
#include "pxr/imaging/hio/glslfx.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((fullscreenVertex,              "FullscreenVertex"))
    ((compositeFragmentNoDepth,      "CompositeFragmentNoDepth"))
    ((compositeFragmentWithDepth,    "CompositeFragmentWithDepth"))
    (fullscreenShader)
);

namespace {
    enum {
        colorIn = 0,
        depthIn = 1,
        position = 2,
        uvIn = 3,
        remapDepthIn = 4
    };
}

HdxCompositor::HdxCompositor()
    : _colorTexture(0), _depthTexture(0)
    , _compositorProgram(), _vertexBuffer(0)
    , _useDepthProgram(false)
{
}

HdxCompositor::~HdxCompositor()
{
    if (_colorTexture != 0) {
        glDeleteTextures(1, &_colorTexture);
    }
    if (_depthTexture != 0) {
        glDeleteTextures(1, &_depthTexture);
    }
    if (_vertexBuffer != 0) {
        glDeleteBuffers(1, &_vertexBuffer);
    }
    if (_compositorProgram) {
        _compositorProgram.reset();
    }
    GLF_POST_PENDING_GL_ERRORS();
}

void
HdxCompositor::_CreateShaderResources(bool useDepthProgram)
{
    _compositorProgram.reset(new HdStGLSLProgram(_tokens->fullscreenShader));
    HioGlslfx glslfx(HdxPackageFullscreenShader());
    TfToken fsToken = useDepthProgram ? _tokens->compositeFragmentWithDepth
                                      : _tokens->compositeFragmentNoDepth;
    if (!_compositorProgram->CompileShader(GL_VERTEX_SHADER,
            glslfx.GetSource(_tokens->fullscreenVertex)) ||
        !_compositorProgram->CompileShader(GL_FRAGMENT_SHADER,
            glslfx.GetSource(fsToken)) ||
        !_compositorProgram->Link()) {
        TF_CODING_ERROR("Failed to load compositing shader");
        _compositorProgram.reset();
        return;
    }
    GLuint programId = _compositorProgram->GetProgram().GetId();
    _locations[colorIn]  = glGetUniformLocation(programId, "colorIn");
    _locations[depthIn]  = glGetUniformLocation(programId, "depthIn");
    _locations[position] = glGetAttribLocation(programId, "position");
    _locations[uvIn]     = glGetAttribLocation(programId, "uvIn");
    _locations[remapDepthIn] = glGetUniformLocation(programId, "remapDepthIn");
}

void
HdxCompositor::_CreateBufferResources()
{
    /* For the fullscreen pass, we draw a triangle:
     *
     * |\
     * |_\
     * | |\
     * |_|_\
     *
     * The vertices are at (-1, 3) [top left]; (-1, -1) [bottom left];
     * and (3, -1) [bottom right]; UVs are assigned so that the bottom left
     * is (0,0) and the clipped vertices are 2 on their axis, so that:
     * x=-1 => s = 0; x = 3 => s = 2, which means x = 1 => s = 1.
     *
     * This maps the texture space [0,1]^2 to the clip space XY [-1,1]^2.
     * The parts of the triangle extending past NDC space are clipped before
     * rasterization.
     *
     * This has the advantage (over rendering a quad) that we don't render
     * the diagonal twice.
     *
     * Note that we're passing in NDC positions, and we don't expect the vertex
     * shader to transform them.  Also note: the fragment shader can optionally
     * read depth from a texture, but otherwise the depth is -1, meaning near
     * plane.
     */
    //                                 positions          |   uvs
    static const float vertices[] = { -1,  3, -1, 1,        0, 2,
                                      -1, -1, -1, 1,        0, 0,
                                       3, -1, -1, 1,        2, 0 };

    glGenBuffers(1, &_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
                 &vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
HdxCompositor::_CreateTextureResources(GLuint *texture)
{
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void
HdxCompositor::UpdateColor(int width, int height, HdFormat format, void* data)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (width == 0 || height == 0) {
        if (_colorTexture != 0) {
            glDeleteTextures(1, &_colorTexture);
            _colorTexture = 0;
        }
        return;
    }

    if (_colorTexture == 0) {
        _CreateTextureResources(&_colorTexture);
    }
    glBindTexture(GL_TEXTURE_2D, _colorTexture);
    if (format == HdFormatFloat32Vec4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA,
                     GL_FLOAT, data);
    } else if (format == HdFormatFloat16Vec4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA,
                     GL_HALF_FLOAT, data);
    } else if (format == HdFormatUNorm8Vec4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, data);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    GLF_POST_PENDING_GL_ERRORS();
}

void
HdxCompositor::UpdateDepth(int width, int height, uint8_t *data)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (width == 0 || height == 0) {
        if (_depthTexture != 0) {
            glDeleteTextures(1, &_depthTexture);
            _depthTexture = 0;
        }
        return;
    }

    if (_depthTexture == 0) {
        _CreateTextureResources(&_depthTexture);
    }
    glBindTexture(GL_TEXTURE_2D, _depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED,
                 GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLF_POST_PENDING_GL_ERRORS();
}

void 
HdxCompositor::Draw(GLuint colorId, GLuint depthId, bool remapDepth)
{
    // No-op if no color data was specified.
    if (colorId == 0) {
        return;
    }

    // Create draw buffers if they haven't been created yet.
    if (_vertexBuffer == 0) {
        _CreateBufferResources();
    }

    bool useDepthProgram = (depthId != 0);

    // Load the shader if it hasn't been loaded, or we're changing modes.
    if (!_compositorProgram || _useDepthProgram != useDepthProgram) {
        _CreateShaderResources(useDepthProgram);
        _useDepthProgram = useDepthProgram;
    }

    // No-op if the shader failed to compile.
    if (!_compositorProgram) {
        return;
    }

    // A note here: HdxCompositor is used for all of our plugins and has to be
    // robust to poor GL support.  OSX compatibility profile provides a
    // GL 2.1 API, slightly restricting our choice of API and heavily
    // restricting our shader syntax.

    GLuint programId = _compositorProgram->GetProgram().GetId();
    glUseProgram(programId);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorId);
    glUniform1i(_locations[colorIn], 0);

    if (depthId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthId);
        glUniform1i(_locations[depthIn], 1);
    }

    glUniform1i(_locations[remapDepthIn], (GLint)remapDepth);

    GlfContextCaps::CoreVAO vao;

    glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
    glVertexAttribPointer(_locations[position], 4, GL_FLOAT, GL_FALSE,
            sizeof(float)*6, 0);
    glEnableVertexAttribArray(_locations[position]);
    glVertexAttribPointer(_locations[uvIn], 2, GL_FLOAT, GL_FALSE,
            sizeof(float)*6, reinterpret_cast<void*>(sizeof(float)*4));
    glEnableVertexAttribArray(_locations[uvIn]);

    GLboolean restoreAlphaToCoverage;
    glGetBooleanv(GL_SAMPLE_ALPHA_TO_COVERAGE, &restoreAlphaToCoverage);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (restoreAlphaToCoverage) {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(_locations[position]);
    glDisableVertexAttribArray(_locations[uvIn]);

    glUseProgram(0);

    if (depthId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLF_POST_PENDING_GL_ERRORS();
}

void
HdxCompositor::Draw()
{
    // we default remapDepth to true because RmMan/Embree give us depth
    // in the -1, 1 range.
    Draw(_colorTexture, _depthTexture, /*remapDepth*/ true);
}

PXR_NAMESPACE_CLOSE_SCOPE
