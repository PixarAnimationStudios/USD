//
// Copyright 2019 Pixar
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

#include "pxr/imaging/hdx/package.h"
#include "pxr/imaging/hdx/oitRenderTask.h"
#include "pxr/imaging/hdx/oitBufferAccessor.h"
#include "pxr/imaging/hdx/tokens.h"
#include "pxr/imaging/hdx/debugCodes.h"

#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/rprimCollection.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include "pxr/imaging/hdSt/lightingShader.h"
#include "pxr/imaging/hdSt/renderPassShader.h"

PXR_NAMESPACE_OPEN_SCOPE

HdxOitRenderTask::HdxOitRenderTask(HdSceneDelegate* delegate, SdfPath const& id)
    : HdxRenderTask(delegate, id)
    , _oitTranslucentRenderPassShader(
        boost::make_shared<HdStRenderPassShader>(
            HdxPackageRenderPassOitShader()))
    , _oitOpaqueRenderPassShader(
        boost::make_shared<HdStRenderPassShader>(
            HdxPackageRenderPassOitOpaqueShader()))
    , _isOitEnabled(HdxOitBufferAccessor::IsOitEnabled())
{
}

HdxOitRenderTask::~HdxOitRenderTask()
{
}

void
HdxOitRenderTask::Sync(
    HdSceneDelegate* delegate,
    HdTaskContext* ctx,
    HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_isOitEnabled) {
        HdxRenderTask::Sync(delegate, ctx, dirtyBits);
    }
}

void
HdxOitRenderTask::Prepare(HdTaskContext* ctx,
                          HdRenderIndex* renderIndex)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_isOitEnabled) {
        HdxRenderTask::Prepare(ctx, renderIndex);

        // OIT buffers take up significant GPU resources. Skip if there are no
        // oit draw items (i.e. no translucent or volumetric draw items)
        if (_GetDrawItemCount() > 0) {
            HdxOitBufferAccessor(ctx).RequestOitBuffers();
        }
    }
}

void
HdxOitRenderTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_isOitEnabled) return;
    if (_GetDrawItemCount() == 0) return;

    //
    // Pre Execute Setup
    //

    HdxOitBufferAccessor oitBufferAccessor(ctx);

    oitBufferAccessor.InitializeOitBuffersIfNecessary();

    HdRenderPassStateSharedPtr renderPassState = _GetRenderPassState(ctx);
    if (!TF_VERIFY(renderPassState)) return;

    HdStRenderPassState* extendedState =
        dynamic_cast<HdStRenderPassState*>(renderPassState.get());
    if (!TF_VERIFY(extendedState, "OIT only works with HdSt")) {
        return;
    }

    extendedState->SetOverrideShader(HdStShaderCodeSharedPtr());

    if (!oitBufferAccessor.AddOitBufferBindings(
            _oitTranslucentRenderPassShader)) {
        TF_CODING_ERROR(
            "No OIT buffers allocated but needed by OIT render task");
        return;
    }
    
    // We render into a SSBO -- not MSSA compatible
    bool oldMSAA = glIsEnabled(GL_MULTISAMPLE);
    glDisable(GL_MULTISAMPLE);
    // XXX When rendering HdStPoints we set GL_POINTS and assume that
    //     GL_POINT_SMOOTH is enabled by default. This renders circles instead
    //     of squares. However, when toggling MSAA off (above) we see GL_POINTS
    //     start to render squares (driver bug?).
    //     For now we always enable GL_POINT_SMOOTH. 
    // XXX Switch points rendering to emit quad with FS that draws circle.
    bool disablePointSmooth = false;
    if (!GlfContextCaps::GetInstance().coreProfile && !glIsEnabled(GL_POINT_SMOOTH)) {
        glEnable(GL_POINT_SMOOTH);
        disablePointSmooth = true;
    }

    // XXX HdxRenderTask::Prepare calls HdStRenderPassState::Prepare.
    // This sets the cullStyle for the render pass shader.
    // Since Oit uses a custom render pass shader, we must manually
    // set cullStyle.
    _oitOpaqueRenderPassShader->SetCullStyle(
        extendedState->GetCullStyle());
    _oitTranslucentRenderPassShader->SetCullStyle(
        extendedState->GetCullStyle());

    //
    // Opaque pixels pass
    // These pixels are rendered to FB instead of OIT buffers
    //
    extendedState->SetRenderPassShader(_oitOpaqueRenderPassShader);
    renderPassState->SetEnableDepthMask(true);
    renderPassState->SetColorMask(HdRenderPassState::ColorMaskRGBA);
    HdxRenderTask::Execute(ctx);

    //
    // Translucent pixels pass
    //
    extendedState->SetRenderPassShader(_oitTranslucentRenderPassShader);
    renderPassState->SetEnableDepthMask(false);
    renderPassState->SetColorMask(HdRenderPassState::ColorMaskNone);
    HdxRenderTask::Execute(ctx);

    //
    // Post Execute Restore
    //

    if (oldMSAA) {
        glEnable(GL_MULTISAMPLE);
    }

    if (disablePointSmooth) {
        glDisable(GL_POINT_SMOOTH);
    }
}


PXR_NAMESPACE_CLOSE_SCOPE
