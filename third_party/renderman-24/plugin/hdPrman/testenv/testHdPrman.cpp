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
#include "pxr/pxr.h"

#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/rprimCollection.h"
#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/cameraUtil/screenWindowParameters.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/usdLux/listAPI.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/xformCache.h"
#include "pxr/usd/usdRender/product.h"
#include "pxr/usd/usdRender/settings.h"
#include "pxr/usd/usdRender/spec.h"
#include "pxr/usd/usdRender/var.h"
#include "pxr/usdImaging/usdImaging/delegate.h"

#include "pxr/base/gf/camera.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/setenv.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stopwatch.h"
#include "pxr/base/trace/reporter.h"
#include "pxr/base/work/threadLimits.h"

#include "hdPrman/offlineContext.h"
#include "hdPrman/renderDelegate.h"
#include "hdPrman/rixStrings.h"

#include "RixShadingUtils.h"

#include <fstream>
#include <memory>
#include <stdio.h>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // Collection Names
    (testCollection)
);

 static const RtUString us_A("A");
 static const RtUString us_default("default");
 static const RtUString us_defaultColor("defaultColor");
 static const RtUString us_density("density");
 static const RtUString us_densityFloatPrimVar("densityFloatPrimVar");
 static const RtUString us_diffuseColor("diffuseColor");
 static const RtUString us_displayColor("displayColor");
 static const RtUString us_lightA("lightA");
 static const RtUString us_lightGroup("lightGroup");
 static const RtUString us_main_cam("main_cam");
 static const RtUString us_main_cam_projection("main_cam_projection");
 static const RtUString us_PathTracer("PathTracer");
 static const RtUString us_pv_color("pv_color");
 static const RtUString us_pv_color_resultRGB("pv_color:resultRGB");
 static const RtUString us_PxrDomeLight("PxrDomeLight");
 static const RtUString us_PxrPathTracer("PxrPathTracer");
 static const RtUString us_PxrPrimvar("PxrPrimvar");
 static const RtUString us_PxrSurface("PxrSurface");
 static const RtUString us_PxrVisualizer("PxrVisualizer");
 static const RtUString us_PxrVolume("PxrVolume");
 static const RtUString us_simpleTestSurface("simpleTestSurface");
 static const RtUString us_simpleVolume("simpleVolume");
 static const RtUString us_specularEdgeColor("specularEdgeColor");
 static const RtUString us_specularFaceColor("specularFaceColor");
 static const RtUString us_specularModelType("specularModelType");
 static const RtUString us_style("style");
 static const RtUString us_traceLightPaths("traceLightPaths");
 static const RtUString us_varname("varname");
 static const RtUString us_wireframe("wireframe");

static TfStopwatch timer_prmanRender;

// Simple Hydra task to Sync and Render the data provided to this test.
class Hd_DrawTask final : public HdTask
{
public:
    Hd_DrawTask(HdRenderPassSharedPtr const &renderPass,
                HdRenderPassStateSharedPtr const &renderPassState,
                TfTokenVector const &renderTags)
    : HdTask(SdfPath::EmptyPath())
    , _renderPass(renderPass)
    , _renderPassState(renderPassState)
    , _renderTags(renderTags)
    {
    }

    void Sync(HdSceneDelegate* delegate,
              HdTaskContext* ctx,
              HdDirtyBits* dirtyBits) override
    {
        _renderPass->Sync();
        *dirtyBits = HdChangeTracker::Clean;
    }

    void Prepare(HdTaskContext* ctx,
                 HdRenderIndex* renderIndex) override
    {
        _renderPassState->Prepare(renderIndex->GetResourceRegistry());
    }

    void Execute(HdTaskContext* ctx) override
    {
        timer_prmanRender.Start();
        _renderPass->Execute(_renderPassState, _renderTags);
        timer_prmanRender.Stop();
    }

    const TfTokenVector &GetRenderTags() const override
    {
        return _renderTags;
    }

private:
    HdRenderPassSharedPtr _renderPass;
    HdRenderPassStateSharedPtr _renderPassState;
    TfTokenVector _renderTags;
};

