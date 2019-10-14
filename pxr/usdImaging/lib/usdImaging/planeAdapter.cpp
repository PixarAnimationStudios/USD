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
#include "pxr/usdImaging/usdImaging/planeAdapter.h"

#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"

#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/pxOsd/tokens.h"

#include "pxr/usd/usdGeom/plane.h"
#include "pxr/usd/usdGeom/xformCache.h"

#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE


TF_REGISTRY_FUNCTION(TfType)
{
    typedef UsdImagingPlaneAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

UsdImagingPlaneAdapter::~UsdImagingPlaneAdapter() 
{
}

bool
UsdImagingPlaneAdapter::IsSupported(UsdImagingIndexProxy const* index) const
{
    return index->IsRprimTypeSupported(HdPrimTypeTokens->mesh);
}

SdfPath
UsdImagingPlaneAdapter::Populate(UsdPrim const& prim, 
                            UsdImagingIndexProxy* index,
                            UsdImagingInstancerContext const* instancerContext)
{
    return _AddRprim(HdPrimTypeTokens->mesh,
                     prim, index, GetMaterialId(prim), instancerContext);
}

void 
UsdImagingPlaneAdapter::TrackVariability(UsdPrim const& prim,
                                        SdfPath const& cachePath,
                                        HdDirtyBits* timeVaryingBits,
                                        UsdImagingInstancerContext const* 
                                            instancerContext) const
{
    BaseAdapter::TrackVariability(
        prim, cachePath, timeVaryingBits, instancerContext);
    // WARNING: This method is executed from multiple threads, the value cache
    // has been carefully pre-populated to avoid mutating the underlying
    // container during update.
    
    // The base adapter may already be setting that transform dirty bit.
    // _IsVarying will clear it, so check it isn't already marked as
    // varying before checking for additional set cases.
     if ((*timeVaryingBits & HdChangeTracker::DirtyTransform) == 0) {
        _IsVarying(prim, UsdGeomTokens->size,
                      HdChangeTracker::DirtyTransform,
                      UsdImagingTokens->usdVaryingXform,
                      timeVaryingBits, /*inherited*/false);
    }
}

void 
UsdImagingPlaneAdapter::UpdateForTime(UsdPrim const& prim,
                                     SdfPath const& cachePath, 
                                     UsdTimeCode time,
                                     HdDirtyBits requestedBits,
                                     UsdImagingInstancerContext const* 
                                         instancerContext) const
{
    BaseAdapter::UpdateForTime(
        prim, cachePath, time, requestedBits, instancerContext);

    UsdImagingValueCache* valueCache = _GetValueCache();

    if (requestedBits & HdChangeTracker::DirtyTopology) {
        valueCache->GetTopology(cachePath) = GetMeshTopology();
    }

    if (_IsRefined(cachePath)) {
        if (requestedBits & HdChangeTracker::DirtySubdivTags) {
            valueCache->GetSubdivTags(cachePath);
        }
    }
}

/*virtual*/
VtValue
UsdImagingPlaneAdapter::GetPoints(UsdPrim const& prim,
                                     SdfPath const& cachePath,
                                     UsdTimeCode time) const
{
    TF_UNUSED(cachePath);
    return GetMeshPoints(prim, time);   
}

// -------------------------------------------------------------------------- //

static VtVec3fArray
_GeneratePlaneMeshPoints(float width,
                        float length,
                        TfToken const & axis)
{
    int numPoints = 4;
    std::vector<GfVec3f> _points(numPoints);

    if (axis == UsdGeomTokens->x) {
        _points = { GfVec3f( 0.0f,  0.5f * length,-0.5f * width ),
                    GfVec3f( 0.0f,  0.5f * length, 0.5f * width ),
                    GfVec3f( 0.0f, -0.5f * length, 0.5f * width ),
                    GfVec3f( 0.0f, -0.5f * length,-0.5f * width ) };
    } else if (axis == UsdGeomTokens->y) {
        _points = { 
                    GfVec3f( 0.5f * width, 0.0f,-0.5f * length ),
                    GfVec3f(-0.5f * width, 0.0f,-0.5f * length ),
                    GfVec3f(-0.5f * width, 0.0f, 0.5f * length ),
                    GfVec3f( 0.5f * width, 0.0f, 0.5f * length ) };
    } else {
        _points = { GfVec3f( 0.5f * width, 0.5f * length, 0.0f ),
                    GfVec3f(-0.5f * width, 0.5f * length, 0.0f ),
                    GfVec3f(-0.5f * width,-0.5f * length, 0.0f ),
                    GfVec3f( 0.5f * width,-0.5f * length, 0.0f ) };
    }

    VtVec3fArray pointsArray(numPoints);
    GfVec3f * p = pointsArray.data();

    for (int i=0; i<numPoints; ++i) {
        *p++ = _points[i];
    }

    TF_VERIFY(p - pointsArray.data() == numPoints);

    return pointsArray;
}

/*static*/
VtValue
UsdImagingPlaneAdapter::GetMeshPoints(UsdPrim const& prim, 
                                     UsdTimeCode time)
{
    UsdGeomPlane plane(prim);
    double width = 2.0;
    double length = 2.0;
    TfToken axis = UsdGeomTokens->z;
    TF_VERIFY(plane.GetWidthAttr().Get(&width, time));
    TF_VERIFY(plane.GetLengthAttr().Get(&length, time));
    TF_VERIFY(plane.GetAxisAttr().Get(&axis, time));

    return VtValue(_GeneratePlaneMeshPoints(float(width),
                                            float(length),
                                            axis)); 
}

template <typename T>
static VtArray<T>
_BuildVtArray(T values[], int numValues)
{
    VtArray<T> result(numValues);
    std::copy(values, values+numValues, result.begin());
    return result;
}

/*static*/
VtValue
UsdImagingPlaneAdapter::GetMeshTopology()
{
    static int numVerts[] = { 4 };
    static int verts[] = {
        0, 1, 2, 3,
    };
    static HdMeshTopology planeTopo(PxOsdOpenSubdivTokens->bilinear,
               HdTokens->rightHanded,
               _BuildVtArray(numVerts, sizeof(numVerts) / sizeof(numVerts[0])),
               _BuildVtArray(verts, sizeof(verts) / sizeof(verts[0])));
    return VtValue(planeTopo);
}

PXR_NAMESPACE_CLOSE_SCOPE

