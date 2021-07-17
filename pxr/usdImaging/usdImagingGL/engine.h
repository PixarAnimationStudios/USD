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

/// \file usdImagingGL/engine.h

#ifndef PXR_USD_IMAGING_USD_IMAGING_GL_ENGINE_H
#define PXR_USD_IMAGING_USD_IMAGING_GL_ENGINE_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImagingGL/api.h"
#include "pxr/usdImaging/usdImagingGL/version.h"
#include "pxr/usdImaging/usdImaging/version.h"

#include "pxr/usdImaging/usdImagingGL/renderParams.h"
#include "pxr/usdImaging/usdImagingGL/rendererSettings.h"

#include "pxr/imaging/cameraUtil/conformWindow.h"

#include "pxr/imaging/hd/driver.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/rprimCollection.h"
#include "pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h"

#include "pxr/imaging/hdx/selectionTracker.h"
#include "pxr/imaging/hdx/renderSetupTask.h"

#include "pxr/imaging/hgi/hgi.h"

#include "pxr/imaging/glf/simpleLight.h"
#include "pxr/imaging/glf/simpleMaterial.h"

#include "pxr/imaging/hgi/hgi.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/timeCode.h"

#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec4i.h"

#include "pxr/base/vt/dictionary.h"

#include "pxr/base/tf/declarePtrs.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class HdRenderIndex;
class HdxTaskController;
class UsdImagingDelegate;
class UsdImagingGLLegacyEngine;

TF_DECLARE_WEAK_AND_REF_PTRS(GlfSimpleLightingContext);

/// \class UsdImagingGLEngine
///
/// The UsdImagingGLEngine is the main entry point API for rendering USD scenes.
///
class UsdImagingGLEngine
{
public:

    // ---------------------------------------------------------------------
    /// \name Global State
    /// @{
    // ---------------------------------------------------------------------

    /// Returns true if Hydra is enabled for GL drawing.
    USDIMAGINGGL_API
    static bool IsHydraEnabled();

    /// @}

    // ---------------------------------------------------------------------
    /// \name Construction
    /// @{
    // ---------------------------------------------------------------------

    /// A HdDriver, containing the Hgi of your choice, can be optionally passed
    /// in during construction. This can be helpful if you application creates
    /// multiple UsdImagingGLEngine that wish to use the same HdDriver / Hgi.
    USDIMAGINGGL_API
    UsdImagingGLEngine(const HdDriver& driver = HdDriver());

    USDIMAGINGGL_API
    UsdImagingGLEngine(const SdfPath& rootPath,
                       const SdfPathVector& excludedPaths,
                       const SdfPathVector& invisedPaths=SdfPathVector(),
                       const SdfPath& sceneDelegateID =
                                        SdfPath::AbsoluteRootPath(),
                       const HdDriver& driver = HdDriver());

    // Disallow copies
    UsdImagingGLEngine(const UsdImagingGLEngine&) = delete;
    UsdImagingGLEngine& operator=(const UsdImagingGLEngine&) = delete;

    USDIMAGINGGL_API
    ~UsdImagingGLEngine();

    /// @}

    // ---------------------------------------------------------------------
    /// \name Rendering
    /// @{
    // ---------------------------------------------------------------------

    /// Support for batched drawing
    USDIMAGINGGL_API
    void PrepareBatch(const UsdPrim& root, 
                      const UsdImagingGLRenderParams& params);
    USDIMAGINGGL_API
    void RenderBatch(const SdfPathVector& paths, 
                     const UsdImagingGLRenderParams& params);

    /// Entry point for kicking off a render
    USDIMAGINGGL_API
    void Render(const UsdPrim& root, 
                const UsdImagingGLRenderParams &params);

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    USDIMAGINGGL_API
    bool IsConverged() const;

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Root Transform and Visibility
    /// @{
    // ---------------------------------------------------------------------

