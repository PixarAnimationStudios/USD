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
#include "pxr/usdImaging/usdImagingGL/engine.h"

#include "pxr/usdImaging/usdImagingGL/legacyEngine.h"

#include "pxr/usdImaging/usdImaging/delegate.h"

#include "pxr/usd/usdGeom/tokens.h"

#include "pxr/imaging/glf/diagnostic.h"
#include "pxr/imaging/glf/contextCaps.h"
#include "pxr/imaging/glf/glContext.h"
#include "pxr/imaging/glf/info.h"

#include "pxr/imaging/hdx/rendererPlugin.h"
#include "pxr/imaging/hdx/rendererPluginRegistry.h"
#include "pxr/imaging/hdx/taskController.h"
#include "pxr/imaging/hdx/tokens.h"

#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/stl.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

static
bool
_GetHydraEnabledEnvVar()
{
    // XXX: Note that we don't cache the result here.  This is primarily because
    // of the way usdview currently interacts with this setting.  This should
    // be cleaned up, and the new class hierarchy around UsdImagingGLEngine
    // makes it much easier to do so.
    return TfGetenv("HD_ENABLED", "1") == "1";
}

static
void _InitGL()
{
    // Initialize Glew library for GL Extensions if needed
    GlfGlewInit();

    // Initialize if needed and switch to shared GL context.
    GlfSharedGLContextScopeHolder sharedContext;

    // Initialize GL context caps based on shared context
    GlfContextCaps::InitInstance();
}

static
bool
_IsHydraEnabled()
{
    // Make sure there is an OpenGL context when 
    // trying to initialize Hydra/Reference
    GlfGLContextSharedPtr context = GlfGLContext::GetCurrentGLContext();
    if (!context || !context->IsValid()) {
        TF_CODING_ERROR("OpenGL context required, using reference renderer");
        return false;
    }

    if (!_GetHydraEnabledEnvVar()) {
        return false;
    }
    
    // Check to see if we have a default plugin for the renderer
    TfToken defaultPlugin = 
        HdxRendererPluginRegistry::GetInstance().GetDefaultPluginId();

    return !defaultPlugin.IsEmpty();
}

} // anonymous namespace

//----------------------------------------------------------------------------
// Global State
//----------------------------------------------------------------------------

/*static*/
bool
UsdImagingGLEngine::IsHydraEnabled()
{
    static bool isHydraEnabled = _IsHydraEnabled();
    return isHydraEnabled;
}

//----------------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------------

UsdImagingGLEngine::UsdImagingGLEngine()
    : _renderIndex(nullptr)
    , _selTracker(new HdxSelectionTracker)
    , _delegateID(SdfPath::AbsoluteRootPath())
    , _delegate(nullptr)
    , _rendererPlugin(nullptr)
    , _taskController(nullptr)
    , _selectionColor(1.0f, 1.0f, 0.0f, 1.0f)
    , _rootPath(SdfPath::AbsoluteRootPath())
    , _excludedPrimPaths()
    , _invisedPrimPaths()
    , _isPopulated(false)
    , _restoreViewport(0)
    , _useFloatPointDrawTarget(false)
{
    static std::once_flag initFlag;

    std::call_once(initFlag, _InitGL);

    if (IsHydraEnabled()) {

        // _renderIndex, _taskController, and _delegate are initialized
        // by the plugin system.
        if (!SetRendererPlugin(_GetDefaultRendererPluginId())) {
            TF_CODING_ERROR("No renderer plugins found! "
                            "Check before creation.");
        }

    } else {

        SdfPathVector excluded;
        _legacyImpl.reset(new UsdImagingGLLegacyEngine(excluded));

    }
}

UsdImagingGLEngine::UsdImagingGLEngine(
    const SdfPath& rootPath,
    const SdfPathVector& excludedPaths,
    const SdfPathVector& invisedPaths,
    const SdfPath& delegateID)
    : _renderIndex(nullptr)
    , _selTracker(new HdxSelectionTracker)
    , _delegateID(delegateID)
    , _delegate(nullptr)
    , _rendererPlugin(nullptr)
    , _taskController(nullptr)
    , _selectionColor(1.0f, 1.0f, 0.0f, 1.0f)
    , _rootPath(rootPath)
    , _excludedPrimPaths(excludedPaths)
    , _invisedPrimPaths(invisedPaths)
    , _isPopulated(false)
    , _restoreViewport(0)
    , _useFloatPointDrawTarget(false)
{
    static std::once_flag initFlag;

    std::call_once(initFlag, _InitGL);

    if (IsHydraEnabled()) {

        // _renderIndex, _taskController, and _delegate are initialized
        // by the plugin system.
        if (!SetRendererPlugin(_GetDefaultRendererPluginId())) {
            TF_CODING_ERROR("No renderer plugins found! "
                            "Check before creation.");
        }

    } else {

        // In the legacy implementation, both excluded paths and invised paths 
        // are treated the same way.
        SdfPathVector pathsToExclude = excludedPaths;
        pathsToExclude.insert(pathsToExclude.end(), 
            invisedPaths.begin(), invisedPaths.end());
        _legacyImpl.reset(new UsdImagingGLLegacyEngine(pathsToExclude));
    }
}

UsdImagingGLEngine::~UsdImagingGLEngine()
{ 
    _DeleteHydraResources();
}

//----------------------------------------------------------------------------
// Rendering
//----------------------------------------------------------------------------