void
PrintUsage(const char* cmd, const char *err=nullptr)
{
    if (err) {
        fprintf(stderr, err);
    }
    fprintf(stderr, "Usage: %s INPUT.usd "
            "[--out OUTPUT] [--frame FRAME] [--freeCamProj CAM_PROJECTION] "
            "[--sceneCamPath CAM_PATH] [--settings RENDERSETTINGS_PATH] "
            "[--sceneCamAspect aspectRatio] "
            "[--visualize STYLE] [--perf PERF] [--trace TRACE]\n"
            "OUTPUT defaults to UsdRenderSettings if not specified.\n"
            "FRAME defaults to 0 if not specified.\n"
            "CAM_PROJECTION default to PxrPerspective if not specified\n"
            "CAM_PATH defaults to empty path if not specified\n"
            "RENDERSETTINGS_PATH defaults to empty path is not specified\n"
            "STYLE indicates a PxrVisualizer style to use instead of "
            "      the default integrator\n"
            "PERF indicates a json file to record performance measurements\n"
            "TRACE indicates a text file to record trace measurements\n",
            cmd);
}

////////////////////////////////////////////////////////////////////////

// Helper to convert a dictionary of Hydra settings to Riley params.
static void
_ConvertSettings(VtDictionary const& settings, RtParamList& params)
{
    for (auto const& entry: settings) {
        // Strip "ri:" namespace from USD.
        // Note that some Renderman options have their own "Ri:"
        // prefix, unrelated to USD, which we leave intact.
        RtUString riName;
        if (TfStringStartsWith(entry.first, "ri:")) {
            riName = RtUString(entry.first.c_str()+3);
        } else {
            riName = RtUString(entry.first.c_str());
        }
        if (entry.second.IsHolding<int>()) {
            params.SetInteger(riName, entry.second.UncheckedGet<int>());
        } else if (entry.second.IsHolding<float>()) {
            params.SetFloat(riName, entry.second.UncheckedGet<float>());
        } else if (entry.second.IsHolding<std::string>()) {
            params.SetString(riName,
                RtUString(entry.second.UncheckedGet<std::string>().c_str()));
        } else if (entry.second.IsHolding<VtArray<int>>()) {
            auto const& array = entry.second.UncheckedGet<VtArray<int>>();
            params.SetIntegerArray(riName, &array[0], array.size());
        } else if (entry.second.IsHolding<VtArray<float>>()) {
            auto const& array = entry.second.UncheckedGet<VtArray<float>>();
            params.SetFloatArray(riName, &array[0], array.size());
        } else {
            TF_CODING_ERROR("Unimplemented setting %s of type %s\n",
                            entry.first.c_str(),
                            entry.second.GetTypeName().c_str());
        }
    }
}