    /// Sets the root transform.
    USDIMAGINGGL_API
    void SetRootTransform(GfMatrix4d const& xf);

    /// Sets the root visibility.
    USDIMAGINGGL_API
    void SetRootVisibility(bool isVisible);

    /// @}

    // ---------------------------------------------------------------------
    /// \name Camera State
    /// @{
    // ---------------------------------------------------------------------
    
    /// Scene camera API
    /// Set the scene camera path to use for rendering.
    USDIMAGINGGL_API
    void SetCameraPath(SdfPath const& id);

    /// Determines how the filmback of the camera is mapped into
    /// the pixels of the render buffer and what pixels of the render
    /// buffer will be rendered into.
    USDIMAGINGGL_API
    void SetFraming(CameraUtilFraming const& framing);

    /// Specifies whether to force a window policy when conforming
    /// the frustum of the camera to match the display window of
    /// the camera framing.
    ///
    /// If set to {false, ...}, the window policy of the specified camera
    /// will be used.
    ///
    /// Note: std::pair<bool, ...> is used instead of std::optional<...>
    /// because the latter is only available in C++17 or later.
    USDIMAGINGGL_API
    void SetOverrideWindowPolicy(
        const std::pair<bool, CameraUtilConformWindowPolicy> &policy);

    /// Set the size of the render buffers baking the AOVs.
    /// GUI applications should set this to the size of the window.
    ///
    USDIMAGINGGL_API
    void SetRenderBufferSize(GfVec2i const& size);

    /// Set the viewport to use for rendering as (x,y,w,h), where (x,y)
    /// represents the lower left corner of the viewport rectangle, and (w,h)
    /// is the width and height of the viewport in pixels.
    ///
    /// \deprecated Use SetFraming and SetRenderBufferSize instead.
    USDIMAGINGGL_API
    void SetRenderViewport(GfVec4d const& viewport);

    /// Set the window policy to use.
    /// XXX: This is currently used for scene cameras set via SetCameraPath.
    /// See comment in SetCameraState for the free cam.
    USDIMAGINGGL_API
    void SetWindowPolicy(CameraUtilConformWindowPolicy policy);

    /// Free camera API
    /// Set camera framing state directly (without pointing to a camera on the 
    /// USD stage). The projection matrix is expected to be pre-adjusted for the
    /// window policy.
    USDIMAGINGGL_API
    void SetCameraState(const GfMatrix4d& viewMatrix,
                        const GfMatrix4d& projectionMatrix);

    /// Helper function to extract camera and viewport state from opengl and
    /// then call SetCameraState and SetRenderViewport
    USDIMAGINGGL_API
    void SetCameraStateFromOpenGL();

    /// @}

    // ---------------------------------------------------------------------
    /// \name Light State
    /// @{
    // ---------------------------------------------------------------------
    
    /// Helper function to extract lighting state from opengl and then
    /// call SetLights.
    USDIMAGINGGL_API
    void SetLightingStateFromOpenGL();

    /// Copy lighting state from another lighting context.
    USDIMAGINGGL_API
    void SetLightingState(GlfSimpleLightingContextPtr const &src);

    /// Set lighting state
    /// Derived classes should ensure that passing an empty lights
    /// vector disables lighting.
    /// \param lights is the set of lights to use, or empty to disable lighting.
    USDIMAGINGGL_API
    void SetLightingState(GlfSimpleLightVector const &lights,
                          GlfSimpleMaterial const &material,
                          GfVec4f const &sceneAmbient);

    /// @}

    // ---------------------------------------------------------------------
    /// \name Selection Highlighting
    /// @{
    // ---------------------------------------------------------------------

    /// Sets (replaces) the list of prim paths that should be included in 
    /// selection highlighting. These paths may include root paths which will 
    /// be expanded internally.
    USDIMAGINGGL_API
    void SetSelected(SdfPathVector const& paths);