void
UsdImagingGLEngine::PrepareBatch(
    const UsdPrim& root, 
    const UsdImagingGLRenderParams& params)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    HD_TRACE_FUNCTION();

    TF_VERIFY(_delegate);

    if (_CanPrepareBatch(root, params)) {
        if (!_isPopulated) {
            _delegate->SetUsdDrawModesEnabled(params.enableUsdDrawModes);
            _delegate->Populate(root.GetStage()->GetPrimAtPath(_rootPath),
                               _excludedPrimPaths);
            _delegate->SetInvisedPrimPaths(_invisedPrimPaths);
            _isPopulated = true;
        }

        _PreSetTime(root, params);
        // SetTime will only react if time actually changes.
        _delegate->SetTime(params.frame);
        _PostSetTime(root, params);
    }
}

void
UsdImagingGLEngine::RenderBatch(
    const SdfPathVector& paths, 
    const UsdImagingGLRenderParams& params)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_taskController);

    _taskController->SetCameraClipPlanes(params.clipPlanes);
    _UpdateHydraCollection(&_renderCollection, paths, params);
    _taskController->SetCollection(_renderCollection);

    TfTokenVector renderTags;
    _ComputeRenderTags(params, &renderTags);
    _taskController->SetRenderTags(renderTags);

    HdxRenderTaskParams hdParams = _MakeHydraUsdImagingGLRenderParams(params);

    _taskController->SetRenderParams(hdParams);
    _taskController->SetEnableSelection(params.highlight);

    SetColorCorrectionSettings(params.colorCorrectionMode, 
                               params.renderResolution);

    // Forward scene materials enable option to delegate
    _delegate->SetSceneMaterialsEnabled(params.enableSceneMaterials);

    VtValue selectionValue(_selTracker);
    _engine.SetTaskContextData(HdxTokens->selectionState, selectionValue);
    _Execute(params, /*fp16 draw target*/true,
             _taskController->GetRenderingTasks());
}

void 
UsdImagingGLEngine::Render(
    const UsdPrim& root, 
    const UsdImagingGLRenderParams &params)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return _legacyImpl->Render(root, params);
    }

    TF_VERIFY(_taskController);

    PrepareBatch(root, params);

    // XXX(UsdImagingPaths): Is it correct to map USD root path directly
    // to the cachePath here?
    SdfPath cachePath = root.GetPath();
    SdfPathVector roots(1, _delegate->ConvertCachePathToIndexPath(cachePath));

    _taskController->SetCameraClipPlanes(params.clipPlanes);
    _UpdateHydraCollection(&_renderCollection, roots, params);
    _taskController->SetCollection(_renderCollection);

    TfTokenVector renderTags;
    _ComputeRenderTags(params, &renderTags);
    _taskController->SetRenderTags(renderTags);


    HdxRenderTaskParams hdParams = _MakeHydraUsdImagingGLRenderParams(params);
    _taskController->SetRenderParams(hdParams);
    _taskController->SetEnableSelection(params.highlight);

    SetColorCorrectionSettings(params.colorCorrectionMode, 
                               params.renderResolution);

    // Forward scene materials enable option to delegate
    _delegate->SetSceneMaterialsEnabled(params.enableSceneMaterials);

    VtValue selectionValue(_selTracker);
    _engine.SetTaskContextData(HdxTokens->selectionState, selectionValue);
    _Execute(params, /*fp16 draw target*/true,
             _taskController->GetRenderingTasks());
}

void
UsdImagingGLEngine::InvalidateBuffers()
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return _legacyImpl->InvalidateBuffers();
    }
}

bool
UsdImagingGLEngine::IsConverged() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return true;
    }

    TF_VERIFY(_taskController);
    return _taskController->IsConverged();
}

//----------------------------------------------------------------------------
// Root and Transform Visibility
//----------------------------------------------------------------------------

void
UsdImagingGLEngine::SetRootTransform(GfMatrix4d const& xf)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_delegate);
    _delegate->SetRootTransform(xf);
}

void
UsdImagingGLEngine::SetRootVisibility(bool isVisible)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_delegate);
    _delegate->SetRootVisibility(isVisible);
}

//----------------------------------------------------------------------------
// Camera and Light State
//----------------------------------------------------------------------------

void 
UsdImagingGLEngine::SetCameraState(
    const GfMatrix4d& viewMatrix,
    const GfMatrix4d& projectionMatrix,
    const GfVec4d& viewport)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        _legacyImpl->SetCameraState(viewMatrix, projectionMatrix, viewport);
        return;
    }

    TF_VERIFY(_taskController);

    // usdview passes these matrices from OpenGL state.
    // update the camera in the task controller accordingly.
    _taskController->SetCameraMatrices(viewMatrix, projectionMatrix);
    _taskController->SetCameraViewport(viewport);
}

void
UsdImagingGLEngine::SetCameraStateFromOpenGL()
{
    GfMatrix4d viewMatrix, projectionMatrix;
    GfVec4d viewport;
    glGetDoublev(GL_MODELVIEW_MATRIX, viewMatrix.GetArray());
    glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix.GetArray());
    glGetDoublev(GL_VIEWPORT, &viewport[0]);

    SetCameraState(viewMatrix, projectionMatrix, viewport);
}

void
UsdImagingGLEngine::SetLightingStateFromOpenGL()
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_taskController);

    if (!_lightingContextForOpenGLState) {
        _lightingContextForOpenGLState = GlfSimpleLightingContext::New();
    }
    _lightingContextForOpenGLState->SetStateFromOpenGL();

    _taskController->SetLightingState(_lightingContextForOpenGLState);
}

