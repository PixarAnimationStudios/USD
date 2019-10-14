set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdGeom)

pxr_library(usdGeom
    LIBRARIES
        js
        tf
        plug
        vt
        sdf
        trace
        usd
        work
        ${Boost_PYTHON_LIBRARY}
        ${TBB_tbb_LIBRARY}

    INCLUDE_DIRS
        ${Boost_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}

    PUBLIC_CLASSES
        debugCodes
        tokens
        bboxCache
        constraintTarget
        xformCache
        basisCurves
        boundable
        boundableComputeExtent
        camera
        capsule
        cone
        cube
        curves
        cylinder
        imageable
        gprim
        mesh
        metrics
        modelAPI
        motionAPI
        nurbsCurves
        nurbsPatch
        plane
        pointBased
        pointInstancer
        points
        primvar
        primvarsAPI
        scope
        sphere
        subset
        xform
        xformable
        xformOp
        xformCommonAPI
        
    PUBLIC_HEADERS
        api.h

    PYTHON_CPPFILES
        moduleDeps.cpp

    PRIVATE_CLASSES
        samplingUtils

    PYMODULE_CPPFILES
        module.cpp
        wrapBBoxCache.cpp
        wrapBasisCurves.cpp
        wrapBoundable.cpp
        wrapCamera.cpp
        wrapConstraintTarget.cpp
        wrapCapsule.cpp
        wrapCone.cpp
        wrapCube.cpp
        wrapCurves.cpp
        wrapCylinder.cpp
        wrapGprim.cpp
        wrapImageable.cpp
        wrapMesh.cpp
        wrapMetrics.cpp
        wrapModelAPI.cpp
        wrapMotionAPI.cpp
        wrapNurbsCurves.cpp
        wrapNurbsPatch.cpp
        wrapPlane.cpp
        wrapPointBased.cpp
        wrapPointInstancer.cpp
        wrapPoints.cpp
        wrapPrimvar.cpp
        wrapPrimvarsAPI.cpp
        wrapScope.cpp
        wrapSphere.cpp
        wrapSubset.cpp
        wrapTokens.cpp
        wrapXform.cpp
        wrapXformCache.cpp
        wrapXformable.cpp
        wrapXformOp.cpp
        wrapXformCommonAPI.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
        generatedSchema.usda
        schema.usda:usdGeom/schema.usda
)

pxr_test_scripts(
    testenv/testUsdGeomBasisCurves.py
    testenv/testUsdGeomBBoxCache.py
    testenv/testUsdGeomCamera.py
    testenv/testUsdGeomComputeAtTime.py
    testenv/testUsdGeomConstraintTarget.py
    testenv/testUsdGeomConsts.py
    testenv/testUsdGeomCurves.py
    testenv/testUsdGeomExtentTransform.py
    testenv/testUsdGeomMesh.py
    testenv/testUsdGeomMetrics.py
    testenv/testUsdGeomMotionAPI.py
    testenv/testUsdGeomNoPlugLoad.py
    testenv/testUsdGeomPointInstancer.py
    testenv/testUsdGeomPrimvar.py
    testenv/testUsdGeomPurposeVisibility.py
    testenv/testUsdGeomSchemata.py
    testenv/testUsdGeomSubset.py
    testenv/testUsdGeomXformable.py
    testenv/testUsdGeomXformCommonAPI.py
)

pxr_build_test(testUsdGeomXformCache
    LIBRARIES
        usd
        usdGeom
    CPPFILES
        testenv/testUsdGeomXformCache.cpp
)


pxr_build_test(testUsdGeomCreateAttribute
    LIBRARIES
        usd
        usdGeom
    CPPFILES
        testenv/testUsdGeomCreateAttribute.cpp
)

pxr_build_test(testUsdGeomIsA
    LIBRARIES
        usd
        usdGeom
    CPPFILES
        testenv/testUsdGeomIsA.cpp
)

pxr_build_test(testUsdGeomHasAPI
    LIBRARIES
        usd
        usdGeom
    CPPFILES
        testenv/testUsdGeomHasAPI.cpp
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomBasisCurves
    DEST testUsdGeomBasisCurves
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomBBoxCache
    DEST testUsdGeomBBoxCache
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomConsts
    DEST testUsdGeomConsts
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomPointInstancer
    DEST testUsdGeomPointInstancer
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomSubset
    DEST testUsdGeomSubset)

pxr_install_test_dir(
    SRC testenv/testUsdGeomBBoxCache
    DEST testUsdGeomBBoxCache
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomExtentTransform
    DEST testUsdGeomExtentTransform
)

pxr_install_test_dir(
    SRC testenv/testUsdGeomComputeAtTime
    DEST testUsdGeomComputeAtTime
)

pxr_register_test(testUsdGeomBasisCurves
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomBasisCurves"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomBBoxCache
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomBBoxCache"
    EXPECTED_RETURN_CODE 0
    ENV 
        TF_DEBUG=USDGEOM_BBOX
        USD_ASSIGN_MASTERS_DETERMINISTICALLY=1
)

pxr_register_test(testUsdGeomCamera
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomCamera"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomConstraintTarget
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomConstraintTarget"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomConsts
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomConsts"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomCreateAttribute
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomCreateAttribute"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomCurves
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomCurves"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomMesh
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomMesh"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomSubset
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomSubset"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomIsA
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomIsA"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomHasAPI
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomHasAPI"
    EXPECTED_RETURN_CODE 0
)

# This tests functionality when usdGeom has not been loaded.  That isn't
# possible with a monolithic library.
if(NOT PXR_BUILD_MONOLITHIC)
pxr_register_test(testUsdGeomNoPlugLoad
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomNoPlugLoad"
    EXPECTED_RETURN_CODE 0
)
endif()

pxr_register_test(testUsdGeomPrimvar
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomPrimvar"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomPointInstancer
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomPointInstancer"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomPurposeVisibility
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomPurposeVisibility"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomSchemata
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomSchemata"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomXformable
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomXformable"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomXformCache
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomXformCache"
    EXPECTED_RETURN_CODE 0 
)

pxr_register_test(testUsdGeomXformCommonAPI
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomXformCommonAPI"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomExtentTransform
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomExtentTransform"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdGeomComputeAtTime
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdGeomComputeAtTime"
    EXPECTED_RETURN_CODE 0
)