    /// Clear the list of prim paths that should be included in selection
    /// highlighting.
    USDIMAGINGGL_API
    void ClearSelected();

    /// Add a path with instanceIndex to the list of prim paths that should be
    /// included in selection highlighting. UsdImagingDelegate::ALL_INSTANCES
    /// can be used for highlighting all instances if path is an instancer.
    USDIMAGINGGL_API
    void AddSelected(SdfPath const &path, int instanceIndex);

    /// Sets the selection highlighting color.
    USDIMAGINGGL_API
    void SetSelectionColor(GfVec4f const& color);

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Picking
    /// @{
    // ---------------------------------------------------------------------
    
    /// Finds closest point of intersection with a frustum by rendering.
    ///	
    /// This method uses a PickRender and a customized depth buffer to find an
    /// approximate point of intersection by rendering. This is less accurate
    /// than implicit methods or rendering with GL_SELECT, but leverages any 
    /// data already cached in the renderer.
    ///
    /// Returns whether a hit occurred and if so, \p outHitPoint will contain
    /// the intersection point in world space (i.e. \p projectionMatrix and
    /// \p viewMatrix factored back out of the result), and \p outHitNormal
    /// will contain the world space normal at that point.
    ///
    /// \p outHitPrimPath will point to the gprim selected by the pick.
    /// \p outHitInstancerPath will point to the point instancer (if applicable)
    /// of that gprim. For nested instancing, outHitInstancerPath points to
    /// the closest instancer.
    ///
    USDIMAGINGGL_API
    bool TestIntersection(
        const GfMatrix4d &viewMatrix,
        const GfMatrix4d &projectionMatrix,
        const UsdPrim& root,
        const UsdImagingGLRenderParams &params,
        GfVec3d *outHitPoint,
        GfVec3d *outHitNormal,
        SdfPath *outHitPrimPath = NULL,
        SdfPath *outHitInstancerPath = NULL,
        int *outHitInstanceIndex = NULL,
        HdInstancerContext *outInstancerContext = NULL);

    /// Decodes a pick result given hydra prim ID/instance ID (like you'd get
    /// from an ID render).
    USDIMAGINGGL_API
    bool DecodeIntersection(
        unsigned char const primIdColor[4],
        unsigned char const instanceIdColor[4],
        SdfPath *outHitPrimPath = NULL,
        SdfPath *outHitInstancerPath = NULL,
        int *outHitInstanceIndex = NULL,
        HdInstancerContext *outInstancerContext = NULL);

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Renderer Plugin Management
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available render-graph delegate plugins.
    USDIMAGINGGL_API
    static TfTokenVector GetRendererPlugins();

    /// Return the user-friendly description of a renderer plugin.
    USDIMAGINGGL_API
    static std::string GetRendererDisplayName(TfToken const &id);

    /// Return the id of the currently used renderer plugin.
    USDIMAGINGGL_API
    TfToken GetCurrentRendererId() const;

    /// Set the current render-graph delegate to \p id.
    /// the plugin will be loaded if it's not yet.
    USDIMAGINGGL_API
    bool SetRendererPlugin(TfToken const &id);

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------

    /// Return the vector of available renderer AOV settings.
    USDIMAGINGGL_API
    TfTokenVector GetRendererAovs() const;

    /// Set the current renderer AOV to \p id.
    USDIMAGINGGL_API
    bool SetRendererAov(TfToken const& id);

    /// Returns an AOV texture handle for the given token.
    USDIMAGINGGL_API
    HgiTextureHandle GetAovTexture(TfToken const& name) const;

    /// Returns the list of renderer settings.
    USDIMAGINGGL_API
    UsdImagingGLRendererSettingsList GetRendererSettingsList() const;

    /// Gets a renderer setting's current value.
    USDIMAGINGGL_API
    VtValue GetRendererSetting(TfToken const& id) const;

    /// Sets a renderer setting's value.
    USDIMAGINGGL_API
    void SetRendererSetting(TfToken const& id,
                            VtValue const& value);