void
UsdImagingGLEngine::SetLightingState(GlfSimpleLightingContextPtr const &src)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_taskController);
    _taskController->SetLightingState(src);
}

void
UsdImagingGLEngine::SetLightingState(
    GlfSimpleLightVector const &lights,
    GlfSimpleMaterial const &material,
    GfVec4f const &sceneAmbient)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        _legacyImpl->SetLightingState(lights, material, sceneAmbient);
        return;
    }

    TF_VERIFY(_taskController);

    // we still use _lightingContextForOpenGLState for convenience, but
    // set the values directly.
    if (!_lightingContextForOpenGLState) {
        _lightingContextForOpenGLState = GlfSimpleLightingContext::New();
    }
    _lightingContextForOpenGLState->SetLights(lights);
    _lightingContextForOpenGLState->SetMaterial(material);
    _lightingContextForOpenGLState->SetSceneAmbient(sceneAmbient);
    _lightingContextForOpenGLState->SetUseLighting(lights.size() > 0);

    _taskController->SetLightingState(_lightingContextForOpenGLState);
}

//----------------------------------------------------------------------------
// Selection Highlighting
//----------------------------------------------------------------------------

void
UsdImagingGLEngine::SetSelected(SdfPathVector const& paths)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_delegate);

    // populate new selection
    HdSelectionSharedPtr selection(new HdSelection);
    // XXX: Usdview currently supports selection on click. If we extend to
    // rollover (locate) selection, we need to pass that mode here.
    HdSelection::HighlightMode mode = HdSelection::HighlightModeSelect;
    for (SdfPath const& path : paths) {
        _delegate->PopulateSelection(mode,
                                     path,
                                     UsdImagingDelegate::ALL_INSTANCES,
                                     selection);
    }

    // set the result back to selection tracker
    _selTracker->SetSelection(selection);
}

void
UsdImagingGLEngine::ClearSelected()
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_selTracker);

    HdSelectionSharedPtr selection(new HdSelection);
    _selTracker->SetSelection(selection);
}

void
UsdImagingGLEngine::AddSelected(SdfPath const &path, int instanceIndex)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_delegate);

    HdSelectionSharedPtr selection = _selTracker->GetSelectionMap();
    if (!selection) {
        selection.reset(new HdSelection);
    }
    // XXX: Usdview currently supports selection on click. If we extend to
    // rollover (locate) selection, we need to pass that mode here.
    HdSelection::HighlightMode mode = HdSelection::HighlightModeSelect;
    _delegate->PopulateSelection(mode, path, instanceIndex, selection);

    // set the result back to selection tracker
    _selTracker->SetSelection(selection);
}

void
UsdImagingGLEngine::SetSelectionColor(GfVec4f const& color)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_taskController);

    _selectionColor = color;
    _taskController->SetSelectionColor(_selectionColor);
}

//----------------------------------------------------------------------------
// Picking
//----------------------------------------------------------------------------

bool 
UsdImagingGLEngine::TestIntersection(
    const GfMatrix4d &viewMatrix,
    const GfMatrix4d &projectionMatrix,
    const GfMatrix4d &worldToLocalSpace,
    const UsdPrim& root,
    const UsdImagingGLRenderParams& params,
    GfVec3d *outHitPoint,
    SdfPath *outHitPrimPath,
    SdfPath *outHitInstancerPath,
    int *outHitInstanceIndex,
    int *outHitElementIndex)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return _legacyImpl->TestIntersection(
            viewMatrix,
            projectionMatrix,
            worldToLocalSpace,
            root,
            params,
            outHitPoint,
            outHitPrimPath,
            outHitInstancerPath,
            outHitInstanceIndex,
            outHitElementIndex);
    }

    TF_VERIFY(_delegate);
    TF_VERIFY(_taskController);

    // XXX(UsdImagingPaths): Is it correct to map USD root path directly
    // to the cachePath here?
    SdfPath cachePath = root.GetPath();
    SdfPathVector roots(1, _delegate->ConvertCachePathToIndexPath(cachePath));
    _UpdateHydraCollection(&_intersectCollection, roots, params);

    TfTokenVector renderTags;
    _ComputeRenderTags(params, &renderTags);
    _taskController->SetRenderTags(renderTags);

    HdxRenderTaskParams hdParams = _MakeHydraUsdImagingGLRenderParams(params);
    _taskController->SetRenderParams(hdParams);

    // Forward scene materials enable option to delegate
    _delegate->SetSceneMaterialsEnabled(params.enableSceneMaterials);

    HdxPickHitVector allHits;
    HdxPickTaskContextParams pickParams;
    pickParams.resolveMode = HdxPickTokens->resolveNearestToCenter;
    pickParams.viewMatrix = worldToLocalSpace * viewMatrix;
    pickParams.projectionMatrix = projectionMatrix;
    pickParams.clipPlanes = params.clipPlanes;
    pickParams.collection = _intersectCollection;
    pickParams.outHits = &allHits;
    VtValue vtPickParams(pickParams);

    _engine.SetTaskContextData(HdxPickTokens->pickParams, vtPickParams);
    _Execute(params, /*fp16 draw target*/false,
             _taskController->GetPickingTasks());

    // Since we are in nearest-hit mode, we expect allHits to have
    // a single point in it.
    if (allHits.size() != 1) {
        return false;
    }

    HdxPickHit &hit = allHits[0];

    if (outHitPoint) {
        *outHitPoint = GfVec3d(hit.worldSpaceHitPoint[0],
                               hit.worldSpaceHitPoint[1],
                               hit.worldSpaceHitPoint[2]);
    }
    if (outHitPrimPath) {
        *outHitPrimPath = hit.objectId;
    }
    if (outHitInstancerPath) {
        *outHitInstancerPath = hit.instancerId;
    }
    if (outHitInstanceIndex) {
        *outHitInstanceIndex = hit.instanceIndex;
    }
    if (outHitElementIndex) {
        *outHitElementIndex = hit.elementIndex;
    }

    return true;
}

