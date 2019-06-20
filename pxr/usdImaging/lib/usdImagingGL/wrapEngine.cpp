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
#include <boost/python/class.hpp>
#include <boost/python/def.hpp>
#include <boost/python/tuple.hpp>
#include <boost/python.hpp>
#include <boost/python/converter/from_python.hpp>

#include "pxr/usdImaging/usdImagingGL/engine.h"

#include "pxr/usdImaging/usdImaging/delegate.h"

#include "pxr/imaging/hdx/rendererPluginRegistry.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyEnum.h"
#include "pxr/base/tf/pyResultConversions.h"

using namespace std;
using namespace boost::python;
using namespace boost;

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

static boost::python::tuple
_TestIntersection(
    UsdImagingGLEngine & self, 
    const GfMatrix4d &viewMatrix,
    const GfMatrix4d &projectionMatrix,
    const GfMatrix4d &worldToLocalSpace,
    const UsdPrim& root, 
    UsdImagingGLRenderParams params)
{
    GfVec3d hitPoint;
    SdfPath hitPrimPath;
    SdfPath hitInstancerPath;
    int hitInstanceIndex;
    int hitElementIndex;

    self.TestIntersection(
        viewMatrix,
        projectionMatrix,
        worldToLocalSpace,
        root,
        params,
        &hitPoint,
        &hitPrimPath,
        &hitInstancerPath,
        &hitInstanceIndex,
        &hitElementIndex);

    return boost::python::make_tuple(hitPoint, hitPrimPath, hitInstancerPath, hitInstanceIndex, hitElementIndex);
}

static boost::python::tuple
_GetPrimPathFromInstanceIndex(
    UsdImagingGLEngine & self,
    const SdfPath& protoPrimPath,
    int instanceIndex)
{
    int absoluteInstanceIndex = 0;
    SdfPath path = self.GetPrimPathFromInstanceIndex(protoPrimPath,
                                                     instanceIndex,
                                                     &absoluteInstanceIndex);
    return boost::python::make_tuple(path, absoluteInstanceIndex);
}

static void
_SetLightingState(UsdImagingGLEngine &self, GlfSimpleLightVector const &lights,
                  GlfSimpleMaterial const &material, GfVec4f const &sceneAmbient)
{
    self.SetLightingState(lights, material, sceneAmbient);
}

} // anonymous namespace 

void wrapEngine()
{
    { 
        scope engineScope = class_<UsdImagingGLEngine, boost::noncopyable>(
                "Engine", "UsdImaging Renderer class")
            .def( init<>() )
            .def( init<const SdfPath &, const SdfPathVector&,
                    const SdfPathVector&, const SdfPath & >
                ((args("rootPath"),
                  args("excludedPaths") = SdfPathVector(),
                  args("invisedPaths") = SdfPathVector(),
                  args("delegateID") = SdfPath::AbsoluteRootPath())))
            .def("Render", &UsdImagingGLEngine::Render)
            .def("SetCameraState", &UsdImagingGLEngine::SetCameraState)
            .def("SetLightingStateFromOpenGL",
                    &UsdImagingGLEngine::SetLightingStateFromOpenGL)
            .def("SetLightingState", &_SetLightingState)
            .def("SetCameraStateFromOpenGL", 
                    &UsdImagingGLEngine::SetCameraStateFromOpenGL)
            .def("SetSelected", &UsdImagingGLEngine::SetSelected)
            .def("ClearSelected", &UsdImagingGLEngine::ClearSelected)
            .def("AddSelected", &UsdImagingGLEngine::AddSelected)
            .def("SetSelectionColor", &UsdImagingGLEngine::SetSelectionColor)
            .def("GetRprimPathFromPrimId", 
                    &UsdImagingGLEngine::GetRprimPathFromPrimId)
            .def("GetPrimPathFromInstanceIndex", &_GetPrimPathFromInstanceIndex)
            .def("ConvertCachePathToIndexPath",
                    &UsdImagingGLEngine::ConvertCachePathToIndexPath)
            .def("ConvertIndexPathToCachePath",
                    &UsdImagingGLEngine::ConvertIndexPathToCachePath)
            .def("TestIntersection", &_TestIntersection)
            .def("IsHydraEnabled", &UsdImagingGLEngine::IsHydraEnabled)
                .staticmethod("IsHydraEnabled")
            .def("IsConverged", &UsdImagingGLEngine::IsConverged)
            .def("GetRendererPlugins", &UsdImagingGLEngine::GetRendererPlugins,
                 return_value_policy< TfPySequenceToList >())
                .staticmethod("GetRendererPlugins")
            .def("GetRendererDisplayName", 
                    &UsdImagingGLEngine::GetRendererDisplayName)
                .staticmethod("GetRendererDisplayName")
            .def("GetCurrentRendererId", 
                    &UsdImagingGLEngine::GetCurrentRendererId)
            .def("SetRendererPlugin", 
                    &UsdImagingGLEngine::SetRendererPlugin)
            .def("GetRendererAovs", 
                    &UsdImagingGLEngine::GetRendererAovs,
                 return_value_policy< TfPySequenceToList >())
            .def("SetRendererAov", 
                    &UsdImagingGLEngine::SetRendererAov)
            .def("GetResourceAllocation", 
                    &UsdImagingGLEngine::GetResourceAllocation)
            .def("GetRendererSettingsList", 
                    &UsdImagingGLEngine::GetRendererSettingsList,
                 return_value_policy< TfPySequenceToList >())
            .def("GetRendererSetting", &UsdImagingGLEngine::GetRendererSetting)
            .def("SetRendererSetting", &UsdImagingGLEngine::SetRendererSetting)
            .def("SetEnableFloatPointDrawTarget", 
                    &UsdImagingGLEngine::SetEnableFloatPointDrawTarget)
            .def("SetColorCorrectionSettings", 
                    &UsdImagingGLEngine::SetColorCorrectionSettings)
            .def("IsColorCorrectionCapable", 
                &UsdImagingGLEngine::IsColorCorrectionCapable)
                .staticmethod("IsColorCorrectionCapable")
        ;

    }

    // Wrap the constants.
    scope().attr("ALL_INSTANCES") = UsdImagingDelegate::ALL_INSTANCES;

    TfPyContainerConversions::from_python_sequence<
        std::vector<GlfSimpleLight>, 
        TfPyContainerConversions::variable_capacity_policy>();
}