    /// Enable / disable presenting the render to bound framebuffer.
    /// An application may choose to manage the AOVs that are rendered into
    /// itself and skip the engine's presentation.
    USDIMAGINGGL_API
    void SetEnablePresentation(bool enabled);

    /// The destination API (e.g., OpenGL, see hgiInterop for details) and
    /// framebuffer that the AOVs are presented into. The framebuffer
    /// is a VtValue that encoding a framebuffer in a destination API
    /// specific way.
    /// E.g., a uint32_t (aka GLuint) for framebuffer object for OpenGL.
    USDIMAGINGGL_API
    void SetPresentationOutput(TfToken const &api, VtValue const &framebuffer);

    /// @}
    
    // ---------------------------------------------------------------------
    /// \name Renderer Command API
    /// @{
    // ---------------------------------------------------------------------

    /// Return command deescriptors for commands supported by the active 
    /// render delegate.
    ///
    USDIMAGINGGL_API
    HdCommandDescriptors GetRendererCommandDescriptors() const;

    /// Invokes command on the active render delegate. If successful, returns
    /// \c true, returns \c false otherwise. Note that the command will not
    /// succeeed if it is not among those returned by
    /// GetRendererCommandDescriptors() for the same active render delegate.
    ///
    USDIMAGINGGL_API
    bool InvokeRendererCommand(
            const TfToken &command, 
            const HdCommandArgs &args = HdCommandArgs()) const;

    // ---------------------------------------------------------------------
    /// \name Control of background rendering threads.
    /// @{
    // ---------------------------------------------------------------------

    /// Query the renderer as to whether it supports pausing and resuming.
    USDIMAGINGGL_API
    bool IsPauseRendererSupported() const;

    /// Pause the renderer.
    ///
    /// Returns \c true if successful.
    USDIMAGINGGL_API
    bool PauseRenderer();

    /// Resume the renderer.
    ///
    /// Returns \c true if successful.
    USDIMAGINGGL_API
    bool ResumeRenderer();

    /// Query the renderer as to whether it supports stopping and restarting.
    USDIMAGINGGL_API
    bool IsStopRendererSupported() const;

    /// Stop the renderer.
    ///
    /// Returns \c true if successful.
    USDIMAGINGGL_API
    bool StopRenderer();

    /// Restart the renderer.
    ///
    /// Returns \c true if successful.
    USDIMAGINGGL_API
    bool RestartRenderer();

    /// @}

    // ---------------------------------------------------------------------
    /// \name Color Correction
    /// @{
    // ---------------------------------------------------------------------

    /// Set \p id to one of the HdxColorCorrectionTokens.
    USDIMAGINGGL_API
    void SetColorCorrectionSettings(
        TfToken const& ccType,
        TfToken const& ocioDisplay = {},
        TfToken const& ocioView = {},
        TfToken const& ocioColorSpace = {},
        TfToken const& ocioLook = {});

    /// @}

    /// Returns true if the platform is color correction capable.
    USDIMAGINGGL_API
    static bool IsColorCorrectionCapable();

    // ---------------------------------------------------------------------
    /// \name Render Statistics
    /// @{
    // ---------------------------------------------------------------------

    /// Returns render statistics.
    ///
    /// The contents of the dictionary will depend on the current render 
    /// delegate.
    ///
    USDIMAGINGGL_API
    VtDictionary GetRenderStats() const;

    /// @}

    // ---------------------------------------------------------------------
    /// \name HGI
    /// @{
    // ---------------------------------------------------------------------

    /// Returns the HGI interface.
    ///
    USDIMAGINGGL_API
    Hgi* GetHgi();

    /// @}
    
protected:

    /// Open some protected methods for whitebox testing.
    friend class UsdImagingGL_UnitTestGLDrawing;

    /// Returns the render index of the engine, if any.  This is only used for
    /// whitebox testing.
    USDIMAGINGGL_API
    HdRenderIndex *_GetRenderIndex() const;