SdfPath
UsdImagingGLEngine::GetRprimPathFromPrimId(int primId) const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return _legacyImpl->GetRprimPathFromPrimId(primId);
    }

    TF_VERIFY(_delegate);
    return _delegate->GetRenderIndex().GetRprimPathFromPrimId(primId);
}

SdfPath
UsdImagingGLEngine::GetPrimPathFromPrimIdColor(
    GfVec4i const &primIdColor,
    GfVec4i const &instanceIdColor,
    int * instanceIndexOut)
{
    unsigned char primIdColorBytes[] =  {
        uint8_t(primIdColor[0]),
        uint8_t(primIdColor[1]),
        uint8_t(primIdColor[2]),
        uint8_t(primIdColor[3])
    };

    int primId = DecodeIDRenderColor(primIdColorBytes);
    SdfPath result = GetRprimPathFromPrimId(primId);
    if (!result.IsEmpty()) {
        if (instanceIndexOut) {
            unsigned char instanceIdColorBytes[] =  {
                uint8_t(instanceIdColor[0]),
                uint8_t(instanceIdColor[1]),
                uint8_t(instanceIdColor[2]),
                uint8_t(instanceIdColor[3])
            };
            *instanceIndexOut = DecodeIDRenderColor(instanceIdColorBytes);
        }
    }
    return result;
}

SdfPath 
UsdImagingGLEngine::GetPrimPathFromInstanceIndex(
    SdfPath const& protoPrimPath,
    int instanceIndex,
    int *absoluteInstanceIndex,
    SdfPath *rprimPath,
    SdfPathVector *instanceContext)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return SdfPath();
    }

    TF_VERIFY(_delegate);

    return _delegate->GetPathForInstanceIndex(protoPrimPath, instanceIndex,
                                             absoluteInstanceIndex, rprimPath,
                                             instanceContext);
}

SdfPath
UsdImagingGLEngine::ConvertCachePathToIndexPath(SdfPath const& cachePath)
{
    if (ARCH_LIKELY(_delegate)) {
        return _delegate->ConvertCachePathToIndexPath(cachePath);
    }
    return cachePath;
}

SdfPath
UsdImagingGLEngine::ConvertIndexPathToCachePath(SdfPath const& indexPath)
{
    if (ARCH_LIKELY(_delegate)) {
        return _delegate->ConvertIndexPathToCachePath(indexPath);
    }
    return indexPath;
}

//----------------------------------------------------------------------------
// Renderer Plugin Management
//----------------------------------------------------------------------------

/* static */
TfTokenVector
UsdImagingGLEngine::GetRendererPlugins()
{
    if (ARCH_UNLIKELY(!_GetHydraEnabledEnvVar())) {
        // No plugins if the legacy implementation is active.
        return std::vector<TfToken>();
    }

    HfPluginDescVector pluginDescriptors;
    HdxRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescriptors);

    TfTokenVector plugins;
    for(size_t i = 0; i < pluginDescriptors.size(); ++i) {
        plugins.push_back(pluginDescriptors[i].id);
    }
    return plugins;
}

/* static */
std::string
UsdImagingGLEngine::GetRendererDisplayName(TfToken const &id)
{
    if (ARCH_UNLIKELY(!_GetHydraEnabledEnvVar() || id.IsEmpty())) {
        // No renderer name is returned if the user requested to disable Hydra, 
        // or if the machine does not support any of the available renderers 
        // and it automatically switches to our legacy engine.
        return std::string();
    }

    HfPluginDesc pluginDescriptor;
    if (!TF_VERIFY(HdxRendererPluginRegistry::GetInstance().
                   GetPluginDesc(id, &pluginDescriptor))) {
        return std::string();
    }

    return pluginDescriptor.displayName;
}

TfToken
UsdImagingGLEngine::GetCurrentRendererId() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        // No renderer support if the legacy implementation is active.
        return TfToken();
    }

    return _rendererId;
}