int main(int argc, char *argv[])
{
    // Pixar studio config
    TfRegistryManager::GetInstance().SubscribeTo<HdPrman_Context>();

    //////////////////////////////////////////////////////////////////////// 
    //
    // Parse args
    //
    if (argc < 2) {
        PrintUsage(argv[0]);
        return -1;
    }

    std::string inputFilename(argv[1]);
    std::string outputFilename;
    std::string perfOutput, traceOutput;

    int frameNum = 0;
    bool isOrthographic = false;
    std::string cameraProjection("PxrPerspective");
    static const std::string PxrOrthographic("PxrOrthographic");
    SdfPath sceneCamPath, renderSettingsPath;
    float sceneCamAspect = -1.0;
    std::string visualizerStyle;

    for (int i=2; i<argc-1; ++i) {
        if (std::string(argv[i]) == "--frame") {
            frameNum = atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--sceneCamPath") {
            sceneCamPath = SdfPath(argv[++i]);
        } else if (std::string(argv[i]) == "--sceneCamAspect") {
            sceneCamAspect = atof(argv[++i]);
        } else if (std::string(argv[i]) == "--freeCamProj") {
            cameraProjection = argv[++i];
            isOrthographic = cameraProjection == PxrOrthographic;
        } else if (std::string(argv[i]) == "--out") {
            outputFilename = argv[++i];
        } else if (std::string(argv[i]) == "--settings") {
            renderSettingsPath = SdfPath(argv[++i]);
        } else if (std::string(argv[i]) == "--visualize") {
            visualizerStyle = argv[++i];
        } else if (std::string(argv[i]) == "--perf") {
            perfOutput = argv[++i];
        } else if (std::string(argv[i]) == "--trace") {
            traceOutput = argv[++i];
        }
    }

    if (!traceOutput.empty()) {
        TraceCollector::GetInstance().SetEnabled(true);
    }

    //////////////////////////////////////////////////////////////////////// 
    //
    // USD setup
    //

    TfStopwatch timer_usdOpen;
    timer_usdOpen.Start();
    // Load USD file
    UsdStageRefPtr stage = UsdStage::Open(inputFilename);
    if (!stage) {
        PrintUsage(argv[0], "could not load input file");
        return -1;
    }
    timer_usdOpen.Stop();

    //////////////////////////////////////////////////////////////////////// 
    // Render settings
    //

    UsdRenderSpec renderSpec;
    UsdRenderSettings settings;
    if (renderSettingsPath.IsEmpty()) {
        settings = UsdRenderSettings::GetStageRenderSettings(stage);
    } else {
        // If a path was specified, try to use the requested settings prim.
        settings = UsdRenderSettings(stage->GetPrimAtPath(renderSettingsPath));
    }
    if (settings) {
        // If we found USD settings, read those.
        renderSpec = UsdRenderComputeSpec(settings, frameNum, {"ri:"});
    } else {
        // Otherwise, provide a built-in render specification.
        renderSpec = {
            /* products */
            {
                UsdRenderSpec::Product {
                    TfToken("raster"),
                    TfToken(outputFilename),
                    // camera path
                    SdfPath(),
                    false,
                    GfVec2i(512,512),
                    1.0f,
                    // aspectRatioConformPolicy
                    UsdRenderTokens->expandAperture,
                    // aperture size
                    GfVec2f(2.0, 2.0),
                    // data window
                    GfRange2f(GfVec2f(0.0f), GfVec2f(1.0f)),
                    // renderVarIndices
                    { 0, 1 },
                },
            },
            /* renderVars */
            {
                UsdRenderSpec::RenderVar {
                    SdfPath("/Render/Vars/Ci"), TfToken("color3f"),
                    TfToken("Ci")
                },
                UsdRenderSpec::RenderVar {
                    SdfPath("/Render/Vars/Alpha"), TfToken("float"),
                    TfToken("a")
                }
            }
        };
    }

    // Merge fallback settings specific to testHdPrman.
    VtDictionary defaultSettings;
    defaultSettings["ri:hider:jitter"] = 1;
    defaultSettings["ri:hider:minsamples"] = 32;
    defaultSettings["ri:hider:maxsamples"] = 64;
    defaultSettings["ri:trace:maxdepth"] = 10;
    defaultSettings["ri:Ri:PixelVariance"] = 0.01f;
    defaultSettings["ri:Ri:Shutter"] = VtArray<float>({0.0f, 0.5f});

    // Update product settings.
    for (auto &product: renderSpec.products) {
        // Command line overrides built-in paths.
        if (!sceneCamPath.IsEmpty()) {
            product.cameraPath = sceneCamPath;
        }
        if (sceneCamAspect > 0.0) {
            product.resolution[1] = (int)(product.resolution[0]/sceneCamAspect);
            product.apertureSize[1] = product.apertureSize[0]/sceneCamAspect;
        }
        VtDictionaryOver(&product.extraSettings, defaultSettings);
    }

    //////////////////////////////////////////////////////////////////////// 
    //
    // Diagnostic aids
    //

    // These are meant to help keep an eye on how much available
    // concurrency is being used, within an automated test environment.
    printf("Current concurrency limit:  %u\n", WorkGetConcurrencyLimit());
    printf("Physical concurrency limit: %u\n",
        WorkGetPhysicalConcurrencyLimit());

    //////////////////////////////////////////////////////////////////////// 
    //
    // Render loop for products
    //

    // Since this test uses HdPrman directly (usually we would
    // use the plugin system), it does not use HdPrmanLoader. This means
    // we need to link the test against libPrman, however our build system
    // will look at the elf and remove any unused libraries. 
    // By calling RixGetContext here, we make sure the symbols are 
    // not removed post compilation.
    RixContext *rix = RixGetContext();
    TF_UNUSED(rix);

    TfStopwatch timer_hydra;

    // XXX In the future, we should be able to produce multiple
    // products directly from one Riley session.
    for (auto product: renderSpec.products) {
        printf("Rendering %s...\n", product.name.GetText());

        std::shared_ptr<HdPrman_OfflineContext> context =
            std::make_shared<HdPrman_OfflineContext>();

        // Find USD camera prim.
        UsdGeomCamera usdCam;
        if (!product.cameraPath.IsEmpty()) {
            UsdPrim prim = stage->GetPrimAtPath(product.cameraPath);
            if (prim && prim.IsA<UsdGeomCamera>()) {
                usdCam = UsdGeomCamera(prim);
            } else {
                TF_WARN("Invalid scene camera at %s. Falling back to the "
                        "free cam.\n", product.cameraPath.GetText());
            }
        }
        
        // Shutter settings from studio production.
        //
        // XXX Up to RenderMan 22, there is a global Ri:Shutter interval
        // that specifies the time when (all) camera shutters begin opening,
        // and when they (all) finish closing.  This is shutterInterval.
        // Then, per-camera, there is a shutterCurve, which use normalized
        // (0..1) time relative to the global shutterInterval.  This forces
        // all the cameras to have the same shutter interval, so in the
        // future the shutterInterval will be moved to new attributes on
        // the cameras, and shutterCurve will exist an a UsdRi schema.
        //
        float shutterCurve[10] = {0, 0, 0, 0, 0, 0, 0, 1, 0.3, 0};
        if (usdCam) {
            float interval[2] = {0.0, 0.5};
            if (usdCam.GetShutterOpenAttr().Get(&interval[0], frameNum) ||
                usdCam.GetShutterCloseAttr().Get(&interval[1], frameNum)) {
                // XXX Scene-wide shutter will change to be per-camera;
                // see RMAN-14078
                product.extraSettings["ri:Ri:Shutter"] =
                    VtArray<float>({interval[0], interval[1]});
            }
        }

        // Use two samples (start and end) of a frame for now.
        std::vector<double> timeSampleOffsets = {0.0, 1.0};

        // Options
        RtParamList rileyOptions;
        {
            // Searchpaths (TEXTUREPATH, etc)
            HdPrman_UpdateSearchPathsFromEnvironment(rileyOptions);

            // Product extraSettings become Riley options.
            _ConvertSettings(product.extraSettings, rileyOptions);
            
            rileyOptions.SetIntegerArray(RixStr.k_Ri_FormatResolution,
                (int*) &product.resolution, 2);
            rileyOptions.SetFloat(RixStr.k_Ri_FormatPixelAspectRatio,
                product.pixelAspectRatio);

            // Compute screen window from product aperture.
            float screenWindow[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
            if (usdCam) {
                GfCamera gfCam = usdCam.GetCamera(frameNum);
                gfCam.SetHorizontalAperture(product.apertureSize[0]);
                gfCam.SetVerticalAperture(product.apertureSize[1]);
                CameraUtilScreenWindowParameters
                    cuswp(gfCam, GfCamera::FOVVertical);
                GfVec4d screenWindowd = cuswp.GetScreenWindow();
                screenWindow[0] = float(screenWindowd[0]);
                screenWindow[1] = float(screenWindowd[1]);
                screenWindow[2] = float(screenWindowd[2]);
                screenWindow[3] = float(screenWindowd[3]);
            }
            rileyOptions.SetFloatArray(RixStr.k_Ri_ScreenWindow, screenWindow, 4);

            // Crop/Data window.
            float cropWindow[4] = {
                product.dataWindowNDC.GetMin()[0], // xmin
                product.dataWindowNDC.GetMax()[0], // xmax
                product.dataWindowNDC.GetMin()[1], // ymin
                product.dataWindowNDC.GetMax()[1], // ymax
            };
            // RiCropWindow semantics has different float->int behavior
            // than UsdRenderSettings dataWindowNDC, so compensate here.
            float dx = 0.5 / product.resolution[0];
            float dy = 0.5 / product.resolution[1];
            cropWindow[0] -= dx;
            cropWindow[1] -= dx;
            cropWindow[2] -= dy;
            cropWindow[3] -= dy;
            rileyOptions.SetFloatArray(RixStr.k_Ri_CropWindow, cropWindow, 4);
        }

        // Integrator
        // TODO Figure out how to represent this in UsdRi.
        // Perhaps a UsdRiIntegrator prim, plus an adapter
        // in UsdImaging that adds it as an sprim?
        riley::ShadingNode integratorNode;
        {
            integratorNode = riley::ShadingNode {
                riley::ShadingNode::Type::k_Integrator,
                us_PxrPathTracer,
                us_PathTracer,
                RtParamList() };

            // If PxrVisualizer was requested, configure it.
            if (!visualizerStyle.empty()) {
                integratorNode.name = us_PxrVisualizer;
                integratorNode.params.SetInteger(us_wireframe, 1);
                integratorNode.params.SetString(us_style,
                    RtUString(visualizerStyle.c_str()));
            }
        }

        // Camera
        riley::ShadingNode cameraNode;
        RtUString cameraName = us_main_cam;
        riley::Transform cameraXform;
        RtParamList cameraParams;
        {
            RtParamList projParams;

            // Shutter curve (this is relative to the Shutter interval above).
            cameraParams.SetFloat(RixStr.k_shutterOpenTime, shutterCurve[0]);
            cameraParams.SetFloat(RixStr.k_shutterCloseTime, shutterCurve[1]);
            cameraParams.SetFloatArray(RixStr.k_shutteropening,shutterCurve+2,8);

            if (usdCam) {
                GfCamera gfCam = usdCam.GetCamera(frameNum);

                // Clip planes
                GfRange1f clipRange = gfCam.GetClippingRange();
                cameraParams.SetFloat(RixStr.k_nearClip, clipRange.GetMin());
                cameraParams.SetFloat(RixStr.k_farClip, clipRange.GetMax());
                
                // Projection
                projParams.SetFloat(
                    RixStr.k_fov, gfCam.GetFieldOfView(GfCamera::FOVVertical));
                // Convert parameters that are specified in tenths of a world
                // unit in USD to world units for Riley. See
                // UsdImagingCameraAdapter::UpdateForTime for reference.
                projParams.SetFloat(RixStr.k_focalLength,
                    gfCam.GetFocalLength() / 10.0f);
                projParams.SetFloat(RixStr.k_fStop, gfCam.GetFStop());
                projParams.SetFloat(RixStr.k_focalDistance,
                    gfCam.GetFocusDistance());
                cameraNode = riley::ShadingNode {
                    riley::ShadingNode::Type::k_Projection,
                    RtUString(cameraProjection.c_str()),
                    RtUString("main_cam_projection"),
                    projParams
                };
                
                // Transform
                std::vector<GfMatrix4d> xforms;
                xforms.reserve(timeSampleOffsets.size());
                // Get the xform at each time sample
                for (double const& offset : timeSampleOffsets) {
                    UsdGeomXformCache xfc(frameNum + offset);
                    xforms.emplace_back(xfc.GetLocalToWorldTransform(
                        usdCam.GetPrim()));
                }

                // USD camera looks down -Z (RHS), while 
                // Prman camera looks down +Z (RHS)
                GfMatrix4d flipZ(1.0);
                flipZ[2][2] = -1.0;
                RtMatrix4x4 xf_rt_values[HDPRMAN_MAX_TIME_SAMPLES];
                float times[HDPRMAN_MAX_TIME_SAMPLES];
                size_t numNetSamples = std::min(xforms.size(),
                                            (size_t) HDPRMAN_MAX_TIME_SAMPLES);
                for (size_t i=0; i < numNetSamples; i++) {
                    xf_rt_values[i] =
                        HdPrman_GfMatrixToRtMatrix(flipZ * xforms[i]);
                    times[i] = timeSampleOffsets[i];
                }
                cameraXform = {(unsigned) numNetSamples, xf_rt_values, times};
            } else {
                // Projection
                projParams.SetFloat(RixStr.k_fov, 60.0f);
                cameraNode = riley::ShadingNode {
                    riley::ShadingNode::Type::k_Projection,
                    RtUString(cameraProjection.c_str()),
                    RtUString("main_cam_projection"),
                    projParams
                };

                // Transform
                float const zerotime = 0.0f;
                RtMatrix4x4 matrix = RixConstants::k_IdentityMatrix;

                // Orthographic camera:
                // XXX In HdPrman RenderPass we apply orthographic
                // projection as a scale onto the viewMatrix. This
                // is because we currently cannot update Renderman's
                // `ScreenWindow` once it is running.
                if (isOrthographic) {
                    matrix.Scale(10,10,10);
                }

                // Translate camera back a bit
                matrix.Translate(0.f, 0.f, -10.0f);
                cameraXform = { 1, &matrix, &zerotime };
            }
        }

        // Displays & Display Channels
        std::vector<HdPrman_OfflineContext::RenderOutput> renderOutputs;
        for (size_t index: product.renderVarIndices) {
            auto const& renderVar = renderSpec.renderVars[index];

            // Map source to Ri name.
            std::string name = renderVar.sourceName;
            if (renderVar.sourceType == UsdRenderTokens->lpe) {
                name = "lpe:" + name;
            }

            // Map dataType from token to Ri enum.
            // XXX use usd tokens?
            riley::RenderOutputType renderOutputType;
            if (renderVar.dataType == TfToken("color3f")) {
                renderOutputType = riley::RenderOutputType::k_Color;
            } else if (renderVar.dataType == TfToken("float")) {
                renderOutputType = riley::RenderOutputType::k_Float;
            } else if (renderVar.dataType == TfToken("int")) {
                renderOutputType = riley::RenderOutputType::k_Integer;
            } else {
                TF_RUNTIME_ERROR("Unimplemented renderVar dataType '%s'; "
                                 "skipping", renderVar.dataType.GetText());
                continue;
            }

            RtParamList params; 
            // RenderVar extraSettings become Riley channel params.
            _ConvertSettings(renderVar.extraSettings, params);

            HdPrman_OfflineContext::RenderOutput ro;
            ro.name = RtUString(name.c_str());
            ro.type = renderOutputType;
            ro.params = params;
            renderOutputs.push_back(ro);
        }

        // Only allow "raster" for now.
        TF_VERIFY(product.type == TfToken("raster"));
        riley::Extent const format = {
            uint32_t(product.resolution[0]), uint32_t(product.resolution[1]),1};    

        // Fallback materials
        std::vector<riley::ShadingNode> materialNodes;
        {
            riley::ShadingNode pxrPrimvar_node;
            pxrPrimvar_node.type = riley::ShadingNode::Type::k_Pattern;
            pxrPrimvar_node.name = us_PxrPrimvar;
            pxrPrimvar_node.handle = us_pv_color;
            pxrPrimvar_node.params.SetString(us_varname, us_displayColor);
            // Note: this 0.5 gray is to match UsdImaging's fallback.
            pxrPrimvar_node.params.SetColor(us_defaultColor,
                                        RtColorRGB(0.5, 0.5, 0.5));
            pxrPrimvar_node.params.SetString(RixStr.k_type, RixStr.k_color);
            materialNodes.push_back(pxrPrimvar_node);

            riley::ShadingNode pxrSurface_node;
            pxrSurface_node.type = riley::ShadingNode::Type::k_Bxdf;
            pxrSurface_node.name = us_PxrSurface;
            pxrSurface_node.handle = us_simpleTestSurface;
            pxrSurface_node.params.SetColorReference(us_diffuseColor,
                                              us_pv_color_resultRGB);
            pxrSurface_node.params.SetInteger(us_specularModelType, 1);
            pxrSurface_node.params.SetColor(us_specularFaceColor,
                                        RtColorRGB(0.04f));
            pxrSurface_node.params.SetColor(us_specularEdgeColor,
                                        RtColorRGB(1.0f));
            materialNodes.push_back(pxrSurface_node);
        }

        // Fallback volume material
        std::vector<riley::ShadingNode> volumeMaterialNodes;
        {
            riley::ShadingNode pxrVolume_node;
            pxrVolume_node.type = riley::ShadingNode::Type::k_Bxdf;
            pxrVolume_node.name = us_PxrVolume;
            pxrVolume_node.handle = us_simpleVolume;
            pxrVolume_node.params.SetString(us_densityFloatPrimVar, us_density);
            volumeMaterialNodes.push_back(pxrVolume_node);
        }

        // Basic configuration       
        context->Initialize(
                rileyOptions,
                integratorNode,
                cameraName,
                cameraNode,
                cameraXform,
                cameraParams,
                format,
                product.name,
                materialNodes,
                volumeMaterialNodes,
                renderOutputs);

        // Optionally add a fallback light if no lights present in USD file.
        if (UsdLuxListAPI(stage->GetPseudoRoot()).ComputeLightList(
            UsdLuxListAPI::ComputeModeIgnoreCache).empty()) {
            // Light shader
            riley::ShadingNode lightNode {
                riley::ShadingNode::Type::k_Light, // type
                us_PxrDomeLight, // name
                us_lightA, // handle
                RtParamList() 
            };
            lightNode.params.SetFloat(RixStr.k_intensity, 1.0f);
            lightNode.params.SetInteger(us_traceLightPaths, 1);
            lightNode.params.SetString(us_lightGroup, us_A);            

            // Light instance
            float const zerotime = 0.0f;
            RtMatrix4x4 matrix = RixConstants::k_IdentityMatrix;
            riley::Transform xform = { 1, &matrix, &zerotime };
            RtParamList lightAttributes;
            lightAttributes.SetInteger(RixStr.k_visibility_camera, 0);
            lightAttributes.SetInteger(RixStr.k_visibility_indirect, 1);
            lightAttributes.SetInteger(RixStr.k_visibility_transmission, 1);
            lightAttributes.SetString(RixStr.k_grouping_membership,
                                      us_default);
            context->SetFallbackLight(lightNode, xform, lightAttributes);
        }

        // Hydra setup
        //
        // Assemble a Hydra pipeline to feed USD data to Riley. 
        // Scene data flows left-to-right:
        //
        //     => UsdStage
        //       => UsdImagingDelegate (hydra "frontend")
        //         => HdRenderIndex
        //           => HdPrmanRenderDelegate (hydra "backend")
        //             => Riley
        //
        // Note that Hydra is flexible, but that means it takes a few steps
        // to configure the details. This might seem out of proportion in a
        // simple usage example like this, if you don't consider the range of
        // other scenarios Hydra is meant to handle.
        {
            // Set up frontend -> index -> backend
            // TODO We should configure the render delegate to request
            // the appropriate materialBindingPurposes from the USD scene.
            // We should also configure the scene to filter for the
            // requested includedPurposes.
            HdRenderSettingsMap settingsMap;
            HdPrmanRenderDelegate hdPrmanBackend(context, settingsMap);
            std::unique_ptr<HdRenderIndex> hdRenderIndex(
                HdRenderIndex::New(&hdPrmanBackend, HdDriverVector()));
            UsdImagingDelegate hdUsdFrontend(hdRenderIndex.get(),
                                             SdfPath::AbsoluteRootPath());
            hdUsdFrontend.Populate(stage->GetPseudoRoot());
            hdUsdFrontend.SetTime(frameNum);
            hdUsdFrontend.SetRefineLevelFallback(8); // max refinement
            if (!product.cameraPath.IsEmpty()) {
                hdUsdFrontend.SetCameraForSampling(product.cameraPath);
            }

            TfTokenVector renderTags(1, HdRenderTagTokens->geometry);
            // The collection of scene contents to render
            HdRprimCollection hdCollection(
                _tokens->testCollection,
                HdReprSelector(HdReprTokens->smoothHull));
            HdChangeTracker &tracker = hdRenderIndex->GetChangeTracker();
            tracker.AddCollection(_tokens->testCollection);

            // We don't need multi-pass rendering with a pathtracer
            // so we use a single, simple render pass.
            HdRenderPassSharedPtr hdRenderPass =
                hdPrmanBackend.CreateRenderPass(hdRenderIndex.get(),
                                                hdCollection);
            HdRenderPassStateSharedPtr hdRenderPassState =
                hdPrmanBackend.CreateRenderPassState();

            // The task execution graph and engine configuration is also simple.
            HdTaskSharedPtrVector tasks = {
                std::make_shared<Hd_DrawTask>(hdRenderPass,
                                              hdRenderPassState,
                                              renderTags)
            };
            HdEngine hdEngine;
            timer_hydra.Start();
            hdEngine.Execute(hdRenderIndex.get(), &tasks);
            timer_hydra.Stop();
        }
        printf("Rendered %s\n", product.name.GetText());
    }

    if (!traceOutput.empty()) {
        std::ofstream outFile(traceOutput);
        TraceCollector::GetInstance().SetEnabled(false);
        TraceReporter::GetGlobalReporter()->Report(outFile);
    }

    if (!perfOutput.empty()) {
        std::ofstream perfResults(perfOutput);
        perfResults << "{'profile': 'usdOpen',"
            << " 'metric': 'time',"
            << " 'value': " << timer_usdOpen.GetSeconds() << ","
            << " 'samples': 1"
            << " }\n";
        perfResults << "{'profile': 'hydraExecute',"
            << " 'metric': 'time',"
            << " 'value': " << timer_hydra.GetSeconds() << ","
            << " 'samples': 1"
            << " }\n";
        perfResults << "{'profile': 'prmanRender',"
            << " 'metric': 'time',"
            << " 'value': " << timer_prmanRender.GetSeconds() << ","
            << " 'samples': 1"
            << " }\n";
    }
}