    USDIMAGINGGL_API
    void _Execute(const UsdImagingGLRenderParams &params,
                  HdTaskSharedPtrVector tasks);

    USDIMAGINGGL_API
    bool _CanPrepare(const UsdPrim& root);
    USDIMAGINGGL_API
    void _PreSetTime(const UsdImagingGLRenderParams& params);
    USDIMAGINGGL_API
    void _PostSetTime(const UsdImagingGLRenderParams& params);

    USDIMAGINGGL_API
    void _PrepareRender(const UsdImagingGLRenderParams& params);

    // Create a hydra collection given root paths and render params.
    // Returns true if the collection was updated.
    USDIMAGINGGL_API
    static bool _UpdateHydraCollection(HdRprimCollection *collection,
                          SdfPathVector const& roots,
                          UsdImagingGLRenderParams const& params);
    USDIMAGINGGL_API
    static HdxRenderTaskParams _MakeHydraUsdImagingGLRenderParams(
                          UsdImagingGLRenderParams const& params);
    USDIMAGINGGL_API
    static void _ComputeRenderTags(UsdImagingGLRenderParams const& params,
                          TfTokenVector *renderTags);

    USDIMAGINGGL_API
    void _InitializeHgiIfNecessary();

    USDIMAGINGGL_API
    void _SetRenderDelegateAndRestoreState(
        HdPluginRenderDelegateUniqueHandle &&);

    USDIMAGINGGL_API
    void _SetRenderDelegate(HdPluginRenderDelegateUniqueHandle &&);

    USDIMAGINGGL_API
    SdfPath _ComputeControllerPath(const HdPluginRenderDelegateUniqueHandle &);

    USDIMAGINGGL_API
    static TfToken _GetDefaultRendererPluginId();

    USDIMAGINGGL_API
    UsdImagingDelegate *_GetSceneDelegate() const;

    USDIMAGINGGL_API
    HdEngine *_GetHdEngine();

    USDIMAGINGGL_API
    HdxTaskController *_GetTaskController() const;

    USDIMAGINGGL_API
    bool _IsUsingLegacyImpl() const;

    USDIMAGINGGL_API
    HdSelectionSharedPtr _GetSelection() const;

protected:

// private:
    // Note that any of the fields below might become private
    // in the future and subclasses should use the above getters
    // to access them instead.

    HgiUniquePtr _hgi;
    // Similar for HdDriver.
    HdDriver _hgiDriver;

    VtValue _userFramebuffer;

protected:
    HdPluginRenderDelegateUniqueHandle _renderDelegate;
    std::unique_ptr<HdRenderIndex> _renderIndex;

    SdfPath const _sceneDelegateId;

    std::unique_ptr<HdxTaskController> _taskController;

    HdxSelectionTrackerSharedPtr _selTracker;
    HdRprimCollection _renderCollection;
    HdRprimCollection _intersectCollection;

    GlfSimpleLightingContextRefPtr _lightingContextForOpenGLState;

    // Data we want to live across render plugin switches:
    GfVec4f _selectionColor;

    SdfPath _rootPath;
    SdfPathVector _excludedPrimPaths;
    SdfPathVector _invisedPrimPaths;
    bool _isPopulated;

    // An implementation of much of the engine functionality that doesn't
    // invoke any of the advanced Hydra features.  It is kept around for 
    // backwards compatibility and may one day be deprecated.  Most of the 
    // time we expect this to be null.  When it is not null, none of the other
    // member variables of this class are used.
    std::unique_ptr<UsdImagingGLLegacyEngine> _legacyImpl;

private:
    void _DestroyHydraObjects();

    std::unique_ptr<UsdImagingDelegate> _sceneDelegate;
    std::unique_ptr<HdEngine> _engine;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_IMAGING_USD_IMAGING_GL_ENGINE_H