bool
UsdImagingGLEngine::SetRendererPlugin(TfToken const &id)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return false;
    }

    HdxRendererPlugin *plugin = nullptr;
    TfToken actualId = id;

    // Special case: TfToken() selects the first plugin in the list.
    if (actualId.IsEmpty()) {
        actualId = HdxRendererPluginRegistry::GetInstance().
            GetDefaultPluginId();
    }
    plugin = HdxRendererPluginRegistry::GetInstance().
        GetRendererPlugin(actualId);

    if (plugin == nullptr) {
        TF_CODING_ERROR("Couldn't find plugin for id %s", actualId.GetText());
        return false;
    } else if (plugin == _rendererPlugin) {
        // It's a no-op to load the same plugin twice.
        HdxRendererPluginRegistry::GetInstance().ReleasePlugin(plugin);
        return true;
    } else if (!plugin->IsSupported()) {
        // Don't do anything if the plugin isn't supported on the running
        // system, just return that we're not able to set it.
        HdxRendererPluginRegistry::GetInstance().ReleasePlugin(plugin);
        return false;
    }

    // Pull old delegate/task controller state.
    GfMatrix4d rootTransform = GfMatrix4d(1.0);
    bool isVisible = true;
    if (_delegate != nullptr) {
        rootTransform = _delegate->GetRootTransform();
        isVisible = _delegate->GetRootVisibility();
    }
    HdSelectionSharedPtr selection = _selTracker->GetSelectionMap();
    if (!selection) {
        selection.reset(new HdSelection);
    }

    // Delete hydra state.
    _DeleteHydraResources();

    // Recreate the render index.
    _rendererPlugin = plugin;
    _rendererId = actualId;

    HdRenderDelegate *renderDelegate = _rendererPlugin->CreateRenderDelegate();
    _renderIndex = HdRenderIndex::New(renderDelegate);

    // Create the new delegate & task controller.
    _delegate = new UsdImagingDelegate(_renderIndex, _delegateID);
    _isPopulated = false;

    _taskController = new HdxTaskController(_renderIndex,
        _delegateID.AppendChild(TfToken(TfStringPrintf(
            "_UsdImaging_%s_%p",
            TfMakeValidIdentifier(actualId.GetText()).c_str(),
            this))));

    // Rebuild state in the new delegate/task controller.
    _delegate->SetRootVisibility(isVisible);
    _delegate->SetRootTransform(rootTransform);
    _selTracker->SetSelection(selection);
    _taskController->SetSelectionColor(_selectionColor);

    return true;
}

//----------------------------------------------------------------------------
// AOVs and Renderer Settings
//----------------------------------------------------------------------------

TfTokenVector
UsdImagingGLEngine::GetRendererAovs() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return std::vector<TfToken>();
    }

    TF_VERIFY(_renderIndex);

    if (_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer)) {
        TfTokenVector aovs;
        aovs.push_back(HdAovTokens->color);

        TfToken candidates[] =
            { HdAovTokens->primId,
              HdAovTokens->depth,
              HdAovTokens->normal,
              HdAovTokensMakePrimvar(TfToken("st")) };

        HdRenderDelegate *renderDelegate = _renderIndex->GetRenderDelegate();
        for (auto const& aov : candidates) {
            if (renderDelegate->GetDefaultAovDescriptor(aov).format 
                    != HdFormatInvalid) {
                aovs.push_back(aov);
            }
        }
        return aovs;
    }
    return TfTokenVector();
}

bool
UsdImagingGLEngine::SetRendererAov(TfToken const &id)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return false;
    }

    TF_VERIFY(_renderIndex);
    if (_renderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer)) {
        _taskController->SetRenderOutputs({id});
        return true;
    }
    return false;
}

UsdImagingGLRendererSettingsList
UsdImagingGLEngine::GetRendererSettingsList() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return UsdImagingGLRendererSettingsList();
    }

    TF_VERIFY(_renderIndex);

    HdRenderSettingDescriptorList descriptors =
        _renderIndex->GetRenderDelegate()->GetRenderSettingDescriptors();
    UsdImagingGLRendererSettingsList ret;

    for (auto const& desc : descriptors) {
        UsdImagingGLRendererSetting r;
        r.key = desc.key;
        r.name = desc.name;
        r.defValue = desc.defaultValue;

        // Use the type of the default value to tell us what kind of
        // widget to create...
        if (r.defValue.IsHolding<bool>()) {
            r.type = UsdImagingGLRendererSetting::TYPE_FLAG;
        } else if (r.defValue.IsHolding<int>() ||
                   r.defValue.IsHolding<unsigned int>()) {
            r.type = UsdImagingGLRendererSetting::TYPE_INT;
        } else if (r.defValue.IsHolding<float>()) {
            r.type = UsdImagingGLRendererSetting::TYPE_FLOAT;
        } else if (r.defValue.IsHolding<std::string>()) {
            r.type = UsdImagingGLRendererSetting::TYPE_STRING;
        } else {
            TF_WARN("Setting '%s' with type '%s' doesn't have a UI"
                    " implementation...",
                    r.name.c_str(),
                    r.defValue.GetTypeName().c_str());
            continue;
        }
        ret.push_back(r);
    }

    return ret;
}

VtValue
UsdImagingGLEngine::GetRendererSetting(TfToken const& id) const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return VtValue();
    }

    TF_VERIFY(_renderIndex);
    return _renderIndex->GetRenderDelegate()->GetRenderSetting(id);
}

void
UsdImagingGLEngine::SetRendererSetting(TfToken const& id, VtValue const& value)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_renderIndex);
    _renderIndex->GetRenderDelegate()->SetRenderSetting(id, value);
}

void 
UsdImagingGLEngine::SetEnableFloatPointDrawTarget(bool state)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    _useFloatPointDrawTarget = state;
}

//----------------------------------------------------------------------------
// Color Correction
//----------------------------------------------------------------------------
void 
UsdImagingGLEngine::SetColorCorrectionSettings(
    TfToken const& id,
    GfVec2i const& framebufferResolution)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    if (!IsColorCorrectionCapable()) {
        return;
    }

    TF_VERIFY(_taskController);

    HdxColorCorrectionTaskParams hdParams;
    hdParams.framebufferSize = framebufferResolution;
    hdParams.colorCorrectionMode = id;
    _taskController->SetColorCorrectionParams(hdParams);
}

bool 
UsdImagingGLEngine::IsColorCorrectionCapable()
{
    return GlfContextCaps::GetInstance().floatingPointBuffersEnabled && 
           IsHydraEnabled();
}

