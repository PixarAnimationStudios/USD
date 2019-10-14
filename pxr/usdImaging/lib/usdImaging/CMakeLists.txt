set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdImaging)

pxr_library(usdImaging
    LIBRARIES
        gf
        tf
        plug
        trace
        vt
        work
        hd
        pxOsd
        sdf
        usd
        usdGeom
        usdLux
        usdShade
        usdVol
        ar
        ${TBB_tbb_LIBRARY}

    INCLUDE_DIRS
        ${TBB_INCLUDE_DIRS}

    PUBLIC_CLASSES
        adapterRegistry
        collectionCache
        debugCodes
        delegate
        indexProxy
        inheritedCache
        instancerContext
        tokens
        valueCache
        basisCurvesAdapter
        capsuleAdapter
        cameraAdapter
        coneAdapter
        coordSysAdapter
        cubeAdapter
        cylinderAdapter
        cylinderLightAdapter
        diskLightAdapter
        distantLightAdapter
        domeLightAdapter
        fieldAdapter
        gprimAdapter
        implicitSurfaceMeshUtils
        instanceAdapter
        lightAdapter
        materialAdapter
        meshAdapter
        nurbsCurvesAdapter
        nurbsPatchAdapter
        planeAdapter
        pointsAdapter
        pointInstancerAdapter
        primAdapter
        rectLightAdapter
        sphereAdapter
        sphereLightAdapter
        textureUtils
        volumeAdapter

    PUBLIC_HEADERS
        api.h
        unitTestHelper.h
        version.h

    RESOURCE_FILES
        plugInfo.json
)

