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

#ifndef USDIMAGINGGL_ENGINE_H
#define USDIMAGINGGL_ENGINE_H

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImagingGL/api.h"
#include "pxr/usdImaging/usdImaging/version.h"

#include "pxr/usdImaging/usdImagingGL/renderParams.h"
#include "pxr/usdImaging/usdImagingGL/rendererSettings.h"

#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/rprimCollection.h"

#include "pxr/imaging/hdx/compositor.h"
#include "pxr/imaging/hdx/selectionTracker.h"
#include "pxr/imaging/hdx/renderSetupTask.h"
#include "pxr/imaging/hdx/pickTask.h"

#include "pxr/imaging/glf/drawTarget.h"
#include "pxr/imaging/glf/simpleLight.h"
#include "pxr/imaging/glf/simpleMaterial.h"

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
class HdxRendererPlugin;
class HdxTaskController;
class UsdImagingDelegate;
class UsdImagingGLLegacyEngine;

typedef boost::shared_ptr<class GlfGLContext> GlfGLContextSharedPtr;
TF_DECLARE_WEAK_AND_REF_PTRS(GlfDrawTarget);
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
    USDIMAGINGGL_API
    UsdImagingGLEngine();

    USDIMAGINGGL_API
    UsdImagingGLEngine(const SdfPath& rootPath,
                       const SdfPathVector& excludedPaths,
                       const SdfPathVector& invisedPaths=SdfPathVector(),
                       const SdfPath& delegateID = SdfPath::AbsoluteRootPath());

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

    USDIMAGINGGL_API
    void InvalidateBuffers();

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
    /// \name Camera and Light State
    /// @{
    // ---------------------------------------------------------------------
    
    USDIMAGINGGL_API
    void SetCameraState(const GfMatrix4d& viewMatrix,
                                const GfMatrix4d& projectionMatrix,
                                const GfVec4d& viewport);

    /// Helper function to extract camera state from opengl and then
    /// call SetCameraState.
    USDIMAGINGGL_API
    void SetCameraStateFromOpenGL();

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
    /// \p viewMatrix factored back out of the result).
    ///
    USDIMAGINGGL_API
    bool TestIntersection(
        const GfMatrix4d &viewMatrix,
        const GfMatrix4d &projectionMatrix,
        const GfMatrix4d &worldToLocalSpace,
        const UsdPrim& root,
        const UsdImagingGLRenderParams& params,
        GfVec3d *outHitPoint,
        SdfPath *outHitPrimPath = NULL,
        SdfPath *outHitInstancerPath = NULL,
        int *outHitInstanceIndex = NULL,
        int *outHitElementIndex = NULL);

    /// Using an Id extracted from an Id render, returns the associated
    /// rprim path.
    ///
    /// Note that this function doesn't resolve instancer relationship.
    /// returning prim can be a prototype mesh which may not exist in usd stage.
    /// It can be resolved to the actual usd prim and corresponding instance
    /// index by GetPrimPathFromInstanceIndex().
    ///
    USDIMAGINGGL_API
    SdfPath GetRprimPathFromPrimId(int primId) const;

    /// Using colors extracted from an Id render, returns the associated
    /// prim path and optional instance index.
    ///
    /// Note that this function doesn't resolve instancer relationship.
    /// returning prim can be a prototype mesh which may not exist in usd stage.
    /// It can be resolved to the actual usd prim and corresponding instance
    /// index by GetPrimPathFromInstanceIndex().
    ///
    /// XXX: consider renaming to GetRprimPathFromPrimIdColor
    ///
    USDIMAGINGGL_API
    SdfPath GetPrimPathFromPrimIdColor(
        GfVec4i const & primIdColor,
        GfVec4i const & instanceIdColor,
        int * instanceIndexOut = NULL);

    /// Returns the path of the instance prim on the UsdStage being rendered
    /// by this engine that corresponds to the instance index generated by
    /// the specified prototype rprim.
    /// Returns an empty path if no such instance prim exists.
    ///
    /// absoluteInstanceIndex is also returned, which is an instance index
    /// of all instances in the instancer. Note that if the instancer instances
    /// heterogeneously, instanceIndex of the prototype rprim doesn't match
    /// the absoluteInstanceIndex in the instancer (see hd/sceneDelegate.h)
    /// 
    /// If \p instanceContext is not NULL, it is populated with the list of 
    /// instance roots that must be traversed to get to the rprim. The last prim
    /// in this vector is always the resolved (or forwarded) rprim.
    /// 
    USDIMAGINGGL_API
    SdfPath GetPrimPathFromInstanceIndex(
        SdfPath const& protoRprimPath,
        int instanceIndex,
        int *absoluteInstanceIndex=NULL,
        SdfPath * rprimPath=NULL,
        SdfPathVector *instanceContext=NULL);

    // Converts a cache path to a path in the render index.
    USDIMAGINGGL_API
    SdfPath ConvertCachePathToIndexPath(SdfPath const& cachePath);

    /// Convert the given Hydra ID to a UsdImaging cache path,
    /// by stripping the scene delegate prefix.
    ///
    /// The UsdImaging cache path is the same as a USD prim path,
    /// except for instanced prims, which get a name-mangled encoding.
    USDIMAGINGGL_API
    SdfPath ConvertIndexPathToCachePath(SdfPath const& indexPath);

    /// Resolves a 4-byte pixel from an id render to an int32 prim ID.
    static inline int DecodeIDRenderColor(unsigned char const idColor[4]) {
        return HdxPickTask::DecodeIDRenderColor(idColor);
    }

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

    /// (Optional) Enable the use of an (internal) 16F draw target.
    /// A 16F or higher framebuffer is needed when color correction is enabled.
    USDIMAGINGGL_API
    void SetEnableFloatPointDrawTarget(bool state);

    /// @}

    // ---------------------------------------------------------------------
    /// \name Color Correction
    /// @{
    // ---------------------------------------------------------------------

    /// Set \p id to one of the HdxColorCorrectionTokens.
    /// \p framebufferResolution should be the size of the bound framebuffer
    /// that will be color corrected. A 16F framebuffer should be bound when
    /// using color correction. See SetEnableFloatPointDrawTarget().
    USDIMAGINGGL_API
    void SetColorCorrectionSettings(
        TfToken const& id, 
        GfVec2i const& framebufferResolution);

    /// @}

    /// Returns true if the platform is color correction capable.
    USDIMAGINGGL_API
    static bool IsColorCorrectionCapable();

    // ---------------------------------------------------------------------
    /// \name Resource Information
    /// @{
    // ---------------------------------------------------------------------

    /// Returns GPU resource allocation info
    USDIMAGINGGL_API
    VtDictionary GetResourceAllocation() const;

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
                  bool fp16DrawTarget,
                  HdTaskSharedPtrVector tasks);

    // These functions factor batch preparation into separate steps so they
    // can be reused by both the vectorized and non-vectorized API.
    bool _CanPrepareBatch(const UsdPrim& root, 
        const UsdImagingGLRenderParams& params);
    void _PreSetTime(const UsdPrim& root, 
        const UsdImagingGLRenderParams& params);
    void _PostSetTime(const UsdPrim& root, 
        const UsdImagingGLRenderParams& params);

    // Create a hydra collection given root paths and render params.
    // Returns true if the collection was updated.
    USDIMAGINGGL_API
    static bool _UpdateHydraCollection(HdRprimCollection *collection,
                          SdfPathVector const& roots,
                          UsdImagingGLRenderParams const& params);
    static HdxRenderTaskParams _MakeHydraUsdImagingGLRenderParams(
                          UsdImagingGLRenderParams const& params);
    static void _ComputeRenderTags(UsdImagingGLRenderParams const& params,
                          TfTokenVector *renderTags);

    // This function disposes of: the render index, the render plugin,
    // the task controller, and the usd imaging delegate.
    void _DeleteHydraResources();

    static TfToken _GetDefaultRendererPluginId();

    // Creates and binds the internal draw-target that Hydra draws into.
    void _BindInternalDrawTarget(UsdImagingGLRenderParams const& params);

    // Restores clients framebuffer and copies our result into their framebuffer
    void _RestoreClientDrawTarget(UsdImagingGLRenderParams const& params);

    HdEngine _engine;

    HdRenderIndex *_renderIndex;

    HdxSelectionTrackerSharedPtr _selTracker;
    HdRprimCollection _renderCollection;
    HdRprimCollection _intersectCollection;

    SdfPath const _delegateID;
    UsdImagingDelegate *_delegate;

    HdxRendererPlugin *_rendererPlugin;
    TfToken _rendererId;
    HdxTaskController *_taskController;

    GlfSimpleLightingContextRefPtr _lightingContextForOpenGLState;

    // Data we want to live across render plugin switches:
    GfVec4f _selectionColor;

    SdfPath _rootPath;
    SdfPathVector _excludedPrimPaths;
    SdfPathVector _invisedPrimPaths;
    bool _isPopulated;

    GfVec4i _restoreViewport;
    bool _useFloatPointDrawTarget;
    HdxCompositor _compositor;
    GlfDrawTargetRefPtr _drawTarget;

    // An implementation of much of the engine functionality that doesn't
    // invoke any of the advanced Hydra features.  It is kept around for 
    // backwards compatibility and may one day be deprecated.  Most of the 
    // time we expect this to be null.  When it is not null, none of the other
    // member variables of this class are used.
    std::unique_ptr<UsdImagingGLLegacyEngine> _legacyImpl;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // USDIMAGINGGL_ENGINE_H