//----------------------------------------------------------------------------
// Resource Information
//----------------------------------------------------------------------------

VtDictionary
UsdImagingGLEngine::GetResourceAllocation() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return VtDictionary();
    }

    TF_VERIFY(_renderIndex);
    return _renderIndex->GetResourceRegistry()->GetResourceAllocation();
}

//----------------------------------------------------------------------------
// Private/Protected
//----------------------------------------------------------------------------

HdRenderIndex *
UsdImagingGLEngine::_GetRenderIndex() const
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return nullptr;
    }

    return _renderIndex;
}

void 
UsdImagingGLEngine::_Execute(const UsdImagingGLRenderParams &params,
                             bool fp16DrawTarget,
                             HdTaskSharedPtrVector tasks)
{
    if (ARCH_UNLIKELY(_legacyImpl)) {
        return;
    }

    TF_VERIFY(_delegate);

    // Render into our interal framebuffer for color management / post-effects.
    if (fp16DrawTarget) {
        _BindInternalDrawTarget(params);
    }

    // User is responsible for initializing GL context and glew
    bool isCoreProfileContext = GlfContextCaps::GetInstance().coreProfile;

    GLF_GROUP_FUNCTION();

    GLuint vao;
    if (isCoreProfileContext) {
        // We must bind a VAO (Vertex Array Object) because core profile 
        // contexts do not have a default vertex array object. VAO objects are 
        // container objects which are not shared between contexts, so we create
        // and bind a VAO here so that core rendering code does not have to 
        // explicitly manage per-GL context state.
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
    } else {
        glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // hydra orients all geometry during topological processing so that
    // front faces have ccw winding. We disable culling because culling
    // is handled by fragment shader discard.
    if (params.flipFrontFacing) {
        glFrontFace(GL_CW); // < State is pushed via GL_POLYGON_BIT
    } else {
        glFrontFace(GL_CCW); // < State is pushed via GL_POLYGON_BIT
    }
    glDisable(GL_CULL_FACE);

    if (params.applyRenderState) {
        glDisable(GL_BLEND);
    }

    // note: to get benefit of alpha-to-coverage, the target framebuffer
    // has to be a MSAA buffer.
    if (params.enableIdRender) {
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    } else if (params.enableSampleAlphaToCoverage) {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    // for points width
    glEnable(GL_PROGRAM_POINT_SIZE);

    // TODO:
    //  * forceRefresh
    //  * showGuides, showRender, showProxy
    //  * gammaCorrectColors

    _engine.Execute(_renderIndex, &tasks);

    if (isCoreProfileContext) {

        glBindVertexArray(0);
        // XXX: We should not delete the VAO on every draw call, but we 
        // currently must because it is GL Context state and we do not control 
        // the context.
        glDeleteVertexArrays(1, &vao);

    } else {
        glPopAttrib(); // GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT
    }

    if (fp16DrawTarget) {
        // Copy the results into the client applications framebuffer.
        _RestoreClientDrawTarget(params);
    }
}

bool 
UsdImagingGLEngine::_CanPrepareBatch(
    const UsdPrim& root, 
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();

    if (!TF_VERIFY(root, "Attempting to draw an invalid/null prim\n")) 
        return false;

    if (!root.GetPath().HasPrefix(_rootPath)) {
        TF_CODING_ERROR("Attempting to draw path <%s>, but engine is rooted"
                    "at <%s>\n",
                    root.GetPath().GetText(),
                    _rootPath.GetText());
        return false;
    }

    return true;
}

static int
_GetRefineLevel(float c)
{
    // TODO: Change complexity to refineLevel when we refactor UsdImaging.
    //
    // Convert complexity float to refine level int.
    int refineLevel = 0;

    // to avoid floating point inaccuracy (e.g. 1.3 > 1.3f)
    c = std::min(c + 0.01f, 2.0f);

    if (1.0f <= c && c < 1.1f) { 
        refineLevel = 0;
    } else if (1.1f <= c && c < 1.2f) { 
        refineLevel = 1;
    } else if (1.2f <= c && c < 1.3f) { 
        refineLevel = 2;
    } else if (1.3f <= c && c < 1.4f) { 
        refineLevel = 3;
    } else if (1.4f <= c && c < 1.5f) { 
        refineLevel = 4;
    } else if (1.5f <= c && c < 1.6f) { 
        refineLevel = 5;
    } else if (1.6f <= c && c < 1.7f) { 
        refineLevel = 6;
    } else if (1.7f <= c && c < 1.8f) { 
        refineLevel = 7;
    } else if (1.8f <= c && c <= 2.0f) { 
        refineLevel = 8;
    } else {
        TF_CODING_ERROR("Invalid complexity %f, expected range is [1.0,2.0]\n", 
                c);
    }
    return refineLevel;
}

void
UsdImagingGLEngine::_PreSetTime(const UsdPrim& root, 
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();

    // Set the fallback refine level, if this changes from the existing value,
    // all prim refine levels will be dirtied.
    int refineLevel = _GetRefineLevel(params.complexity);
    _delegate->SetRefineLevelFallback(refineLevel);

    // Apply any queued up scene edits.
    _delegate->ApplyPendingUpdates();
}

void
UsdImagingGLEngine::_PostSetTime(
    const UsdPrim& root, 
    const UsdImagingGLRenderParams& params)
{
    HD_TRACE_FUNCTION();
}


/* static */
bool
UsdImagingGLEngine::_UpdateHydraCollection(
    HdRprimCollection *collection,
    SdfPathVector const& roots,
    UsdImagingGLRenderParams const& params)
{
    if (collection == nullptr) {
        TF_CODING_ERROR("Null passed to _UpdateHydraCollection");
        return false;
    }

    // choose repr
    HdReprSelector reprSelector = HdReprSelector(HdReprTokens->smoothHull);
    bool refined = params.complexity > 1.0;
    
    if (params.drawMode == UsdImagingGLDrawMode::DRAW_POINTS) {
        reprSelector = HdReprSelector(HdReprTokens->points);
    } else if (params.drawMode == UsdImagingGLDrawMode::DRAW_GEOM_FLAT ||
        params.drawMode == UsdImagingGLDrawMode::DRAW_SHADED_FLAT) {
        // Flat shading
        reprSelector = HdReprSelector(HdReprTokens->hull);
    } else if (
        params.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE) {
        // Wireframe on surface
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refinedWireOnSurf : HdReprTokens->wireOnSurf);
    } else if (params.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME) {
        // Wireframe
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refinedWire : HdReprTokens->wire);
    } else {
        // Smooth shading
        reprSelector = HdReprSelector(refined ?
            HdReprTokens->refined : HdReprTokens->smoothHull);
    }

    // By default our main collection will be called geometry
    TfToken colName = HdTokens->geometry;

    // Check if the collection needs to be updated (so we can avoid the sort).
    SdfPathVector const& oldRoots = collection->GetRootPaths();

    // inexpensive comparison first
    bool match = collection->GetName() == colName &&
                 oldRoots.size() == roots.size() &&
                 collection->GetReprSelector() == reprSelector;

    // Only take the time to compare root paths if everything else matches.
    if (match) {
        // Note that oldRoots is guaranteed to be sorted.
        for(size_t i = 0; i < roots.size(); i++) {
            // Avoid binary search when both vectors are sorted.
            if (oldRoots[i] == roots[i])
                continue;
            // Binary search to find the current root.
            if (!std::binary_search(oldRoots.begin(), oldRoots.end(), roots[i])) 
            {
                match = false;
                break;
            }
        }

        // if everything matches, do nothing.
        if (match) return false;
    }

    // Recreate the collection.
    *collection = HdRprimCollection(colName, reprSelector);
    collection->SetRootPaths(roots);

    return true;
}

/* static */
HdxRenderTaskParams
UsdImagingGLEngine::_MakeHydraUsdImagingGLRenderParams(
    UsdImagingGLRenderParams const& renderParams)
{
    // Note this table is dangerous and making changes to the order of the 
    // enums in UsdImagingGLCullStyle, will affect this with no compiler help.
    static const HdCullStyle USD_2_HD_CULL_STYLE[] =
    {
        HdCullStyleDontCare,              // Cull No Opinion (unused)
        HdCullStyleNothing,               // CULL_STYLE_NOTHING,
        HdCullStyleBack,                  // CULL_STYLE_BACK,
        HdCullStyleFront,                 // CULL_STYLE_FRONT,
        HdCullStyleBackUnlessDoubleSided, // CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED
    };
    static_assert(((sizeof(USD_2_HD_CULL_STYLE) / 
                    sizeof(USD_2_HD_CULL_STYLE[0])) 
              == (size_t)UsdImagingGLCullStyle::CULL_STYLE_COUNT),
        "enum size mismatch");

    HdxRenderTaskParams params;

    params.overrideColor       = renderParams.overrideColor;
    params.wireframeColor      = renderParams.wireframeColor;

    if (renderParams.drawMode == UsdImagingGLDrawMode::DRAW_GEOM_ONLY ||
        renderParams.drawMode == UsdImagingGLDrawMode::DRAW_POINTS) {
        params.enableLighting = false;
    } else {
        params.enableLighting =  renderParams.enableLighting &&
                                !renderParams.enableIdRender;
    }

    params.enableIdRender      = renderParams.enableIdRender;
    params.depthBiasUseDefault = true;
    params.depthFunc           = HdCmpFuncLess;
    params.cullStyle           = USD_2_HD_CULL_STYLE[
        (size_t)renderParams.cullStyle];

    // Decrease the alpha threshold if we are using sample alpha to
    // coverage.
    if (renderParams.alphaThreshold < 0.0) {
        params.alphaThreshold =
            renderParams.enableSampleAlphaToCoverage ? 0.1f : 0.5f;
    } else {
        params.alphaThreshold =
            renderParams.alphaThreshold;
    }

    params.enableSceneMaterials = renderParams.enableSceneMaterials;

    // We don't provide the following because task controller ignores them:
    // - params.camera
    // - params.viewport

    return params;
}

//static
void
UsdImagingGLEngine::_ComputeRenderTags(UsdImagingGLRenderParams const& params,
                                       TfTokenVector *renderTags)
{
    // Calculate the rendertags needed based on the parameters passed by
    // the application
    renderTags->clear();
    renderTags->reserve(4);
    renderTags->push_back(HdTokens->geometry);
    if (params.showGuides) {
        renderTags->push_back(HdxRenderTagsTokens->guide);
    }
    if (params.showProxy) {
        renderTags->push_back(UsdGeomTokens->proxy);
    }
    if (params.showRender) {
        renderTags->push_back(UsdGeomTokens->render);
    }
}

void
UsdImagingGLEngine::_DeleteHydraResources()
{
    // Unwinding order: remove data sources first (task controller, scene
    // delegate); then render index; then render delegate; finally the
    // renderer plugin used to manage the render delegate.
    
    if (_taskController != nullptr) {
        delete _taskController;
        _taskController = nullptr;
    }
    if (_delegate != nullptr) {
        delete _delegate;
        _delegate = nullptr;
    }
    HdRenderDelegate *renderDelegate = nullptr;
    if (_renderIndex != nullptr) {
        renderDelegate = _renderIndex->GetRenderDelegate();
        delete _renderIndex;
        _renderIndex = nullptr;
    }
    if (_rendererPlugin != nullptr) {
        if (renderDelegate != nullptr) {
            _rendererPlugin->DeleteRenderDelegate(renderDelegate);
        }
        HdxRendererPluginRegistry::GetInstance().ReleasePlugin(_rendererPlugin);
        _rendererPlugin = nullptr;
        _rendererId = TfToken();
    }
}

/* static */
TfToken
UsdImagingGLEngine::_GetDefaultRendererPluginId()
{
    std::string defaultRendererDisplayName = 
        TfGetenv("HD_DEFAULT_RENDERER", "");

    if (defaultRendererDisplayName.empty()) {
        return TfToken();
    }

    HfPluginDescVector pluginDescs;
    HdxRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescs);

    // Look for the one with the matching display name
    for (size_t i = 0; i < pluginDescs.size(); ++i) {
        if (pluginDescs[i].displayName == defaultRendererDisplayName) {
            return pluginDescs[i].id;
        }
    }

    TF_WARN("Failed to find default renderer with display name '%s'.",
            defaultRendererDisplayName.c_str());

    return TfToken();
}

void 
UsdImagingGLEngine::_BindInternalDrawTarget(
    UsdImagingGLRenderParams const& params)
{
    // Avoid GL errors in MacOS (e.g. Hydra with Embree)
    if (!GlfContextCaps::GetInstance().floatingPointBuffersEnabled) {
        return;
    }

    if (!_useFloatPointDrawTarget) {
        return;
    }

    glGetIntegerv(GL_VIEWPORT, _restoreViewport.data());
    GfVec2i drawTargetSize = GfVec2i(_restoreViewport[2], _restoreViewport[3]);

    // Bind our internal drawtarget to control the render bitdepth.
    // We want to render (linear-colors), do color-correction and other post-
    // effects at a higher bitdepth then may be bound by the client.
    if(!_drawTarget) {
        bool requestMSAA = glIsEnabled(GL_MULTISAMPLE);
        _drawTarget = GlfDrawTarget::New(drawTargetSize, requestMSAA);
        _drawTarget->Bind();
        _drawTarget->AddAttachment("color", GL_RGBA, GL_FLOAT, GL_RGBA16F);
        _drawTarget->AddAttachment("depth", GL_DEPTH_COMPONENT, GL_FLOAT, 
                                   GL_DEPTH_COMPONENT32F);
    } else {
        _drawTarget->Bind();

        // The clients viewport may have changed size.
        _drawTarget->SetSize(drawTargetSize);
    }

    // Remove any offset applied to the viewport so clearing and rendering
    // happens in the correct region. (UsdView CameraMask mode messes with this)
    glViewport(0, 0, _restoreViewport[2], _restoreViewport[3]);

    // Clear our internal drawTarget with the clearcolor set by client
    glClearBufferfv(GL_COLOR, 0, params.clearColor.data());
    GLfloat clearDepth[1] = {1.0f};
    glClearBufferfv(GL_DEPTH, 0, clearDepth);
}

void 
UsdImagingGLEngine::_RestoreClientDrawTarget(
    UsdImagingGLRenderParams const& params)
{
    // Avoid GL errors in MacOS (e.g. Hydra with Embree)
    if (!GlfContextCaps::GetInstance().floatingPointBuffersEnabled) {
        return;
    }

    if (!_useFloatPointDrawTarget || !_drawTarget) {
        return;
    }

    // Restore the clients framebuffer and copy the contents of our internal
    // framebuffer into the clients framebuffer.
    // We need to blit depth because the client may do additional compositing
    // on top of the render, such as drawing bounding boxes.
    _drawTarget->Unbind();
    _drawTarget->Resolve();

    // Restore viewport set by client/app
    glViewport(_restoreViewport[0], 
               _restoreViewport[1], 
               _restoreViewport[2], 
               _restoreViewport[3]);

    // Depth test must always pass to ensure that all pixels are transfered
    // from our drawTarget to the clients when HdxCompositor draws its triangle.
    GLboolean restoreDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
    glEnable(GL_DEPTH_TEST);

    // Depth test must be ALWAYS instead of disabling the depth_test because
    // we still want to write to the depth buffer. Disabling depth_test disables
    // depth_buffer writes and we need to copy depth to client buffer.
    GLint restoreDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &restoreDepthFunc);
    glDepthFunc(GL_ALWAYS);

    // Any alpha blending the client wanted should have happened into our 
    // internal FB. When copying back to client buffer disable blending.
    GLboolean restoreblendEnabled;
    glGetBooleanv(GL_BLEND, &restoreblendEnabled);
    glDisable(GL_BLEND);

    GLuint colorId = _drawTarget->GetAttachment("color")->GetGlTextureName();
    GLuint depthId = _drawTarget->GetAttachment("depth")->GetGlTextureName();
    _compositor.Draw(colorId, depthId, /*remapDepth*/ false);

    if (restoreblendEnabled) {
        glEnable(GL_BLEND);
    }

    glDepthFunc(restoreDepthFunc);
    if (!restoreDepthEnabled) {
        glDisable(GL_DEPTH_TEST);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

