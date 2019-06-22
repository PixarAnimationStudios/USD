#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#
# Qt Components
from qt import QtCore, QtGui, QtWidgets

# Stdlib components
import re, sys, os, cProfile, pstats, traceback
from itertools import groupby
from time import time, sleep
from collections import deque, OrderedDict

# Usd Library Components
from pxr import Usd, UsdGeom, UsdShade, UsdUtils, UsdImagingGL, Glf, Sdf, Tf, Ar
from pxr import UsdAppUtils
from pxr.UsdAppUtils.complexityArgs import RefinementComplexities

# UI Components
from ._usdviewq import Utils
from stageView import StageView
from mainWindowUI import Ui_MainWindow
from primContextMenu import PrimContextMenu
from headerContextMenu import HeaderContextMenu
from layerStackContextMenu import LayerStackContextMenu
from attributeViewContextMenu import AttributeViewContextMenu
from customAttributes import (_GetCustomAttributes, CustomAttribute,
                              BoundingBoxAttribute, LocalToWorldXformAttribute,
                              ResolvedBoundMaterial)
from primTreeWidget import PrimTreeWidget, PrimViewColumnIndex
from primViewItem import PrimViewItem
from variantComboBox import VariantComboBox
from legendUtil import ToggleLegendWithBrowser
import prettyPrint, adjustClipping, adjustDefaultMaterial, settings
from constantGroup import ConstantGroup
from selectionDataModel import ALL_INSTANCES, SelectionDataModel

# Common Utilities
from common import (UIBaseColors, UIPropertyValueSourceColors, UIFonts,
                    GetPropertyColor, GetPropertyTextFont,
                    Timer, Drange, BusyContext, DumpMallocTags, GetShortString,
                    GetInstanceIdForIndex,
                    ResetSessionVisibility, InvisRootPrims, GetAssetCreationTime,
                    PropertyViewIndex, PropertyViewIcons, PropertyViewDataRoles, 
                    RenderModes, ColorCorrectionModes, ShadedRenderModes,
                    PickModes, SelectionHighlightModes, CameraMaskModes,
                    PropTreeWidgetTypeIsRel, PrimNotFoundException,
                    GetRootLayerStackInfo, HasSessionVis, GetEnclosingModelPrim,
                    GetPrimsLoadability, ClearColors,
                    HighlightColors)

import settings2
from settings2 import StateSource
from usdviewApi import UsdviewApi
from rootDataModel import RootDataModel
from viewSettingsDataModel import ViewSettingsDataModel
import plugin
from pythonInterpreter import Myconsole

SETTINGS_VERSION = "1"

class HUDEntries(ConstantGroup):
    # Upper HUD entries (declared in variables for abstraction)
    PRIM = "Prims"
    CV = "CVs"
    VERT = "Verts"
    FACE = "Faces"

    # Lower HUD entries
    PLAYBACK = "Playback"
    RENDER = "Render"
    GETBOUNDS = "BBox"

    # Name for prims that have no type
    NOTYPE = "Typeless"

class PropertyIndex(ConstantGroup):
    VALUE, METADATA, LAYERSTACK, COMPOSITION = range(4)

class UIDefaults(ConstantGroup):
    STAGE_VIEW_WIDTH = 604
    PRIM_VIEW_WIDTH = 521
    ATTRIBUTE_VIEW_WIDTH = 682
    ATTRIBUTE_INSPECTOR_WIDTH = 443
    TOP_HEIGHT = 538
    BOTTOM_HEIGHT = 306

# Name of the Qt binding being used
QT_BINDING = QtCore.__name__.split('.')[0]


class UsdviewDataModel(RootDataModel):

    def __init__(self, printTiming, settings2):
        super(UsdviewDataModel, self).__init__(printTiming)

        self._selectionDataModel = SelectionDataModel(self)
        self._viewSettingsDataModel = ViewSettingsDataModel(self, settings2)

    @property
    def selection(self):
        return self._selectionDataModel

    @property
    def viewSettings(self):
        return self._viewSettingsDataModel

class UIStateProxySource(StateSource):
    """XXX Temporary class which allows AppController to serve as two state sources.
    All fields here will be moved back into AppController in the future.
    """

    def __init__(self, mainWindow, parent, name):
        StateSource.__init__(self, parent, name)

        self._mainWindow = mainWindow
        primViewColumnVisibility = self.stateProperty("primViewColumnVisibility",
                default=[True, True, True, False], validator=lambda value: 
                len(value) == 4)
        propertyViewColumnVisibility = self.stateProperty("propertyViewColumnVisibility",
                default=[True, True, True], validator=lambda value: len(value) == 3)
        attributeInspectorCurrentTab = self.stateProperty("attributeInspectorCurrentTab", default=PropertyIndex.VALUE)

        # UI is different when --norender is used so just save the default splitter sizes.
        # TODO Save the loaded state so it doesn't disappear after using --norender.
        if not self._mainWindow._noRender:
            stageViewWidth = self.stateProperty("stageViewWidth", default=UIDefaults.STAGE_VIEW_WIDTH)
            primViewWidth = self.stateProperty("primViewWidth", default=UIDefaults.PRIM_VIEW_WIDTH)
            attributeViewWidth = self.stateProperty("attributeViewWidth", default=UIDefaults.ATTRIBUTE_VIEW_WIDTH)
            attributeInspectorWidth = self.stateProperty("attributeInspectorWidth", default=UIDefaults.ATTRIBUTE_INSPECTOR_WIDTH)
            topHeight = self.stateProperty("topHeight", default=UIDefaults.TOP_HEIGHT)
            bottomHeight = self.stateProperty("bottomHeight", default=UIDefaults.BOTTOM_HEIGHT)
            viewerMode = self.stateProperty("viewerMode", default=False)

            if viewerMode:
                self._mainWindow._ui.primStageSplitter.setSizes([0, 1])
                self._mainWindow._ui.topBottomSplitter.setSizes([1, 0])
            else:
                self._mainWindow._ui.primStageSplitter.setSizes(
                    [primViewWidth, stageViewWidth])
                self._mainWindow._ui.topBottomSplitter.setSizes(
                    [topHeight, bottomHeight])
            self._mainWindow._ui.attribBrowserInspectorSplitter.setSizes(
                [attributeViewWidth, attributeInspectorWidth])
            self._mainWindow._viewerModeEscapeSizes = topHeight, bottomHeight, primViewWidth, stageViewWidth
        else:
            self._mainWindow._ui.primStageSplitter.setSizes(
                [UIDefaults.PRIM_VIEW_WIDTH, UIDefaults.STAGE_VIEW_WIDTH])
            self._mainWindow._ui.attribBrowserInspectorSplitter.setSizes(
                [UIDefaults.ATTRIBUTE_VIEW_WIDTH, UIDefaults.ATTRIBUTE_INSPECTOR_WIDTH])
            self._mainWindow._ui.topBottomSplitter.setSizes(
                [UIDefaults.TOP_HEIGHT, UIDefaults.BOTTOM_HEIGHT])

        for i, visible in enumerate(primViewColumnVisibility):
            self._mainWindow._ui.primView.setColumnHidden(i, not visible)
        for i, visible in enumerate(propertyViewColumnVisibility):
            self._mainWindow._ui.propertyView.setColumnHidden(i, not visible)

        propertyIndex = attributeInspectorCurrentTab
        if propertyIndex not in PropertyIndex:
            propertyIndex = PropertyIndex.VALUE
        self._mainWindow._ui.propertyInspector.setCurrentIndex(propertyIndex)

    def onSaveState(self, state):
        # UI is different when --norender is used so don't load the splitter sizes.
        if not self._mainWindow._noRender:
            primViewWidth, stageViewWidth = self._mainWindow._ui.primStageSplitter.sizes()
            attributeViewWidth, attributeInspectorWidth = self._mainWindow._ui.attribBrowserInspectorSplitter.sizes()
            topHeight, bottomHeight = self._mainWindow._ui.topBottomSplitter.sizes()
            viewerMode = (bottomHeight == 0 and primViewWidth == 0)

            # If viewer mode is active, save the escape sizes instead of the
            # actual sizes. If there are no escape sizes, just save the defaults.
            if viewerMode:
                if self._mainWindow._viewerModeEscapeSizes is not None:
                    topHeight, bottomHeight, primViewWidth, stageViewWidth = self._mainWindow._viewerModeEscapeSizes
                else:
                    primViewWidth = UIDefaults.STAGE_VIEW_WIDTH
                    stageViewWidth = UIDefaults.PRIM_VIEW_WIDTH
                    attributeViewWidth = UIDefaults.ATTRIBUTE_VIEW_WIDTH
                    attributeInspectorWidth = UIDefaults.ATTRIBUTE_INSPECTOR_WIDTH
                    topHeight = UIDefaults.TOP_HEIGHT
                    bottomHeight = UIDefaults.BOTTOM_HEIGHT

            state["primViewWidth"] = primViewWidth
            state["stageViewWidth"] = stageViewWidth
            state["attributeViewWidth"] = attributeViewWidth
            state["attributeInspectorWidth"] = attributeInspectorWidth
            state["topHeight"] = topHeight
            state["bottomHeight"] = bottomHeight
            state["viewerMode"] = viewerMode

        state["primViewColumnVisibility"] = [
            not self._mainWindow._ui.primView.isColumnHidden(c)
            for c in range(self._mainWindow._ui.primView.columnCount())]
        state["propertyViewColumnVisibility"] = [
            not self._mainWindow._ui.propertyView.isColumnHidden(c)
            for c in range(self._mainWindow._ui.propertyView.columnCount())]

        state["attributeInspectorCurrentTab"] = self._mainWindow._ui.propertyInspector.currentIndex()


class Blocker:
    """Object which can be used to temporarily block the execution of a body of
    code. This object is a context manager, and enters a 'blocked' state when
    used in a 'with' statement. The 'blocked()' method can be used to find if
    the Blocker is in this 'blocked' state.

    For example, this is used to prevent UI code from handling signals from the
    selection data model while the UI code itself modifies selection.
    """

    def __init__(self):

        # A count is used rather than a 'blocked' flag to allow for nested
        # blocking.
        self._count = 0

    def __enter__(self):
        """Enter the 'blocked' state until the context is exited."""

        self._count += 1

    def __exit__(self, *args):
        """Exit the 'blocked' state."""

        self._count -= 1

    def blocked(self):
        """Returns True if in the 'blocked' state, and False otherwise."""

        return self._count > 0


class AppController(QtCore.QObject):

    HYDRA_DISABLED_OPTION_STRING = "HydraDisabled"

    ###########
    # Signals #
    ###########

    @classmethod
    def clearSettings(cls):
        settingsPath = cls._outputBaseDirectory()
        if settingsPath is None:
            return None
        else:
            settingsPath = os.path.join(settingsPath, 'state')
            if not os.path.exists(settingsPath):
                print 'INFO: ClearSettings requested, but there ' \
                      'were no settings currently stored.'
                return None

            if not os.access(settingsPath, os.W_OK):
                print 'ERROR: Could not remove settings file.'
                return None
            else:
                os.remove(settingsPath)

        print 'INFO: Settings restored to default.'

    def _configurePlugins(self):

        with Timer() as t:
            self._plugRegistry = plugin.loadPlugins(
                self._usdviewApi, self._mainWindow)

        if self._printTiming:
            t.PrintTime("configure and load plugins.")

    def _openSettings2(self, defaultSettings):
        settingsPathDir = self._outputBaseDirectory()

        if (settingsPathDir is None) or defaultSettings:
            # Create an ephemeral settings object by withholding the file path.
            self._settings2 = settings2.Settings(SETTINGS_VERSION)
        else:
            settings2Path = os.path.join(settingsPathDir, "state.json")
            self._settings2 = settings2.Settings(SETTINGS_VERSION, settings2Path)

        uiProxy = UIStateProxySource(self, self._settings2, "ui")

    def __del__(self):
        # This is needed to free Qt items before exit; Qt hits failed GTK
        # assertions without it.
        self._primToItemMap.clear()

    def __init__(self, parserData, resolverContextFn):
        QtCore.QObject.__init__(self)

        with Timer() as uiOpenTimer:

            self._primToItemMap = {}
            self._itemsToPush = []
            self._currentSpec = None
            self._currentLayer = None
            self._console = None
            self._debugFlagsWindow = None
            self._interpreter = None
            self._parserData = parserData
            self._noRender = parserData.noRender
            self._noPlugins = parserData.noPlugins
            self._unloaded = parserData.unloaded
            self._resolverContextFn = resolverContextFn
            self._debug = os.getenv('USDVIEW_DEBUG', False)
            self._printTiming = parserData.timing or self._debug
            self._lastViewContext = {}
            if QT_BINDING == 'PySide':
                self._statusFileName = 'state'
                self._deprecatedStatusFileNames = ('.usdviewrc')
            else:
                self._statusFileName = 'state.%s'%QT_BINDING
                self._deprecatedStatusFileNames = ('state', '.usdviewrc')
            self._mallocTags = parserData.mallocTagStats

            self._allowViewUpdates = True

            # When viewer mode is active, the panel sizes are cached so they can
            # be restored later.
            self._viewerModeEscapeSizes = None

            self._rendererNameOpt = parserData.renderer

            if self._rendererNameOpt:
                if self._rendererNameOpt == \
                            AppController.HYDRA_DISABLED_OPTION_STRING:
                    os.environ['HD_ENABLED'] = '0'
                else:
                    os.environ['HD_DEFAULT_RENDERER'] = self._rendererNameOpt

            self._mainWindow = QtWidgets.QMainWindow(None)
            # Showing the window immediately prevents UI flashing.
            self._mainWindow.show()

            self._ui = Ui_MainWindow()
            self._ui.setupUi(self._mainWindow)

            self._mainWindow.setWindowTitle(parserData.usdFile)
            self._statusBar = QtWidgets.QStatusBar(self._mainWindow)
            self._mainWindow.setStatusBar(self._statusBar)

            # Install our custom event filter.  The member assignment of the
            # filter is just for lifetime management
            from appEventFilter import AppEventFilter
            self._filterObj = AppEventFilter(self)
            QtWidgets.QApplication.instance().installEventFilter(self._filterObj)

            # Setup Usdview API and optionally load plugins.  We do this before
            # loading the stage in case a plugin wants to modify global settings
            # that affect stage loading.
            self._plugRegistry = None
            self._usdviewApi = UsdviewApi(self)
            if not self._noPlugins:
                self._configurePlugins()

            # read the stage here
            stage = self._openStage(
                self._parserData.usdFile, self._parserData.sessionLayer,
                self._parserData.populationMask)
            if not stage:
                sys.exit(0)

            if not stage.GetPseudoRoot():
                print parserData.usdFile, 'has no prims; exiting.'
                sys.exit(0)

            self._openSettings2(parserData.defaultSettings)

            self._dataModel = UsdviewDataModel(
                self._printTiming, self._settings2)

            self._dataModel.signalPrimsChanged.connect(
                self._onPrimsChanged)

            self._dataModel.stage = stage

            self._primViewSelectionBlocker = Blocker()
            self._propertyViewSelectionBlocker = Blocker()

            self._dataModel.selection.signalPrimSelectionChanged.connect(
                self._primSelectionChanged)
            self._dataModel.selection.signalPropSelectionChanged.connect(
                self._propSelectionChanged)
            self._dataModel.selection.signalComputedPropSelectionChanged.connect(
                self._propSelectionChanged)

            self._initialSelectPrim = self._dataModel.stage.GetPrimAtPath(
                parserData.primPath)
            if not self._initialSelectPrim:
                print 'Could not find prim at path <%s> to select. '\
                    'Ignoring...' % parserData.primPath
                self._initialSelectPrim = None

            try:
                self._dataModel.viewSettings.complexity = parserData.complexity
            except ValueError:
                fallback = RefinementComplexities.LOW
                sys.stderr.write(("Error: Invalid complexity '{}'. "
                    "Using fallback '{}' instead.\n").format(
                        parserData.complexity, fallback.id))
                self._dataModel.viewSettings.complexity = fallback

            self._hasPrimResync = False
            self._timeSamples = None
            self._stageView = None
            self._startingPrimCamera = None
            if (parserData.camera.IsAbsolutePath() or
                    parserData.camera.pathElementCount > 1):
                self._startingPrimCameraName = None
                self._startingPrimCameraPath = parserData.camera
            else:
                self._startingPrimCameraName = parserData.camera.pathString
                self._startingPrimCameraPath = None

            settingsPathDir = self._outputBaseDirectory()
            if settingsPathDir is None or parserData.defaultSettings:
                # Create an ephemeral settings object with a non existent filepath
                self._settings = settings.Settings('', seq=None, ephemeral=True)
            else:
                settingsPath = os.path.join(settingsPathDir, self._statusFileName)
                for deprecatedName in self._deprecatedStatusFileNames:
                    deprecatedSettingsPath = \
                        os.path.join(settingsPathDir, deprecatedName)
                    if (os.path.isfile(deprecatedSettingsPath) and
                        not os.path.isfile(settingsPath)):
                        warning = ('\nWARNING: The settings file at: '
                                + str(deprecatedSettingsPath) + ' is deprecated.\n'
                                + 'These settings are not being used, the new '
                                + 'settings file will be located at: '
                                + str(settingsPath) + '.\n')
                        print warning
                        break

                self._settings = settings.Settings(settingsPath)

                try:
                    self._settings.load()
                except IOError:
                    # try to force out a new settings file
                    try:
                        self._settings.save()
                    except:
                        settings.EmitWarning(settingsPath)

                except EOFError:
                    # try to force out a new settings file
                    try:
                        self._settings.save()
                    except:
                        settings.EmitWarning(settingsPath)
                except:
                    settings.EmitWarning(settingsPath)

            QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.BusyCursor)

            self._timer = QtCore.QTimer(self)
            # Timeout interval in ms. We set it to 0 so it runs as fast as
            # possible. In advanceFrameForPlayback we use the sleep() call
            # to slow down rendering to self.framesPerSecond fps.
            self._timer.setInterval(0)
            self._lastFrameTime = time()

            # Initialize the upper HUD info
            self._upperHUDInfo = dict()

            # declare dictionary keys for the fps info too
            self._fpsHUDKeys = (HUDEntries.RENDER, HUDEntries.PLAYBACK)

            # Initialize fps HUD with empty strings
            self._fpsHUDInfo = dict(zip(self._fpsHUDKeys,
                                        ["N/A", "N/A"]))
            self._startTime = self._endTime = time()

            # This timer is used to coalesce the primView resizes
            # in certain cases. e.g. When you
            # deactivate/activate a prim.
            self._primViewResizeTimer = QtCore.QTimer(self)
            self._primViewResizeTimer.setInterval(0)
            self._primViewResizeTimer.setSingleShot(True)
            self._primViewResizeTimer.timeout.connect(self._resizePrimView)

            # This timer coalesces GUI resets when the USD stage is modified or
            # reloaded.
            self._guiResetTimer = QtCore.QTimer(self)
            self._guiResetTimer.setInterval(0)
            self._guiResetTimer.setSingleShot(True)
            self._guiResetTimer.timeout.connect(self._resetGUI)

            # Idle timer to push off-screen data to the UI.
            self._primViewUpdateTimer = QtCore.QTimer(self)
            self._primViewUpdateTimer.setInterval(0)
            self._primViewUpdateTimer.timeout.connect(self._updatePrimView)

            # This creates the _stageView and restores state from settings file
            self._resetSettings()

            # This is for validating frame values input on the "Frame" line edit
            validator = QtGui.QDoubleValidator(self)
            self._ui.frameField.setValidator(validator)
            self._ui.rangeEnd.setValidator(validator)
            self._ui.rangeBegin.setValidator(validator)

            stepValidator = QtGui.QDoubleValidator(self)
            stepValidator.setRange(0.01, 1e7, 2)
            self._ui.stepSize.setValidator(stepValidator)

            # This causes the last column of the attribute view (the value)
            # to be stretched to fill the available space
            self._ui.propertyView.header().setStretchLastSection(True)

            self._ui.propertyView.setSelectionBehavior(
                QtWidgets.QAbstractItemView.SelectRows)
            self._ui.primView.setSelectionBehavior(
                QtWidgets.QAbstractItemView.SelectRows)
            # This allows ctrl and shift clicking for multi-selecting
            self._ui.propertyView.setSelectionMode(
                QtWidgets.QAbstractItemView.ExtendedSelection)

            self._ui.propertyView.setHorizontalScrollMode(
                QtWidgets.QAbstractItemView.ScrollPerPixel)

            self._ui.frameSlider.setTracking(
                    self._dataModel.viewSettings.redrawOnScrub)

            self._ui.colorGroup = QtWidgets.QActionGroup(self)
            self._ui.colorGroup.setExclusive(True)
            self._clearColorActions = (
                self._ui.actionBlack,
                self._ui.actionGrey_Dark,
                self._ui.actionGrey_Light,
                self._ui.actionWhite)
            for action in self._clearColorActions:
                self._ui.colorGroup.addAction(action)

            self._ui.threePointLights = QtWidgets.QActionGroup(self)
            self._ui.threePointLights.setExclusive(False)
            self._threePointLightsActions = (
                self._ui.actionKey,
                self._ui.actionFill,
                self._ui.actionBack)
            for action in self._threePointLightsActions:
                self._ui.threePointLights.addAction(action)

            self._ui.renderModeActionGroup = QtWidgets.QActionGroup(self)
            self._ui.renderModeActionGroup.setExclusive(True)
            self._renderModeActions = (
                self._ui.actionWireframe,
                self._ui.actionWireframeOnSurface,
                self._ui.actionSmooth_Shaded,
                self._ui.actionFlat_Shaded,
                self._ui.actionPoints,
                self._ui.actionGeom_Only,
                self._ui.actionGeom_Smooth,
                self._ui.actionGeom_Flat,
                self._ui.actionHidden_Surface_Wireframe)
            for action in self._renderModeActions:
                self._ui.renderModeActionGroup.addAction(action)

            self._ui.colorCorrectionActionGroup = QtWidgets.QActionGroup(self)
            self._ui.colorCorrectionActionGroup.setExclusive(True)
            self._colorCorrectionActions = (
                self._ui.actionNoColorCorrection,
                self._ui.actionSRGBColorCorrection,
                self._ui.actionOpenColorIO)
            for action in self._colorCorrectionActions:
                self._ui.colorCorrectionActionGroup.addAction(action)

            # XXX This should be a validator in ViewSettingsDataModel.
            if self._dataModel.viewSettings.renderMode not in RenderModes:
                fallback = str(
                    self._ui.renderModeActionGroup.actions()[0].text())
                print "Warning: Unknown render mode '%s', falling back to '%s'" % (
                            self._dataModel.viewSettings.renderMode, fallback)
                self._dataModel.viewSettings.renderMode = fallback

            self._ui.pickModeActionGroup = QtWidgets.QActionGroup(self)
            self._ui.pickModeActionGroup.setExclusive(True)
            self._pickModeActions = (
                self._ui.actionPick_Prims,
                self._ui.actionPick_Models,
                self._ui.actionPick_Instances)
            for action in self._pickModeActions:
                self._ui.pickModeActionGroup.addAction(action)

            # XXX This should be a validator in ViewSettingsDataModel.
            if self._dataModel.viewSettings.pickMode not in PickModes:
                fallback = str(self._ui.pickModeActionGroup.actions()[0].text())
                print "Warning: Unknown pick mode '%s', falling back to '%s'" % (
                            self._dataModel.viewSettings.pickMode, fallback)
                self._dataModel.viewSettings.pickMode = fallback

            self._ui.selHighlightModeActionGroup = QtWidgets.QActionGroup(self)
            self._ui.selHighlightModeActionGroup.setExclusive(True)
            self._selHighlightActions = (
                self._ui.actionNever,
                self._ui.actionOnly_when_paused,
                self._ui.actionAlways)
            for action in self._selHighlightActions:
                self._ui.selHighlightModeActionGroup.addAction(action)

            self._ui.highlightColorActionGroup = QtWidgets.QActionGroup(self)
            self._ui.highlightColorActionGroup.setExclusive(True)
            self._selHighlightColorActions = (
                self._ui.actionSelYellow,
                self._ui.actionSelCyan,
                self._ui.actionSelWhite)
            for action in self._selHighlightColorActions:
                self._ui.highlightColorActionGroup.addAction(action)

            self._ui.interpolationActionGroup = QtWidgets.QActionGroup(self)
            self._ui.interpolationActionGroup.setExclusive(True)
            for interpolationType in Usd.InterpolationType.allValues:
                action = self._ui.menuInterpolation.addAction(interpolationType.displayName)
                action.setCheckable(True)
                action.setChecked(
                    self._dataModel.stage.GetInterpolationType() == interpolationType)
                self._ui.interpolationActionGroup.addAction(action)

            self._ui.primViewDepthGroup = QtWidgets.QActionGroup(self)
            for i in range(1, 9):
                action = getattr(self._ui, "actionLevel_" + str(i))
                self._ui.primViewDepthGroup.addAction(action)

            # setup animation objects for the primView and propertyView
            self._propertyLegendAnim = QtCore.QPropertyAnimation(
                self._ui.propertyLegendContainer, "maximumHeight")
            self._primLegendAnim = QtCore.QPropertyAnimation(
                self._ui.primLegendContainer, "maximumHeight")

            # set the context menu policy for the prim browser and attribute
            # inspector headers. This is so we can have a context menu on the
            # headers that allows you to select which columns are visible.
            self._ui.propertyView.header()\
                    .setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
            self._ui.primView.header()\
                    .setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

            # Set custom context menu for attribute browser
            self._ui.propertyView\
                    .setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

            # Set custom context menu for layer stack browser
            self._ui.layerStackView\
                    .setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

            # Set custom context menu for composition tree browser
            self._ui.compositionTreeWidget\
                    .setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

            # Arc path is the most likely to need stretch.
            twh = self._ui.compositionTreeWidget.header()
            twh.setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeToContents)
            twh.setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeToContents)
            twh.setSectionResizeMode(2, QtWidgets.QHeaderView.Stretch)
            twh.setSectionResizeMode(3, QtWidgets.QHeaderView.ResizeToContents)

            # Set the prim view header to have a fixed size type and vis columns
            nvh = self._ui.primView.header()
            nvh.setSectionResizeMode(0, QtWidgets.QHeaderView.Stretch)
            nvh.setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeToContents)
            nvh.setSectionResizeMode(2, QtWidgets.QHeaderView.ResizeToContents)
            nvh.resizeSection(3, 116)
            nvh.setSectionResizeMode(3, QtWidgets.QHeaderView.Fixed)

            pvh = self._ui.propertyView.header()
            pvh.setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeToContents)
            pvh.setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeToContents)
            pvh.setSectionResizeMode(2, QtWidgets.QHeaderView.Stretch)

            # XXX:
            # To avoid QTBUG-12850 (https://bugreports.qt.io/browse/QTBUG-12850),
            # we force the horizontal scrollbar to always be visible for all
            # QTableWidget widgets in use.
            self._ui.primView.setHorizontalScrollBarPolicy(
                QtCore.Qt.ScrollBarAlwaysOn)
            self._ui.propertyView.setHorizontalScrollBarPolicy(
                QtCore.Qt.ScrollBarAlwaysOn)
            self._ui.metadataView.setHorizontalScrollBarPolicy(
                QtCore.Qt.ScrollBarAlwaysOn)
            self._ui.layerStackView.setHorizontalScrollBarPolicy(
                QtCore.Qt.ScrollBarAlwaysOn)

            self._ui.attributeValueEditor.setAppController(self)
            self._ui.primView.InitDrawModeDelegate(self)

            self._ui.currentPathWidget.editingFinished.connect(
                self._currentPathChanged)

            # XXX:
            # To avoid PYSIDE-79 (https://bugreports.qt.io/browse/PYSIDE-79)
            # with Qt4/PySide, we must hold the prim view's selectionModel
            # in a local variable before connecting its signals.
            primViewSelModel = self._ui.primView.selectionModel()
            primViewSelModel.selectionChanged.connect(self._selectionChanged)

            self._ui.primView.itemClicked.connect(self._itemClicked)
            self._ui.primView.itemPressed.connect(self._itemPressed)

            self._ui.primView.header().customContextMenuRequested.connect(
                self._primViewHeaderContextMenu)

            self._timer.timeout.connect(self._advanceFrameForPlayback)

            self._ui.primView.customContextMenuRequested.connect(
                self._primViewContextMenu)

            self._ui.primView.expanded.connect(self._primViewExpanded)

            self._ui.frameSlider.valueChanged.connect(self._setFrameIndex)
            self._ui.frameSlider.sliderMoved.connect(self._sliderMoved)
            self._ui.frameSlider.sliderReleased.connect(self._updateGUIForFrameChange)

            self._ui.frameField.editingFinished.connect(self._frameStringChanged)

            self._ui.rangeBegin.editingFinished.connect(self._rangeBeginChanged)

            self._ui.stepSize.editingFinished.connect(self._stepSizeChanged)

            self._ui.rangeEnd.editingFinished.connect(self._rangeEndChanged)

            self._ui.actionFrame_Forward.triggered.connect(self._advanceFrame)

            self._ui.actionFrame_Backwards.triggered.connect(self._retreatFrame)

            self._ui.actionReset_View.triggered.connect(lambda: self._resetView())

            self._ui.topBottomSplitter.splitterMoved.connect(self._cacheViewerModeEscapeSizes)
            self._ui.primStageSplitter.splitterMoved.connect(self._cacheViewerModeEscapeSizes)
            self._ui.actionToggle_Viewer_Mode.triggered.connect(
                self._toggleViewerMode)

            self._ui.showBBoxes.triggered.connect(self._toggleShowBBoxes)

            self._ui.showAABBox.triggered.connect(self._toggleShowAABBox)

            self._ui.showOBBox.triggered.connect(self._toggleShowOBBox)

            self._ui.showBBoxPlayback.triggered.connect(
                self._toggleShowBBoxPlayback)

            self._ui.useExtentsHint.triggered.connect(self._setUseExtentsHint)

            self._ui.showInterpreter.triggered.connect(self._showInterpreter)

            self._ui.showDebugFlags.triggered.connect(self._showDebugFlags)

            self._ui.redrawOnScrub.toggled.connect(self._redrawOptionToggled)

            if self._stageView:
                self._ui.actionAuto_Compute_Clipping_Planes.triggered.connect(
                    self._toggleAutoComputeClippingPlanes)

            self._ui.actionAdjust_Clipping.triggered[bool].connect(
                self._adjustClippingPlanes)

            self._ui.actionAdjust_Default_Material.triggered[bool].connect(
                self._adjustDefaultMaterial)

            self._ui.actionOpen.triggered.connect(self._openFile)

            self._ui.actionSave_Overrides_As.triggered.connect(
                self._saveOverridesAs)

            self._ui.actionSave_Flattened_As.triggered.connect(
                self._saveFlattenedAs)

            # Setup quit actions to ensure _cleanAndClose is only invoked once.
            self._ui.actionQuit.triggered.connect(QtWidgets.QApplication.instance().quit)

            QtWidgets.QApplication.instance().aboutToQuit.connect(self._cleanAndClose)

            self._ui.actionReopen_Stage.triggered.connect(self._reopenStage)

            self._ui.actionReload_All_Layers.triggered.connect(self._reloadStage)

            self._ui.actionFrame_Selection.triggered.connect(self._frameSelection)

            self._ui.actionToggle_Framed_View.triggered.connect(self._toggleFramedView)

            self._ui.actionAdjust_FOV.triggered.connect(self._adjustFOV)

            self._ui.complexityGroup = QtWidgets.QActionGroup(self._mainWindow)
            self._ui.complexityGroup.setExclusive(True)
            self._complexityActions = (
                self._ui.actionLow,
                self._ui.actionMedium,
                self._ui.actionHigh,
                self._ui.actionVery_High)
            for action in self._complexityActions:
                self._ui.complexityGroup.addAction(action)

            self._ui.complexityGroup.triggered.connect(self._changeComplexity)

            self._ui.actionDisplay_Guide.triggered.connect(
                self._toggleDisplayGuide)

            self._ui.actionDisplay_Proxy.triggered.connect(
                self._toggleDisplayProxy)

            self._ui.actionDisplay_Render.triggered.connect(
                self._toggleDisplayRender)

            self._ui.actionDisplay_Camera_Oracles.triggered.connect(
                self._toggleDisplayCameraOracles)

            self._ui.actionDisplay_PrimId.triggered.connect(
                self._toggleDisplayPrimId)

            self._ui.actionEnable_Scene_Materials.triggered.connect(
                self._toggleEnableSceneMaterials)

            self._ui.actionCull_Backfaces.triggered.connect(
                self._toggleCullBackfaces)

            self._ui.propertyInspector.currentChanged.connect(
                self._updatePropertyInspector)

            self._ui.propertyView.itemSelectionChanged.connect(
                self._propertyViewSelectionChanged)

            self._ui.propertyView.currentItemChanged.connect(
                self._propertyViewCurrentItemChanged)

            self._ui.propertyView.header().customContextMenuRequested.\
                connect(self._propertyViewHeaderContextMenu)

            self._ui.propertyView.customContextMenuRequested.connect(
                self._propertyViewContextMenu)

            self._ui.layerStackView.customContextMenuRequested.connect(
                self._layerStackContextMenu)

            self._ui.compositionTreeWidget.customContextMenuRequested.connect(
                self._compositionTreeContextMenu)

            self._ui.compositionTreeWidget.currentItemChanged.connect(
                self._onCompositionSelectionChanged)

            self._ui.renderModeActionGroup.triggered.connect(self._changeRenderMode)

            self._ui.colorCorrectionActionGroup.triggered.connect(
                self._changeColorCorrection)

            self._ui.pickModeActionGroup.triggered.connect(self._changePickMode)

            self._ui.selHighlightModeActionGroup.triggered.connect(
                self._changeSelHighlightMode)

            self._ui.highlightColorActionGroup.triggered.connect(
                self._changeHighlightColor)

            self._ui.interpolationActionGroup.triggered.connect(
                self._changeInterpolationType)

            self._ui.actionAmbient_Only.triggered[bool].connect(
                self._ambientOnlyClicked)

            self._ui.actionKey.triggered[bool].connect(self._onKeyLightClicked)

            self._ui.actionFill.triggered[bool].connect(self._onFillLightClicked)

            self._ui.actionBack.triggered[bool].connect(self._onBackLightClicked)

            self._ui.colorGroup.triggered.connect(self._changeBgColor)

            # Configuring the PrimView's Show menu.  In addition to the
            # "designed" menu items, we inject a PrimView HeaderContextMenu
            self._ui.primViewDepthGroup.triggered.connect(self._changePrimViewDepth)

            self._ui.actionExpand_All.triggered.connect(
                lambda: self._expandToDepth(1000000))

            self._ui.actionCollapse_All.triggered.connect(
                self._ui.primView.collapseAll)

            self._ui.actionShow_Inactive_Prims.triggered.connect(
                self._toggleShowInactivePrims)

            self._ui.actionShow_All_Master_Prims.triggered.connect(
                self._toggleShowMasterPrims)

            self._ui.actionShow_Undefined_Prims.triggered.connect(
                self._toggleShowUndefinedPrims)

            self._ui.actionShow_Abstract_Prims.triggered.connect(
                self._toggleShowAbstractPrims)

            # Since setting column visibility is probably not a common
            # operation, it's actually good to have Columns at the end.
            self._ui.menuShow.addSeparator()
            self._ui.menuShow.addMenu(HeaderContextMenu(self._ui.primView))


            self._ui.actionRollover_Prim_Info.triggered.connect(
                self._toggleRolloverPrimInfo)

            self._ui.primViewLineEdit.returnPressed.connect(
                self._ui.primViewFindNext.click)

            self._ui.primViewFindNext.clicked.connect(self._primViewFindNext)

            self._ui.attrViewLineEdit.returnPressed.connect(
                self._ui.attrViewFindNext.click)

            self._ui.attrViewFindNext.clicked.connect(self._attrViewFindNext)

            self._ui.primLegendQButton.clicked.connect(
                self._primLegendToggleCollapse)

            self._ui.propertyLegendQButton.clicked.connect(
                self._propertyLegendToggleCollapse)

            self._ui.playButton.clicked.connect(self._playClicked)

            self._ui.actionCameraMask_Full.triggered.connect(
                self._updateCameraMaskMenu)

            self._ui.actionCameraMask_Partial.triggered.connect(
                self._updateCameraMaskMenu)

            self._ui.actionCameraMask_None.triggered.connect(
                self._updateCameraMaskMenu)

            self._ui.actionCameraMask_Outline.triggered.connect(
                self._updateCameraMaskOutlineMenu)

            self._ui.actionCameraMask_Color.triggered.connect(
                self._pickCameraMaskColor)

            self._ui.actionCameraReticles_Inside.triggered.connect(
                self._updateCameraReticlesInsideMenu)

            self._ui.actionCameraReticles_Outside.triggered.connect(
                self._updateCameraReticlesOutsideMenu)

            self._ui.actionCameraReticles_Color.triggered.connect(
                self._pickCameraReticlesColor)

            self._ui.actionHUD.triggered.connect(self._showHUDChanged)

            self._ui.actionHUD_Info.triggered.connect(self._showHUD_InfoChanged)

            self._ui.actionHUD_Complexity.triggered.connect(
                self._showHUD_ComplexityChanged)

            self._ui.actionHUD_Performance.triggered.connect(
                self._showHUD_PerformanceChanged)

            self._ui.actionHUD_GPUstats.triggered.connect(
                self._showHUD_GPUstatsChanged)

            self._mainWindow.addAction(self._ui.actionIncrementComplexity1)
            self._mainWindow.addAction(self._ui.actionIncrementComplexity2)
            self._mainWindow.addAction(self._ui.actionDecrementComplexity)

            self._ui.actionIncrementComplexity1.triggered.connect(
                self._incrementComplexity)

            self._ui.actionIncrementComplexity2.triggered.connect(
                self._incrementComplexity)

            self._ui.actionDecrementComplexity.triggered.connect(
                self._decrementComplexity)

            self._ui.attributeValueEditor.editComplete.connect(self.editComplete)

            # Edit Prim menu
            self._ui.menuEdit.aboutToShow.connect(self._updateEditMenu)
            self._ui.menuNavigation.aboutToShow.connect(self._updateNavigationMenu)

            self._ui.actionFind_Prims.triggered.connect(
                self._ui.primViewLineEdit.setFocus)

            self._ui.actionSelect_Stage_Root.triggered.connect(
                self.selectPseudoroot)

            self._ui.actionSelect_Model_Root.triggered.connect(
                self.selectEnclosingModel)

            self._ui.actionSelect_Bound_Preview_Material.triggered.connect(
                self.selectBoundPreviewMaterial)

            self._ui.actionSelect_Bound_Full_Material.triggered.connect(
                self.selectBoundFullMaterial)

            self._ui.actionSelect_Preview_Binding_Relationship.triggered.connect(
                self.selectPreviewBindingRel)

            self._ui.actionSelect_Full_Binding_Relationship.triggered.connect(
                self.selectFullBindingRel)

            self._ui.actionMake_Visible.triggered.connect(self.visSelectedPrims)
            # Add extra, Presto-inspired shortcut for Make Visible
            self._ui.actionMake_Visible.setShortcuts(["Shift+H", "Ctrl+Shift+H"])

            self._ui.actionMake_Invisible.triggered.connect(self.invisSelectedPrims)

            self._ui.actionVis_Only.triggered.connect(self.visOnlySelectedPrims)

            self._ui.actionRemove_Session_Visibility.triggered.connect(
                self.removeVisSelectedPrims)

            self._ui.actionReset_All_Session_Visibility.triggered.connect(
                self.resetSessionVisibility)

            self._ui.actionLoad.triggered.connect(self.loadSelectedPrims)

            self._ui.actionUnload.triggered.connect(self.unloadSelectedPrims)

            self._ui.actionActivate.triggered.connect(self.activateSelectedPrims)

            self._ui.actionDeactivate.triggered.connect(self.deactivateSelectedPrims)

            # We refresh as if all view settings changed. In the future, we
            # should do more granular refreshes. This first requires more
            # granular signals from ViewSettingsDataModel.
            self._dataModel.viewSettings.signalSettingChanged.connect(
                self._viewSettingChanged)

            # Update view menu actions and submenus with initial state.
            self._refreshViewMenubar()

            # We manually call processEvents() here to make sure that the prim
            # browser and other widgetry get drawn before we draw the first image in
            # the viewer, which might take a long time.
            if self._stageView:
                self._stageView.setUpdatesEnabled(False)

            self._mainWindow.update()

            QtWidgets.QApplication.processEvents()

        if self._printTiming:
            uiOpenTimer.PrintTime('bring up the UI')

        self._drawFirstImage()

        QtWidgets.QApplication.restoreOverrideCursor()

    def _drawFirstImage(self):
        if self._stageView:
            self._stageView.setUpdatesEnabled(True)
        with BusyContext():
            try:
                self._resetView(self._initialSelectPrim)
            except Exception:
                pass
            QtWidgets.QApplication.processEvents()

        # configure render plugins after stageView initialized its renderer.
        self._configureRendererPlugins()
        self._configureColorManagement()

        if self._mallocTags == 'stageAndImaging':
            DumpMallocTags(self._dataModel.stage,
                "stage-loading and imaging")

    def statusMessage(self, msg, timeout = 0):
        self._statusBar.showMessage(msg, timeout * 1000)

    def editComplete(self, msg):
        title = self._mainWindow.windowTitle()
        if title[-1] != '*':
            self._mainWindow.setWindowTitle(title + ' *')

        self.statusMessage(msg, 12)
        with Timer() as t:
            if self._stageView:
                self._stageView.updateView(resetCam=False, forceComputeBBox=True)
        if self._printTiming:
            t.PrintTime("'%s'" % msg)

    def _openStage(self, usdFilePath, sessionFilePath, populationMaskPaths):

        # We are iterating through the plugin registry to add anything
        # containing shaders to the default search path.
        resourcePaths = set()
        from pxr import Plug
        pr = Plug.Registry()
        for t in pr.GetAllPlugins():
            if t.metadata.get('ShaderResources') is not None:
                resourcePaths.add(t.resourcePath)
        Ar.DefaultResolver.SetDefaultSearchPath(sorted(list(resourcePaths)))

        def _GetFormattedError(reasons=[]):
            err = ("Error: Unable to open stage '{0}'\n".format(usdFilePath))
            if reasons:
                err += "\n".join(reasons) + "\n"
            return err

        if not Ar.GetResolver().Resolve(usdFilePath):
            sys.stderr.write(_GetFormattedError(["File not found"]))
            sys.exit(1)

        if self._mallocTags != 'none':
            Tf.MallocTag.Initialize()

        with Timer() as t:
            loadSet = Usd.Stage.LoadNone if self._unloaded else Usd.Stage.LoadAll
            popMask = (None if populationMaskPaths is None else
                       Usd.StagePopulationMask())

            # Open as a layer first to make sure its a valid file format
            try:
                layer = Sdf.Layer.FindOrOpen(usdFilePath)
            except Tf.ErrorException as e:
                sys.stderr.write(_GetFormattedError(
                    [err.commentary.strip() for err in e.args]))
                sys.exit(1)

            if sessionFilePath:
                try:
                    sessionLayer = Sdf.Layer.Find(sessionFilePath)
                    if sessionLayer:
                        sessionLayer.Reload()
                    else:
                        sessionLayer = Sdf.Layer.FindOrOpen(sessionFilePath)
                except Tf.ErrorException as e:
                    sys.stderr.write(_GetFormattedError(
                        [err.commentary.strip() for err in e.args]))
                    sys.exit(1)
            else:
                sessionLayer = Sdf.Layer.CreateAnonymous()

            if popMask:
                for p in populationMaskPaths:
                    popMask.Add(p)
                stage = Usd.Stage.OpenMasked(layer,
                                             sessionLayer,
                                             self._resolverContextFn(usdFilePath),
                                             popMask, loadSet)
            else:
                stage = Usd.Stage.Open(layer,
                                       sessionLayer,
                                       self._resolverContextFn(usdFilePath), 
                                       loadSet)

        if not stage:
            sys.stderr.write(_GetFormattedError())
        else:
            if self._printTiming:
                t.PrintTime('open stage "%s"' % usdFilePath)
            stage.SetEditTarget(stage.GetSessionLayer())

        if self._mallocTags == 'stage':
            DumpMallocTags(stage, "stage-loading")

        return stage

    def _closeStage(self):
        # Close the USD stage.
        if self._stageView:
            self._stageView.closeRenderer()
        self._dataModel.stage = None

    def _setPlayShortcut(self):
        self._ui.playButton.setShortcut(QtGui.QKeySequence(QtCore.Qt.Key_Space))

    # Non-topology dependent UI changes
    def _reloadFixedUI(self, resetStageDataOnly=False):
        # If animation is playing, stop it.
        if self._dataModel.playing:
            self._ui.playButton.click()

        # frame range supplied by user
        ff = self._parserData.firstframe
        lf = self._parserData.lastframe

        # frame range supplied by stage
        stageStartTimeCode = self._dataModel.stage.GetStartTimeCode()
        stageEndTimeCode = self._dataModel.stage.GetEndTimeCode()

        # final range results
        self.realStartTimeCode = None
        self.realEndTimeCode = None

        self.framesPerSecond = self._dataModel.stage.GetFramesPerSecond()

        if not resetStageDataOnly:
            self.step = self._dataModel.stage.GetTimeCodesPerSecond() / self.framesPerSecond
            self._ui.stepSize.setText(str(self.step))

        # if one option is provided(lastframe or firstframe), we utilize it
        if ff is not None and lf is not None:
            self.realStartTimeCode = ff
            self.realEndTimeCode = lf
        elif ff is not None:
            self.realStartTimeCode = ff
            self.realEndTimeCode = stageEndTimeCode
        elif lf is not None:
            self.realStartTimeCode = stageStartTimeCode
            self.realEndTimeCode = lf
        elif self._dataModel.stage.HasAuthoredTimeCodeRange():
            self.realStartTimeCode = stageStartTimeCode
            self.realEndTimeCode = stageEndTimeCode

        self._ui.stageBegin.setText(str(stageStartTimeCode))
        self._ui.stageEnd.setText(str(stageEndTimeCode))

        # Use a valid current frame supplied by user, or allow _UpdateTimeSamples
        # to set the current frame.
        cf = self._parserData.currentframe
        if cf:
            if (cf < self.realStartTimeCode or cf > self.realEndTimeCode):
                sys.stderr.write('Warning: Invalid current frame specified (%s)\n' % (cf))
            else:
                self._dataModel.currentFrame = Usd.TimeCode(cf)
        
        self._UpdateTimeSamples(resetStageDataOnly)


    def _UpdateTimeSamples(self, resetStageDataOnly=False):
        if self.realStartTimeCode is not None and self.realEndTimeCode is not None:
            if self.realStartTimeCode > self.realEndTimeCode:
                sys.stderr.write('Warning: Invalid frame range (%s, %s)\n'
                % (self.realStartTimeCode, self.realEndTimeCode))
                self._timeSamples = []
            else:
                self._timeSamples = Drange(self.realStartTimeCode,
                                           self.realEndTimeCode,
                                           self.step)
        else:
            self._timeSamples = []

        self._geomCounts = dict()
        self._hasTimeSamples = (len(self._timeSamples) > 0)
        self._setPlaybackAvailability() # this sets self._playbackAvailable

        if self._hasTimeSamples:
            self._ui.rangeBegin.setText(str(self._timeSamples[0]))
            self._ui.rangeEnd.setText(str(self._timeSamples[-1]))
            if ( self._dataModel.currentFrame.IsDefault() or
                 self._dataModel.currentFrame < self._timeSamples[0] ):
                self._dataModel.currentFrame = Usd.TimeCode(self._timeSamples[0])
            if self._dataModel.currentFrame > self._timeSamples[-1]:
                self._dataModel.currentFrame = Usd.TimeCode(self._timeSamples[-1])
        else:
            self._dataModel.currentFrame = Usd.TimeCode(0.0)
        
        if not resetStageDataOnly:
            self._ui.frameField.setText(
                str(self._dataModel.currentFrame.GetValue()))

        if self._playbackAvailable:
            if not resetStageDataOnly:
                self._ui.frameSlider.setRange(0, len(self._timeSamples) - 1)
                self._ui.frameSlider.setValue(0)
            self._setPlayShortcut()
            self._ui.playButton.setCheckable(True)
            # Ensure the play button state respects the current playback state
            self._ui.playButton.setChecked(self._dataModel.playing)

    def _clearCaches(self, preserveCamera=False):
        """Clears value and computation caches maintained by the controller.
        Does NOT initiate any GUI updates"""

        self._geomCounts = dict()

        self._dataModel._clearCaches()

        self._refreshCameraListAndMenu(preserveCurrCamera = preserveCamera)

    @staticmethod
    def GetRendererOptionChoices():
        ids = UsdImagingGL.Engine.GetRendererPlugins()
        choices = []
        if ids:
            choices = [UsdImagingGL.Engine.GetRendererDisplayName(x) 
                        for x in ids]
            choices.append(AppController.HYDRA_DISABLED_OPTION_STRING)
        else:
            choices = [AppController.HYDRA_DISABLED_OPTION_STRING]

        return choices

    # Render plugin support
    def _rendererPluginChanged(self, plugin):
        if self._stageView:
            if not self._stageView.SetRendererPlugin(plugin):
                # If SetRendererPlugin failed, we need to reset the check mark
                # to whatever the currently loaded renderer is.
                for action in self._ui.rendererPluginActionGroup.actions():
                    if action.text() == self._stageView.rendererDisplayName:
                        action.setChecked(True)
                        break
                # Then display an error message to let the user know something
                # went wrong, and disable the menu item so it can't be selected
                # again.
                for action in self._ui.rendererPluginActionGroup.actions():
                    if action.pluginType == plugin:
                        self.statusMessage(
                            'Renderer not supported: %s' % action.text())
                        action.setText(action.text() + " (unsupported)")
                        action.setDisabled(True)
                        break
            else:
                # Refresh the AOV menu and settings menu
                self._configureRendererAovs()
                self._configureRendererSettings()

    def _configureRendererPlugins(self):
        if self._stageView:
            self._ui.rendererPluginActionGroup = QtWidgets.QActionGroup(self)
            self._ui.rendererPluginActionGroup.setExclusive(True)

            pluginTypes = self._stageView.GetRendererPlugins()
            for pluginType in pluginTypes:
                name = self._stageView.GetRendererDisplayName(pluginType)
                action = self._ui.menuRendererPlugin.addAction(name)
                action.setCheckable(True)
                action.pluginType = pluginType
                self._ui.rendererPluginActionGroup.addAction(action)

                action.triggered[bool].connect(lambda _, pluginType=pluginType:
                        self._rendererPluginChanged(pluginType))


            # Now set the checked box on the current renderer (it should
            # have been set by now).
            currentRendererId = self._stageView.GetCurrentRendererId()
            foundPlugin = False
            
            for action in self._ui.rendererPluginActionGroup.actions():
                if action.pluginType == currentRendererId:
                    action.setChecked(True)
                    foundPlugin = True
                    break

            # Disable the menu if no plugins were found
            self._ui.menuRendererPlugin.setEnabled(foundPlugin)

            # Refresh the AOV menu and settings menu
            self._configureRendererAovs()
            self._configureRendererSettings()

    # Renderer AOV support
    def _rendererAovChanged(self, aov):
        if self._stageView:
            self._stageView.SetRendererAov(aov)
            self._ui.aovOtherAction.setText("Other...")

    def _configureRendererAovs(self):
        if self._stageView:
            self._ui.rendererAovActionGroup = QtWidgets.QActionGroup(self)
            self._ui.rendererAovActionGroup.setExclusive(True)
            self._ui.menuRendererAovs.clear()

            aovs = self._stageView.GetRendererAovs()
            for aov in aovs:
                action = self._ui.menuRendererAovs.addAction(aov)
                action.setCheckable(True)
                if (aov == "color"):
                    action.setChecked(True)
                action.aov = aov
                self._ui.rendererAovActionGroup.addAction(action)

                action.triggered[bool].connect(lambda _, aov=aov:
                        self._rendererAovChanged(aov))

            self._ui.menuRendererAovs.addSeparator()

            self._ui.aovOtherAction = self._ui.menuRendererAovs.addAction("Other...")
            self._ui.aovOtherAction.setCheckable(True)
            self._ui.aovOtherAction.aov = "Other"
            self._ui.rendererAovActionGroup.addAction(self._ui.aovOtherAction)
            self._ui.aovOtherAction.triggered[bool].connect(self._otherAov)

            self._ui.menuRendererAovs.setEnabled(len(aovs) != 0)

    def _otherAov(self):
        # If we've already selected "Other..." as an AOV, populate the current
        # AOV name.
        initial = ""
        if self._ui.aovOtherAction.text() != "Other...":
            initial = self._stageView.rendererAovName

        aov, ok = QtWidgets.QInputDialog.getText(self._mainWindow, "Other AOVs",
            "Enter the aov name. Visualize primvars with \"primvars:name\".",
            QtWidgets.QLineEdit.Normal, initial)
        if (ok and len(aov) > 0):
            self._rendererAovChanged(str(aov))
            self._ui.aovOtherAction.setText("Other (%r)..." % str(aov))
        else:
            for action in self._ui.rendererAovActionGroup.actions():
                if action.text() == self._stageView.rendererAovName:
                    action.setChecked(True)
                    break

    def _rendererSettingsFlagChanged(self, action):
        if self._stageView:
            self._stageView.SetRendererSetting(action.key, action.isChecked())

    def _configureRendererSettings(self):
        if self._stageView:
            self._ui.menuRendererSettings.clear()
            self._ui.settingsFlagActions = []

            settings = self._stageView.GetRendererSettingsList()
            moreSettings = False
            for setting in settings:
                if setting.type != UsdImagingGL.RendererSettingType.FLAG:
                    moreSettings = True
                    continue
                action = self._ui.menuRendererSettings.addAction(setting.name)
                action.setCheckable(True)
                action.key = str(setting.key)
                action.setChecked(self._stageView.GetRendererSetting(setting.key))
                action.triggered[bool].connect(lambda _, action=action:
                    self._rendererSettingsFlagChanged(action))
                self._ui.settingsFlagActions.append(action)

            if moreSettings:
                self._ui.menuRendererSettings.addSeparator()

                self._ui.settingsMoreAction = self._ui.menuRendererSettings.addAction("More...")
                self._ui.settingsMoreAction.setCheckable(False)
                self._ui.settingsMoreAction.triggered[bool].connect(self._moreRendererSettings)

            self._ui.menuRendererSettings.setEnabled(len(settings) != 0)

            # Close the old "More..." dialog if it's still open
            if hasattr(self._ui, 'settingsMoreDialog'):
                self._ui.settingsMoreDialog.reject()

    def _moreRendererSettings(self):
        # Recreate the settings dialog
        self._ui.settingsMoreDialog = QtWidgets.QDialog(self._mainWindow)
        self._ui.settingsMoreDialog.setWindowTitle("Hydra Settings")
        self._ui.settingsMoreWidgets = []
        layout = QtWidgets.QVBoxLayout()

        # Add settings
        groupBox = QtWidgets.QGroupBox()
        formLayout = QtWidgets.QFormLayout()
        groupBox.setLayout(formLayout)
        layout.addWidget(groupBox)
        formLayout.setLabelAlignment(QtCore.Qt.AlignLeft)
        formLayout.setFormAlignment(QtCore.Qt.AlignRight)

        settings = self._stageView.GetRendererSettingsList()
        for setting in settings:
            if setting.type == UsdImagingGL.RendererSettingType.FLAG:
                checkBox = QtWidgets.QCheckBox()
                checkBox.setChecked(self._stageView.GetRendererSetting(setting.key))
                checkBox.key = str(setting.key)
                checkBox.defValue = setting.defValue
                formLayout.addRow(setting.name, checkBox)
                self._ui.settingsMoreWidgets.append(checkBox)
            if setting.type == UsdImagingGL.RendererSettingType.INT:
                spinBox = QtWidgets.QSpinBox()
                spinBox.setMinimum(-2 ** 31)
                spinBox.setMaximum(2 ** 31 - 1)
                spinBox.setValue(self._stageView.GetRendererSetting(setting.key))
                spinBox.key = str(setting.key)
                spinBox.defValue = setting.defValue
                formLayout.addRow(setting.name, spinBox)
                self._ui.settingsMoreWidgets.append(spinBox)
            if setting.type == UsdImagingGL.RendererSettingType.FLOAT:
                spinBox = QtWidgets.QDoubleSpinBox()
                spinBox.setDecimals(10)
                spinBox.setMinimum(-2 ** 31)
                spinBox.setMaximum(2 ** 31 - 1)
                spinBox.setValue(self._stageView.GetRendererSetting(setting.key))
                spinBox.key = str(setting.key)
                spinBox.defValue = setting.defValue
                formLayout.addRow(setting.name, spinBox)
                self._ui.settingsMoreWidgets.append(spinBox)
            if setting.type == UsdImagingGL.RendererSettingType.STRING:
                lineEdit = QtWidgets.QLineEdit()
                lineEdit.setText(self._stageView.GetRendererSetting(setting.key))
                lineEdit.key = str(setting.key)
                lineEdit.defValue = setting.defValue
                formLayout.addRow(setting.name, lineEdit)
                self._ui.settingsMoreWidgets.append(lineEdit)

        # Add buttons
        buttonBox = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.Ok |
            QtWidgets.QDialogButtonBox.Cancel |
            QtWidgets.QDialogButtonBox.RestoreDefaults)
        layout.addWidget(buttonBox)
        buttonBox.rejected.connect(self._ui.settingsMoreDialog.reject)
        buttonBox.accepted.connect(self._ui.settingsMoreDialog.accept)
        self._ui.settingsMoreDialog.accepted.connect(self._applyMoreRendererSettings)
        defaultButton = buttonBox.button(QtWidgets.QDialogButtonBox.RestoreDefaults)
        defaultButton.clicked.connect(self._resetMoreRendererSettings)

        self._ui.settingsMoreDialog.setLayout(layout)
        self._ui.settingsMoreDialog.show()

    def _applyMoreRendererSettings(self):
        for widget in self._ui.settingsMoreWidgets:
            if isinstance(widget, QtWidgets.QCheckBox):
                self._stageView.SetRendererSetting(widget.key, widget.isChecked())
            if isinstance(widget, QtWidgets.QSpinBox):
                self._stageView.SetRendererSetting(widget.key, widget.value())
            if isinstance(widget, QtWidgets.QDoubleSpinBox):
                self._stageView.SetRendererSetting(widget.key, widget.value())
            if isinstance(widget, QtWidgets.QLineEdit):
                self._stageView.SetRendererSetting(widget.key, widget.text())

        for action in self._ui.settingsFlagActions:
            action.setChecked(self._stageView.GetRendererSetting(action.key))

    def _resetMoreRendererSettings(self):
        for widget in self._ui.settingsMoreWidgets:
            if isinstance(widget, QtWidgets.QCheckBox):
                widget.setChecked(widget.defValue)
            if isinstance(widget, QtWidgets.QSpinBox):
                widget.setValue(widget.defValue)
            if isinstance(widget, QtWidgets.QDoubleSpinBox):
                widget.setValue(widget.defValue)
            if isinstance(widget, QtWidgets.QLineEdit):
                widget.setText(widget.defValue)

    def _configureColorManagement(self):
        enableMenu = (not self._noRender and 
                      UsdImagingGL.Engine.IsColorCorrectionCapable())
        self._ui.menuColorCorrection.setEnabled(enableMenu)

    # Topology-dependent UI changes
    def _reloadVaryingUI(self):

        self._clearCaches()

        if self._debug:
            cProfile.runctx('self._resetPrimView(restoreSelection=False)', globals(), locals(), 'resetPrimView')
            p = pstats.Stats('resetPrimView')
            p.strip_dirs().sort_stats(-1).print_stats()
        else:
            self._resetPrimView(restoreSelection=False)

        if not self._stageView:

            # The second child is self._ui.glFrame, which disappears if
            # its size is set to zero.
            if self._noRender:
                # remove glFrame from the ui
                self._ui.glFrame.setParent(None)

                # move the attributeBrowser into the primSplitter instead
                self._ui.primStageSplitter.addWidget(self._ui.attributeBrowserFrame)

            else:
                self._stageView = StageView(parent=self._mainWindow,
                    dataModel=self._dataModel,
                    printTiming=self._printTiming)

                self._stageView.fpsHUDInfo = self._fpsHUDInfo
                self._stageView.fpsHUDKeys = self._fpsHUDKeys

                self._stageView.signalPrimSelected.connect(self.onPrimSelected)
                self._stageView.signalPrimRollover.connect(self.onRollover)
                self._stageView.signalMouseDrag.connect(self.onStageViewMouseDrag)
                self._stageView.signalErrorMessage.connect(self.statusMessage)

                layout = QtWidgets.QVBoxLayout()
                layout.setContentsMargins(0, 0, 0, 0)
                self._ui.glFrame.setLayout(layout)
                layout.addWidget(self._stageView)

        self._primSearchResults = deque([])
        self._attrSearchResults = deque([])
        self._primSearchString = ""
        self._attrSearchString = ""
        self._lastPrimSearched = self._dataModel.selection.getFocusPrim()

        if self._stageView:
            self._stageView.setFocus(QtCore.Qt.TabFocusReason)
            self._stageView.rolloverPicking = self._dataModel.viewSettings.rolloverPrimInfo

    def _scheduleResizePrimView(self):
        """ Schedules a resize of the primView widget.
            This will call _resizePrimView when the timer expires
            (uses timer coalescing to prevent redundant resizes from occurring).
        """
        self._primViewResizeTimer.start(0)

    def _resizePrimView(self):
        """ Used to coalesce excess calls to resizeColumnToContents.
        """
        self._ui.primView.resizeColumnToContents(0)

    # This appears to be "reasonably" performant in normal sized pose caches.
    # If it turns out to be too slow, or if we want to do a better job of
    # preserving the view the user currently has, we could look into ways of
    # reconstructing just the prim tree under the "changed" prim(s).  The
    # (far and away) faster solution would be to implement our own TreeView
    # and model in C++.
    def _resetPrimView(self, restoreSelection=True):
        with Timer() as t, BusyContext():
            startingDepth = 3
            self._computeDisplayPredicate()
            with self._primViewSelectionBlocker:
                self._ui.primView.setUpdatesEnabled(False)
                self._ui.primView.clear()
                self._primToItemMap.clear()
                self._itemsToPush = []
                # force new search since we are blowing away the primViewItems
                # that may be cached in _primSearchResults
                self._primSearchResults = []
                self._populateRoots()
                # it's confusing to see timing for expand followed by reset with
                # the times being similar (esp when they are large)
                self._expandToDepth(startingDepth, suppressTiming=True)
                if restoreSelection:
                    self._refreshPrimViewSelection()
                self._ui.primView.setUpdatesEnabled(True)
            self._refreshCameraListAndMenu(preserveCurrCamera = True)
        if self._printTiming:
            t.PrintTime("reset Prim Browser to depth %d" % startingDepth)

    def _resetGUI(self):
        """Perform a full refresh/resync of all GUI contents. This should be
        called whenever the USD stage is modified, and assumes that all data
        previously fetched from the stage is invalid. In the future, more
        granular updates will be supported by listening to UsdNotice objects on
        the active stage.

        If a prim resync is needed then we fully update the prim view,
        otherwise can just do a simplified update to the prim view.
        """
        with BusyContext():
            if self._hasPrimResync:
                self._resetPrimView()
                self._hasPrimResync = False
            else:
                self._resetPrimViewVis(selItemsOnly=False)

            self._updatePropertyView()
            self._populatePropertyInspector()
            self._updateMetadataView()
            self._updateLayerStackView()
            self._updateCompositionView()

            if self._stageView:
                self._stageView.update()

    def updateGUI(self):
        """Will schedule a full refresh/resync of the GUI contents.
        Prefer this to calling _resetGUI() directly, since it will
        coalesce multiple calls to this method in to a single refresh.
        """
        self._guiResetTimer.start()

    def _resetPrimViewVis(self, selItemsOnly=True,
                          authoredVisHasChanged=True):
        """Updates browser rows' Vis columns... can update just selected
        items (and their descendants and ancestors), or all items in the
        primView.  When authoredVisHasChanged is True, we force each item
        to discard any value caches it may be holding onto."""
        with Timer() as t:
            self._ui.primView.setUpdatesEnabled(False)
            rootsToProcess = self.getSelectedItems() if selItemsOnly else \
                [self._ui.primView.invisibleRootItem()]
            for item in rootsToProcess:
                PrimViewItem.propagateVis(item, authoredVisHasChanged)
            self._ui.primView.setUpdatesEnabled(True)
        if self._printTiming:
            t.PrintTime("update vis column")

    def _updatePrimView(self):
        # Process some more prim view items.
        n = min(100, len(self._itemsToPush))
        if n:
            items = self._itemsToPush[-n:]
            del self._itemsToPush[-n:]
            for item in items:
                item.push()
        else:
            self._primViewUpdateTimer.stop()

    # Option windows ==========================================================

    def _setComplexity(self, complexity):
        """Set the complexity and update the UI."""
        self._dataModel.viewSettings.complexity = complexity

    def _incrementComplexity(self):
        """Jump up to the next level of complexity."""
        self._setComplexity(RefinementComplexities.next(
            self._dataModel.viewSettings.complexity))

    def _decrementComplexity(self):
        """Jump back to the previous level of complexity."""
        self._setComplexity(RefinementComplexities.prev(
            self._dataModel.viewSettings.complexity))

    def _changeComplexity(self, action):
        """Update the complexity from a selected QAction."""
        self._setComplexity(RefinementComplexities.fromName(action.text()))

    def _adjustFOV(self):
        fov = QtWidgets.QInputDialog.getDouble(self._mainWindow, "Adjust FOV",
            "Enter a value between 0 and 180", self._dataModel.viewSettings.freeCamera.fov, 0, 180)
        if (fov[1]):
            self._dataModel.viewSettings.freeCamera.fov = fov[0]
            if self._stageView:
                self._stageView.update()

    def _adjustClippingPlanes(self, checked):
        # Eventually, this will not be accessible when _stageView is None.
        # Until then, silently ignore.
        if self._stageView:
            if (checked):
                self._adjustClippingDlg = adjustClipping.AdjustClipping(self._mainWindow,
                                                                     self._stageView)
                self._adjustClippingDlg.finished.connect(
                    lambda status : self._ui.actionAdjust_Clipping.setChecked(False))

                self._adjustClippingDlg.show()
            else:
                self._adjustClippingDlg.close()

    def _adjustDefaultMaterial(self, checked):
        if (checked):
            self._adjustDefaultMaterialDlg = adjustDefaultMaterial.AdjustDefaultMaterial(
                self._mainWindow, self._dataModel.viewSettings)
            self._adjustDefaultMaterialDlg.finished.connect(lambda status :
                self._ui.actionAdjust_Default_Material.setChecked(False))

            self._adjustDefaultMaterialDlg.show()
        else:
            self._adjustDefaultMaterialDlg.close()

    def _redrawOptionToggled(self, checked):
        self._dataModel.viewSettings.redrawOnScrub = checked
        self._ui.frameSlider.setTracking(
            self._dataModel.viewSettings.redrawOnScrub)

    # Frame-by-frame/Playback functionality ===================================

    def _setPlaybackAvailability(self, enabled = True):
        isEnabled = len(self._timeSamples) > 1 and enabled
        self._playbackAvailable = isEnabled

        #If playback is disabled, but the animation is playing...
        if not isEnabled and self._dataModel.playing:
            self._ui.playButton.click()

        self._ui.playButton.setEnabled(isEnabled)
        self._ui.frameSlider.setEnabled(isEnabled)
        self._ui.actionFrame_Forward.setEnabled(isEnabled)
        self._ui.actionFrame_Backwards.setEnabled(isEnabled)
        self._ui.frameField.setEnabled(isEnabled
                                       if self._hasTimeSamples else False)
        self._ui.frameLabel.setEnabled(isEnabled
                                       if self._hasTimeSamples else False)
        self._ui.stageBegin.setEnabled(isEnabled)
        self._ui.stageEnd.setEnabled(isEnabled)
        self._ui.redrawOnScrub.setEnabled(isEnabled)
        self._ui.stepSizeLabel.setEnabled(isEnabled)
        self._ui.stepSize.setEnabled(isEnabled)


    def _playClicked(self):
        if self._ui.playButton.isChecked():
            # Enable tracking whilst playing
            self._ui.frameSlider.setTracking(True)
            # Start playback.
            self._dataModel.playing = True
            self._ui.playButton.setText("Stop")
            # setText() causes the shortcut to be reset to whatever
            # Qt thinks it should be based on the text.  We know better.
            self._setPlayShortcut()
            self._fpsHUDInfo[HUDEntries.PLAYBACK]  = "..."
            self._timer.start()
            # For performance, don't update the prim tree view while playing.
            self._primViewUpdateTimer.stop()
            self._playbackIndex = 0
        else:
            self._ui.frameSlider.setTracking(self._ui.redrawOnScrub.isChecked())
            # Stop playback.
            self._dataModel.playing = False
            self._ui.playButton.setText("Play")
            # setText() causes the shortcut to be reset to whatever
            # Qt thinks it should be based on the text.  We know better.
            self._setPlayShortcut()
            self._fpsHUDInfo[HUDEntries.PLAYBACK]  = "N/A"
            self._timer.stop()
            self._primViewUpdateTimer.start()
            self._updateOnFrameChange()

    def _advanceFrameForPlayback(self):
        sleep(max(0, 1. / self.framesPerSecond - (time() - self._lastFrameTime)))
        self._lastFrameTime = time()
        if self._playbackIndex == 0:
            self._startTime = time()
        if self._playbackIndex == 4:
            self._endTime = time()
            delta = (self._endTime - self._startTime)/4.
            ms = delta * 1000.
            fps = 1. / delta
            self._fpsHUDInfo[HUDEntries.PLAYBACK] = "%.2f ms (%.2f FPS)" % (ms, fps)

        self._playbackIndex = (self._playbackIndex + 1) % 5
        self._advanceFrame()

    def _advanceFrame(self):
        if self._playbackAvailable:
            value = self._ui.frameSlider.value() + 1
            if value > self._ui.frameSlider.maximum():
                value = self._ui.frameSlider.minimum()
            self._ui.frameSlider.setValue(value)

    def _retreatFrame(self):
        if self._playbackAvailable:
            value = self._ui.frameSlider.value() - 1
            if value < self._ui.frameSlider.minimum():
                value = self._ui.frameSlider.maximum()
            self._ui.frameSlider.setValue(value)

    def _findClosestFrameIndex(self, timeSample):
        """Find the closest frame index for the given `timeSample`.

        Args:
            timeSample (float): A time sample value.

        Returns:
            int: The closest matching frame index or 0 if one cannot be
            found.
        """
        closestIndex = int(round((timeSample - self._timeSamples[0]) / self.step))

        # Bounds checking
        # 0 <= closestIndex <= number of time samples - 1
        closestIndex = max(0, closestIndex)
        closestIndex = min(len(self._timeSamples) - 1, closestIndex)

        return closestIndex

    def _rangeBeginChanged(self):
        value = float(self._ui.rangeBegin.text())
        if value != self.realStartTimeCode:
            self.realStartTimeCode = value
            self._UpdateTimeSamples(resetStageDataOnly=False)

    def _stepSizeChanged(self):
        value = float(self._ui.stepSize.text())
        if value != self.step:
            self.step = value
            self._UpdateTimeSamples(resetStageDataOnly=False)

    def _rangeEndChanged(self):
        value = float(self._ui.rangeEnd.text())
        if value != self.realEndTimeCode:
            self.realEndTimeCode = value
            self._UpdateTimeSamples(resetStageDataOnly=False)

    def _frameStringChanged(self):
        value = float(self._ui.frameField.text())
        self.setFrame(value)

    def _sliderMoved(self, frameIndex):
        """Slot called when the frame slider is moved by a user.

        Args:
            frameIndex (int): The new frame index value.
        """
        # If redraw on scrub is disabled, ensure we still update the
        # frame field.
        if not self._ui.redrawOnScrub.isChecked():
            self.setFrameField(self._timeSamples[frameIndex])

    def setFrameField(self, frame):
        """Set the frame field to the given `frame`.

        Args:
            frame (str|int|float): The new frame value.
        """
        frame = round(float(frame), ndigits=2)
        self._ui.frameField.setText(str(frame))

    # Prim/Attribute search functionality =====================================

    def _findPrims(self, pattern, useRegex=True):
        """Search the Usd Stage for matching prims
        """
        # If pattern doesn't contain regexp special chars, drop
        # down to simple search, as it's faster
        if useRegex and re.match("^[0-9_A-Za-z]+$", pattern):
            useRegex = False
        if useRegex:
            isMatch = re.compile(pattern, re.IGNORECASE).search
        else:
            pattern = pattern.lower()
            isMatch = lambda x: pattern in x.lower()

        matches = [prim.GetPath() for prim
                   in Usd.PrimRange.Stage(self._dataModel.stage,
                                             self._displayPredicate)
                   if isMatch(prim.GetName())]

        if self._dataModel.viewSettings.showAllMasterPrims:
            for master in self._dataModel.stage.GetMasters():
                matches += [prim.GetPath() for prim
                            in Usd.PrimRange(master, self._displayPredicate)
                            if isMatch(prim.GetName())]

        return matches

    def _primViewFindNext(self):
        if (self._primSearchString == self._ui.primViewLineEdit.text() and
            len(self._primSearchResults) > 0 and
            self._lastPrimSearched == self._dataModel.selection.getFocusPrim()):
            # Go to the next result of the currently ongoing search.
            # First time through, we'll be converting from SdfPaths
            # to items (see the append() below)
            nextResult = self._primSearchResults.popleft()
            if isinstance(nextResult, Sdf.Path):
                nextResult = self._getItemAtPath(nextResult)

            if nextResult:
                with self._dataModel.selection.batchPrimChanges:
                    self._dataModel.selection.clearPrims()
                    self._dataModel.selection.addPrim(nextResult.prim)
                self._primSearchResults.append(nextResult)
                self._lastPrimSearched = self._dataModel.selection.getFocusPrim()
            # The path is effectively pruned if we couldn't map the
            # path to an item
        else:
            # Begin a new search
            with Timer() as t:
                self._primSearchString = self._ui.primViewLineEdit.text()
                self._primSearchResults = self._findPrims(str(self._ui.primViewLineEdit.text()))

                self._primSearchResults = deque(self._primSearchResults)
                self._lastPrimSearched = self._dataModel.selection.getFocusPrim()

                if (len(self._primSearchResults) > 0):
                    self._primViewFindNext()
            if self._printTiming:
                t.PrintTime("match '%s' (%d matches)" %
                            (self._primSearchString,
                             len(self._primSearchResults)))

    def _primLegendToggleCollapse(self):
        ToggleLegendWithBrowser(self._ui.primLegendContainer,
                                self._ui.primLegendQButton,
                                self._primLegendAnim)

    def _propertyLegendToggleCollapse(self):
        ToggleLegendWithBrowser(self._ui.propertyLegendContainer,
                                self._ui.propertyLegendQButton,
                                self._propertyLegendAnim)

    def _attrViewFindNext(self):
        if (self._attrSearchString == self._ui.attrViewLineEdit.text() and
            len(self._attrSearchResults) > 0 and
            self._lastPrimSearched == self._dataModel.selection.getFocusPrim()):

            # Go to the next result of the currently ongoing search
            nextResult = self._attrSearchResults.popleft()
            itemName = str(nextResult.text(PropertyViewIndex.NAME))

            selectedProp = self._propertiesDict[itemName]
            if isinstance(selectedProp, CustomAttribute):
                self._dataModel.selection.clearProps()
                self._dataModel.selection.setComputedProp(selectedProp)
            else:
                self._dataModel.selection.setProp(selectedProp)
                self._dataModel.selection.clearComputedProps()
            self._ui.propertyView.scrollToItem(nextResult)

            self._attrSearchResults.append(nextResult)
            self._lastPrimSearched = self._dataModel.selection.getFocusPrim()

            self._ui.attributeValueEditor.populate(
                self._dataModel.selection.getFocusPrim().GetPath(), itemName)
            self._updateMetadataView(self._getSelectedObject())
            self._updateLayerStackView(self._getSelectedObject())
        else:
            # Begin a new search
            self._attrSearchString = self._ui.attrViewLineEdit.text()
            attrSearchItems = self._ui.propertyView.findItems(
                self._ui.attrViewLineEdit.text(),
                QtCore.Qt.MatchRegExp,
                PropertyViewIndex.NAME)

            # Now just search for the string itself
            otherSearch = self._ui.propertyView.findItems(
                self._ui.attrViewLineEdit.text(),
                QtCore.Qt.MatchContains,
                PropertyViewIndex.NAME)

            combinedItems = attrSearchItems + otherSearch
            # We find properties first, then connections/targets
            # Based on the default recursive match finding in Qt.
            combinedItems.sort()

            self._attrSearchResults = deque(combinedItems)

            self._lastPrimSearched = self._dataModel.selection.getFocusPrim()
            if (len(self._attrSearchResults) > 0):
                self._attrViewFindNext()

    @classmethod
    def _outputBaseDirectory(cls):
        homeDirRoot = os.getenv('HOME') or os.path.expanduser('~')
        baseDir = os.path.join(homeDirRoot, '.usdview')

        try:
            if not os.path.exists(baseDir):
                os.makedirs(baseDir)
            return baseDir

        except OSError:
            sys.stderr.write('ERROR: Unable to create base directory '
                             'for settings file, settings will not be saved.\n')
            return None

    # View adjustment functionality ===========================================

    def _storeAndReturnViewState(self):
        lastView = self._lastViewContext
        self._lastViewContext = self._stageView.copyViewState()
        return lastView

    def _frameSelection(self):
        if self._stageView:
            # Save all the pertinent attribute values (for _toggleFramedView)
            self._storeAndReturnViewState() # ignore return val - we're stomping it
            self._stageView.updateView(True, True) # compute bbox on frame selection

    def _toggleFramedView(self):
        if self._stageView:
            self._stageView.restoreViewState(self._storeAndReturnViewState())

    def _resetSettings(self):
        """Reloads the UI and Sets up the initial settings for the
        _stageView object created in _reloadVaryingUI"""

        # Seems like a good time to clear the texture registry
        Glf.TextureRegistry.Reset()

        # RELOAD fixed and varying UI
        self._reloadFixedUI()
        self._reloadVaryingUI()

        if self._stageView:
            self._stageView.update()

        self._ui.actionFreeCam._prim = None
        self._ui.actionFreeCam.triggered.connect(
            lambda : self._cameraSelectionChanged(None))
        if self._stageView:
            self._stageView.signalSwitchedToFreeCam.connect(
                lambda : self._cameraSelectionChanged(None))

        self._refreshCameraListAndMenu(preserveCurrCamera = False)

    def _updateForStageChanges(self, hasPrimResync=True):
        """Assuming there have been authoring changes to the already-loaded
        stage, make the minimal updates to the UI required to maintain a
        consistent state.  This may still be over-zealous until we know
        what actually changed, but we should be able to preserve camera and
        playback positions (unless viewing through a stage camera that no
        longer exists"""

        self._hasPrimResync = hasPrimResync or self._hasPrimResync

        self._clearCaches(preserveCamera=True)

        # Update the UIs (it gets all of them) and StageView on a timer
        self.updateGUI()

    def _cacheViewerModeEscapeSizes(self, pos=None, index=None):
        topHeight, bottomHeight = self._ui.topBottomSplitter.sizes()
        primViewWidth, stageViewWidth = self._ui.primStageSplitter.sizes()
        if bottomHeight > 0 or primViewWidth > 0:
            self._viewerModeEscapeSizes = topHeight, bottomHeight, primViewWidth, stageViewWidth
        else:
            self._viewerModeEscapeSizes = None

    def _toggleViewerMode(self):
        topHeight, bottomHeight = self._ui.topBottomSplitter.sizes()
        primViewWidth, stageViewWidth = self._ui.primStageSplitter.sizes()
        if bottomHeight > 0 or primViewWidth > 0:
            topHeight += bottomHeight
            bottomHeight = 0
            stageViewWidth += primViewWidth
            primViewWidth = 0
        else:
            if self._viewerModeEscapeSizes is not None:
                topHeight, bottomHeight, primViewWidth, stageViewWidth = self._viewerModeEscapeSizes
            else:
                bottomHeight = UIDefaults.BOTTOM_HEIGHT
                topHeight = UIDefaults.TOP_HEIGHT
                primViewWidth = UIDefaults.PRIM_VIEW_WIDTH
                stageViewWidth = UIDefaults.STAGE_VIEW_WIDTH
        self._ui.topBottomSplitter.setSizes([topHeight, bottomHeight])
        self._ui.primStageSplitter.setSizes([primViewWidth, stageViewWidth])

    def _resetView(self,selectPrim = None):
        """ Reverts the GL frame to the initial camera view,
        and clears selection (sets to pseudoRoot), UNLESS 'selectPrim' is
        not None, in which case we'll select and frame it."""
        self._ui.primView.clearSelection()
        pRoot = self._dataModel.stage.GetPseudoRoot()
        if selectPrim is None:
            # if we had a command-line specified selection, re-frame it
            selectPrim = self._initialSelectPrim or pRoot

        item = self._getItemAtPath(selectPrim.GetPath())

        # Our response to selection-change includes redrawing.  We do NOT
        # want that to happen here, since we are subsequently going to
        # change the camera framing (and redraw, again), which can cause
        # flickering.  So make sure we don't redraw!
        self._allowViewUpdates = False
        self._ui.primView.setCurrentItem(item)
        self._allowViewUpdates = True

        if self._stageView:
            if (selectPrim and selectPrim != pRoot) or not self._startingPrimCamera:
                # _frameSelection translates the camera from wherever it happens
                # to be at the time.  If we had a starting selection AND a
                # primCam, then before framing, switch back to the prim camera
                if selectPrim == self._initialSelectPrim and self._startingPrimCamera:
                    self._dataModel.viewSettings.cameraPrim = self._startingPrimCamera
                self._frameSelection()
            else:
                self._dataModel.viewSettings.cameraPrim = self._startingPrimCamera
                self._stageView.updateView()

    def _changeRenderMode(self, mode):
        self._dataModel.viewSettings.renderMode = str(mode.text())

    def _changeColorCorrection(self, mode):
        self._dataModel.viewSettings.colorCorrectionMode = str(mode.text())

    def _changePickMode(self, mode):
        self._dataModel.viewSettings.pickMode = str(mode.text())

    def _changeSelHighlightMode(self, mode):
        self._dataModel.viewSettings.selHighlightMode = str(mode.text())

    def _changeHighlightColor(self, color):
        self._dataModel.viewSettings.highlightColorName = str(color.text())

    def _changeInterpolationType(self, interpolationType):
        for t in Usd.InterpolationType.allValues:
            if t.displayName == str(interpolationType.text()):
                self._dataModel.stage.SetInterpolationType(t)
                self._resetSettings()
                break

    def _ambientOnlyClicked(self, checked=None):
        if self._stageView and checked is not None:
            self._dataModel.viewSettings.ambientLightOnly = checked

            # If all three lights are disabled, re-enable them all.
            if (not self._dataModel.viewSettings.keyLightEnabled and not self._dataModel.viewSettings.fillLightEnabled and
                    not self._dataModel.viewSettings.backLightEnabled):
                self._dataModel.viewSettings.keyLightEnabled = True
                self._dataModel.viewSettings.fillLightEnabled = True
                self._dataModel.viewSettings.backLightEnabled = True

    def _onKeyLightClicked(self, checked=None):
        if self._stageView and checked is not None:
            self._dataModel.viewSettings.keyLightEnabled = checked

    def _onFillLightClicked(self, checked=None):
        if self._stageView and checked is not None:
            self._dataModel.viewSettings.fillLightEnabled = checked

    def _onBackLightClicked(self, checked=None):
        if self._stageView and checked is not None:
            self._dataModel.viewSettings.backLightEnabled = checked

    def _changeBgColor(self, mode):
        self._dataModel.viewSettings.clearColorText = str(mode.text())

    def _toggleShowBBoxPlayback(self):
        """Called when the menu item for showing BBoxes
        during playback is activated or deactivated."""
        self._dataModel.viewSettings.showBBoxPlayback = (
            self._ui.showBBoxPlayback.isChecked())

    def _toggleAutoComputeClippingPlanes(self):
        autoClip = self._ui.actionAuto_Compute_Clipping_Planes.isChecked()
        self._dataModel.viewSettings.autoComputeClippingPlanes = autoClip
        if autoClip:
            self._stageView.detachAndReClipFromCurrentCamera()
        

    def _setUseExtentsHint(self):
        self._dataModel.useExtentsHint = self._ui.useExtentsHint.isChecked()

        self._updatePropertyView()

        #recompute and display bbox
        self._refreshBBox()

    def _toggleShowBBoxes(self):
        """Called when the menu item for showing BBoxes
        is activated."""
        self._dataModel.viewSettings.showBBoxes = self._ui.showBBoxes.isChecked()
        #recompute and display bbox
        self._refreshBBox()

    def _toggleShowAABBox(self):
        """Called when Axis-Aligned bounding boxes
        are activated/deactivated via menu item"""
        self._dataModel.viewSettings.showAABBox = self._ui.showAABBox.isChecked()
        # recompute and display bbox
        self._refreshBBox()

    def _toggleShowOBBox(self):
        """Called when Oriented bounding boxes
        are activated/deactivated via menu item"""
        self._dataModel.viewSettings.showOBBox = self._ui.showOBBox.isChecked()
        # recompute and display bbox
        self._refreshBBox()

    def _refreshBBox(self):
        """Recompute and hide/show Bounding Box."""
        if self._stageView:
            self._stageView.updateView(forceComputeBBox=True)

    def _toggleDisplayGuide(self):
        self._dataModel.viewSettings.displayGuide = (
            self._ui.actionDisplay_Guide.isChecked())

    def _toggleDisplayProxy(self):
        self._dataModel.viewSettings.displayProxy = (
            self._ui.actionDisplay_Proxy.isChecked())

    def _toggleDisplayRender(self):
        self._dataModel.viewSettings.displayRender = (
            self._ui.actionDisplay_Render.isChecked())

    def _toggleDisplayCameraOracles(self):
        self._dataModel.viewSettings.displayCameraOracles = (
            self._ui.actionDisplay_Camera_Oracles.isChecked())

    def _toggleDisplayPrimId(self):
        self._dataModel.viewSettings.displayPrimId = (
            self._ui.actionDisplay_PrimId.isChecked())

    def _toggleEnableSceneMaterials(self):
        self._dataModel.viewSettings.enableSceneMaterials = (
            self._ui.actionEnable_Scene_Materials.isChecked())

    def _toggleCullBackfaces(self):
        self._dataModel.viewSettings.cullBackfaces = (
            self._ui.actionCull_Backfaces.isChecked())

    def _showInterpreter(self):
        if self._interpreter is None:
            self._interpreter = QtWidgets.QDialog(self._mainWindow)
            self._interpreter.setObjectName("Interpreter")
            self._console = Myconsole(self._interpreter, self._usdviewApi)
            self._interpreter.setFocusProxy(self._console) # this is important!
            lay = QtWidgets.QVBoxLayout()
            lay.addWidget(self._console)
            self._interpreter.setLayout(lay)

        # dock the interpreter window next to the main usdview window
        self._interpreter.move(self._mainWindow.x() + self._mainWindow.frameGeometry().width(),
                               self._mainWindow.y())
        self._interpreter.resize(600, self._mainWindow.size().height()/2)

        self._interpreter.show()
        self._interpreter.activateWindow()
        self._interpreter.setFocus()

    def _showDebugFlags(self):
        if self._debugFlagsWindow is None:
            from debugFlagsWidget import DebugFlagsWidget
            self._debugFlagsWindow = DebugFlagsWidget()

        self._debugFlagsWindow.show()

    # Screen capture functionality ===========================================

    def GrabWindowShot(self):
        '''Returns a QImage of the full usdview window '''
        # generate an image of the window. Due to how Qt's rendering
        # works, this will not pick up the GL Widget(_stageView)'s
        # contents, and we'll need to compose it separately.
        windowShot = QtGui.QImage(self._mainWindow.size(),
                                  QtGui.QImage.Format_ARGB32_Premultiplied)
        painter = QtGui.QPainter(windowShot)
        self._mainWindow.render(painter, QtCore.QPoint())

        if self._stageView:
            # overlay the QGLWidget on and return the composed image
            # we offset by a single point here because of Qt.Pos funkyness
            offset = QtCore.QPoint(0,1)
            pos = self._stageView.mapTo(self._mainWindow, self._stageView.pos()) - offset
            painter.drawImage(pos, self.GrabViewportShot())

        return windowShot

    def GrabViewportShot(self):
        '''Returns a QImage of the current stage view in usdview.'''
        if self._stageView:
            return self._stageView.grabFrameBuffer()
        else:
            return None

    # File handling functionality =============================================

    def _cleanAndClose(self):

        self._settings2.save()

        # If the current path widget is focused when closing usdview, it can
        # trigger an "editingFinished()" signal, which will look for a prim in
        # the scene (which is already deleted). This prevents that.

        # XXX:
        # This method is reentrant and calling disconnect twice on a signal
        # causes an exception to be thrown.
        try:
            self._ui.currentPathWidget.editingFinished.disconnect(
                self._currentPathChanged)
        except RuntimeError:
            pass

        # Shut down some timers and our eventFilter
        self._primViewUpdateTimer.stop()
        self._guiResetTimer.stop()
        QtWidgets.QApplication.instance().removeEventFilter(self._filterObj)
        
        # If the timer is currently active, stop it from being invoked while
        # the USD stage is being torn down.
        if self._timer.isActive():
            self._timer.stop()

        # Close the stage.
        self._closeStage()

        # Tear down the UI window.
        with Timer() as t:
            self._mainWindow.close()
        if self._printTiming:
            t.PrintTime('tear down the UI')

    def _openFile(self):
        extensions = Sdf.FileFormat.FindAllFileFormatExtensions()
        builtInFiles = lambda f: f.startswith(".usd")
        notBuiltInFiles = lambda f: not f.startswith(".usd")
        extensions = filter(builtInFiles, extensions) + filter(notBuiltInFiles, extensions)
        fileFilter = "USD Compatible Files (" + " ".join("*." + e for e in extensions) + ")" 
        (filename, _) = QtWidgets.QFileDialog.getOpenFileName(
            self._mainWindow,
            caption="Select file",
            dir=".",
            filter=fileFilter,
            selectedFilter=fileFilter)

        if len(filename) > 0:
            self._parserData.usdFile = str(filename)
            self._mainWindow.setWindowTitle(filename)
            self._reopenStage()

    def _getSaveFileName(self, caption, recommendedFilename):
        (saveName, _) = QtWidgets.QFileDialog.getSaveFileName(
            self._mainWindow,
            caption,
            './' + recommendedFilename,
            'USD Files (*.usd)'
            ';;USD ASCII Files (*.usda)'
            ';;USD Crate Files (*.usdc)'
            ';;Any USD File (*.usd *.usda *.usdc)',
            'Any USD File (*.usd *.usda *.usdc)')

        if len(saveName) == 0:
            return ''

        _, ext = os.path.splitext(saveName)
        if ext not in ('.usd', '.usda', '.usdc'):
            saveName += '.usd'
        
        return saveName

    def _saveOverridesAs(self):
        recommendedFilename = self._parserData.usdFile.rsplit('.', 1)[0]
        recommendedFilename += '_overrides.usd'

        saveName = self._getSaveFileName(
            'Save Overrides As', recommendedFilename)
        if len(saveName) == 0:
            return

        if not self._dataModel.stage:
            return

        with BusyContext():
            # In the future, we may allow usdview to be brought up with no file,
            # in which case it would create an in-memory root layer, to which
            # all edits will be targeted.  In order to future proof
            # this, first fetch the root layer, and if it is anonymous, just
            # export it to the given filename. If it isn't anonmyous (i.e., it
            # is a regular usd file on disk), export the session layer and add
            # the stage root file as a sublayer.
            rootLayer = self._dataModel.stage.GetRootLayer()
            if not rootLayer.anonymous:
                self._dataModel.stage.GetSessionLayer().Export(
                    saveName, 'Created by UsdView')
                targetLayer = Sdf.Layer.FindOrOpen(saveName)
                UsdUtils.CopyLayerMetadata(rootLayer, targetLayer,
                                           skipSublayers=True)

                # We don't ever store self.realStartTimeCode or
                # self.realEndTimeCode in a layer, so we need to author them
                # here explicitly.
                if self.realStartTimeCode:
                    targetLayer.startTimeCode = self.realStartTimeCode
                if self.realEndTimeCode:
                    targetLayer.endTimeCode = self.realEndTimeCode

                targetLayer.subLayerPaths.append(
                    self._dataModel.stage.GetRootLayer().realPath)
                targetLayer.RemoveInertSceneDescription()
                targetLayer.Save()
            else:
                self._dataModel.stage.GetRootLayer().Export(
                    saveName, 'Created by UsdView')

    def _saveFlattenedAs(self):
        recommendedFilename = self._parserData.usdFile.rsplit('.', 1)[0]
        recommendedFilename += '_flattened.usd'

        saveName = self._getSaveFileName(
            'Save Flattened As', recommendedFilename)
        if len(saveName) == 0:
            return

        with BusyContext():
            self._dataModel.stage.Export(saveName)

    def _reopenStage(self):
        QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.BusyCursor)

        try:
            # Pause the stage view while we update
            self._stageView.setUpdatesEnabled(False)

            # Clear out any Usd objects that may become invalid.
            self._dataModel.selection.clear()
            self._currentSpec = None
            self._currentLayer = None

            # Close the current stage so that we don't keep it in memory
            # while trying to open another stage.
            self._closeStage()
            stage = self._openStage(
                self._parserData.usdFile, self._parserData.sessionLayer,
                self._parserData.populationMask)
            # We need this for layers which were cached in memory but changed on
            # disk. The additional Reload call should be cheap when nothing
            # actually changed.
            stage.Reload()

            self._dataModel.stage = stage

            self._resetSettings()
            self._resetView()

            self._stepSizeChanged()
            self._stepSizeChanged()
            self._stageView.setUpdatesEnabled(True)
        except Exception as err:
            self.statusMessage('Error occurred reopening Stage: %s' % err)
            traceback.print_exc()
        finally:
            QtWidgets.QApplication.restoreOverrideCursor()

        self.statusMessage('Stage Reopened')

    def _reloadStage(self):
        QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.BusyCursor)

        try:
            self._dataModel.stage.Reload()
            # Seems like a good time to clear the texture registry
            Glf.TextureRegistry.Reset()
            # reset timeline, and playback settings from stage metadata
            self._reloadFixedUI(resetStageDataOnly=True)
        except Exception as err:
            self.statusMessage('Error occurred rereading all layers for Stage: %s' % err)
        finally:
            QtWidgets.QApplication.restoreOverrideCursor()

        self.statusMessage('All Layers Reloaded.')

    def _cameraSelectionChanged(self, camera):
        self._dataModel.viewSettings.cameraPrim = camera

    def _refreshCameraListAndMenu(self, preserveCurrCamera):
        self._allSceneCameras = Utils._GetAllPrimsOfType(
            self._dataModel.stage, Tf.Type.Find(UsdGeom.Camera))
        currCamera = self._startingPrimCamera
        if self._stageView:
            currCamera = self._dataModel.viewSettings.cameraPrim
            self._stageView.allSceneCameras = self._allSceneCameras
            # if the stageView is holding an expired camera, clear it first
            # and force search for a new one
            if currCamera != None and not (currCamera and currCamera.IsActive()):
                currCamera = None
                self._dataModel.viewSettings.cameraPrim = None
                preserveCurrCamera = False

        if not preserveCurrCamera:
            cameraWasSet = False
            def setCamera(camera):
                self._startingPrimCamera = currCamera = camera
                self._dataModel.viewSettings.cameraPrim = camera
                cameraWasSet = True

            if self._startingPrimCameraPath:
                prim = self._dataModel.stage.GetPrimAtPath(
                    self._startingPrimCameraPath)
                if not prim.IsValid():
                    msg = sys.stderr
                    print >> msg, "WARNING: Camera path %r did not exist in " \
                                  "stage" % (str(self._startingPrimCameraPath),)
                    self._startingPrimCameraPath = None
                elif not prim.IsA(UsdGeom.Camera):
                    msg = sys.stderr
                    print >> msg, "WARNING: Camera path %r was not a " \
                                  "UsdGeom.Camera" % \
                                  (str(self._startingPrimCameraPath),)
                    self._startingPrimCameraPath = None
                else:
                    setCamera(prim)

            if not cameraWasSet and self._startingPrimCameraName:
                for camera in self._allSceneCameras:
                    if camera.GetName() == self._startingPrimCameraName:
                        setCamera(camera)
                        break

        # Now that we have the current camera and all cameras, build the menu
        self._ui.menuCamera.clear()
        if len(self._allSceneCameras) == 0:
            self._ui.menuCamera.setEnabled(False)
        else:
            self._ui.menuCamera.setEnabled(True)
            currCameraPath = None
            if currCamera:
                currCameraPath = currCamera.GetPath()
            for camera in self._allSceneCameras:
                action = self._ui.menuCamera.addAction(camera.GetName())
                action.setData(camera.GetPath())
                action.setToolTip(str(camera.GetPath()))
                action.setCheckable(True)

                action.triggered[bool].connect(
                    lambda _, cam = camera: self._cameraSelectionChanged(cam))
                action.setChecked(action.data() == currCameraPath)

    def _updatePropertiesFromPropertyView(self):
        """Update the data model's property selection to match property view's
        current selection.
        """

        selectedProperties = dict()
        for item in self._ui.propertyView.selectedItems():
            # We define data 'roles' in the property viewer to distinguish between things
            # like attributes and attributes with connections, relationships and relationships
            # with targets etc etc.
            role = item.data(PropertyViewIndex.TYPE, QtCore.Qt.ItemDataRole.WhatsThisRole)
            if role in (PropertyViewDataRoles.CONNECTION, PropertyViewDataRoles.TARGET):

                # Get the owning property's set of selected targets.
                propName = str(item.parent().text(PropertyViewIndex.NAME))
                prop = self._propertiesDict[propName]
                targets = selectedProperties.setdefault(prop, set())

                # Add the target to the set of targets.
                targetPath = Sdf.Path(str(item.text(PropertyViewIndex.NAME)))
                if role == PropertyViewDataRoles.CONNECTION:
                    prim = self._dataModel.stage.GetPrimAtPath(
                        targetPath.GetPrimPath())
                    target = prim.GetProperty(targetPath.name)
                else: # role == PropertyViewDataRoles.TARGET
                    target = self._dataModel.stage.GetPrimAtPath(
                        targetPath)
                targets.add(target)

            else:

                propName = str(item.text(PropertyViewIndex.NAME))
                prop = self._propertiesDict[propName]
                selectedProperties.setdefault(prop, set())

        with self._dataModel.selection.batchPropChanges:
            self._dataModel.selection.clearProps()
            for prop, targets in selectedProperties.items():
                if not isinstance(prop, CustomAttribute):
                    self._dataModel.selection.addProp(prop)
                    for target in targets:
                        self._dataModel.selection.addPropTarget(prop, target)

        with self._dataModel.selection.batchComputedPropChanges:
            self._dataModel.selection.clearComputedProps()
            for prop, targets in selectedProperties.items():
                if isinstance(prop, CustomAttribute):
                    self._dataModel.selection.addComputedProp(prop)

    def _propertyViewSelectionChanged(self):
        """Called whenever property view's selection changes."""

        if self._propertyViewSelectionBlocker.blocked():
            return

        self._updatePropertiesFromPropertyView()

    def _propertyViewCurrentItemChanged(self, currentItem, lastItem):

        """Called whenever property view's current item changes."""
        if self._propertyViewSelectionBlocker.blocked():
            return

        # If a selected item becomes the current item, it will not fire a
        # selection changed signal but we still want to change the property
        # selection.
        if currentItem is not None and currentItem.isSelected():
            self._updatePropertiesFromPropertyView()

    def _propSelectionChanged(self):
        """Called whenever the property selection in the data model changes.
        Updates any UI that relies on the selection state.
        """
        self._updatePropertyViewSelection()
        self._populatePropertyInspector()
        self._updatePropertyInspector()

    def _populatePropertyInspector(self):

        focusPrimPath = None
        focusPropName = None

        focusProp = self._dataModel.selection.getFocusProp()
        if focusProp is None:
            focusPrimPath, focusPropName = (
                self._dataModel.selection.getFocusComputedPropPath())
        else:
            focusPrimPath = focusProp.GetPrimPath()
            focusPropName = focusProp.GetName()

        if focusPropName is not None:
            # inform the value editor that we selected a new attribute
            self._ui.attributeValueEditor.populate(focusPrimPath, focusPropName)
        else:
            self._ui.attributeValueEditor.clear()

    def _onCompositionSelectionChanged(self, curr=None, prev=None):
        self._currentSpec = getattr(curr, 'spec', None)
        self._currentLayer = getattr(curr, 'layer', None)

    def _updatePropertyInspector(self, index=None, obj=None):
        # index must be the first parameter since this method is used as
        # propertyInspector tab widget's currentChanged(int) signal callback
        if index is None:
            index = self._ui.propertyInspector.currentIndex()

        if obj is None:
            obj = self._getSelectedObject()

        if index == PropertyIndex.METADATA:
            self._updateMetadataView(obj)
        elif index == PropertyIndex.LAYERSTACK:
            self._updateLayerStackView(obj)
        elif index == PropertyIndex.COMPOSITION:
            self._updateCompositionView(obj)

    def _refreshAttributeValue(self):
        self._ui.attributeValueEditor.refresh()

    def _propertyViewContextMenu(self, point):
        item = self._ui.propertyView.itemAt(point)
        if item:
            self.contextMenu = AttributeViewContextMenu(self._mainWindow, 
                                                        item, self._dataModel)
            self.contextMenu.exec_(QtGui.QCursor.pos())

    def _layerStackContextMenu(self, point):
        item = self._ui.layerStackView.itemAt(point)
        if item:
            self.contextMenu = LayerStackContextMenu(self._mainWindow, item)
            self.contextMenu.exec_(QtGui.QCursor.pos())

    def _compositionTreeContextMenu(self, point):
        item = self._ui.compositionTreeWidget.itemAt(point)
        self.contextMenu = LayerStackContextMenu(self._mainWindow, item)
        self.contextMenu.exec_(QtGui.QCursor.pos())

    # Headers & Columns =================================================
    def _propertyViewHeaderContextMenu(self, point):
        self.contextMenu = HeaderContextMenu(self._ui.propertyView)
        self.contextMenu.exec_(QtGui.QCursor.pos())

    def _primViewHeaderContextMenu(self, point):
        self.contextMenu = HeaderContextMenu(self._ui.primView)
        self.contextMenu.exec_(QtGui.QCursor.pos())


    # Widget management =================================================

    def _changePrimViewDepth(self, action):
        """Signal handler for view-depth menu items
        """
        actionTxt = str(action.text())
        # recover the depth factor from the action's name
        depth = int(actionTxt[actionTxt.find(" ")+1])
        self._expandToDepth(depth)

    def _expandToDepth(self, depth, suppressTiming=False):
        """Expands treeview prims to the given depth
        """
        with Timer() as t, BusyContext():
            # Populate items down to depth.  Qt will expand items at depth
            # depth-1 so we need to have items at depth.  We know something
            # changed if any items were added to _itemsToPush.
            n = len(self._itemsToPush)
            self._populateItem(self._dataModel.stage.GetPseudoRoot(),
                maxDepth=depth)
            changed = (n != len(self._itemsToPush))

            # Expand the tree to depth.
            self._ui.primView.expandToDepth(depth-1)
            if changed:
                # Resize column.
                self._scheduleResizePrimView()

                # Start pushing prim data to the UI during idle cycles.
                # Qt doesn't need the data unless the item is actually
                # visible (or affects what's visible) but to avoid
                # jerky scrolling when that data is pulled during the
                # scroll, we can do it ahead of time.  But don't do it
                # if we're currently playing to maximize playback
                # performance.
                if not self._dataModel.playing:
                    self._primViewUpdateTimer.start()

        if self._printTiming and not suppressTiming:
            t.PrintTime("expand Prim browser to depth %d" % depth)

    def _primViewExpanded(self, index):
        """Signal handler for expanded(index), facilitates lazy tree population
        """
        self._populateChildren(self._ui.primView.itemFromIndex(index))
        self._scheduleResizePrimView()

    def _toggleShowInactivePrims(self):
        self._dataModel.viewSettings.showInactivePrims = (
            self._ui.actionShow_Inactive_Prims.isChecked())
        # Note: _toggleShowInactivePrims, _toggleShowMasterPrims,
        #       _toggleShowUndefinedPrims, and _toggleShowAbstractPrims all call
        #       _resetPrimView after being toggled, but only from menu items.
        #       In the future, we should do this when a signal from
        #       ViewSettingsDataModel is emitted so the prim view always updates
        #       when they are changed.
        self._dataModel.selection.removeInactivePrims()
        self._resetPrimView()

    def _toggleShowMasterPrims(self):
        self._dataModel.viewSettings.showAllMasterPrims = (
            self._ui.actionShow_All_Master_Prims.isChecked())
        self._dataModel.selection.removeMasterPrims()
        self._resetPrimView()

    def _toggleShowUndefinedPrims(self):
        self._dataModel.viewSettings.showUndefinedPrims = (
            self._ui.actionShow_Undefined_Prims.isChecked())
        self._dataModel.selection.removeUndefinedPrims()
        self._resetPrimView()

    def _toggleShowAbstractPrims(self):
        self._dataModel.viewSettings.showAbstractPrims = (
            self._ui.actionShow_Abstract_Prims.isChecked())
        self._dataModel.selection.removeAbstractPrims()
        self._resetPrimView()

    def _toggleRolloverPrimInfo(self):
        self._dataModel.viewSettings.rolloverPrimInfo = (
            self._ui.actionRollover_Prim_Info.isChecked())
        if self._stageView:
            self._stageView.rolloverPicking = self._dataModel.viewSettings.rolloverPrimInfo

    def _tallyPrimStats(self, prim):
        def _GetType(prim):
            typeString = prim.GetTypeName()
            return HUDEntries.NOTYPE if not typeString else typeString

        childTypeDict = {}
        primCount = 0

        for child in Usd.PrimRange(prim):
            typeString = _GetType(child)
            # skip pseudoroot
            if typeString is HUDEntries.NOTYPE and not prim.GetParent():
                continue
            primCount += 1
            childTypeDict[typeString] = 1 + childTypeDict.get(typeString, 0)

        return (primCount, childTypeDict)

    def _populateChildren(self, item, depth=0, maxDepth=1, childrenToAdd=None):
        """Populates the children of the given item in the prim viewer.
           If childrenToAdd is given its a list of prims to add as
           children."""
        if depth < maxDepth and item.prim.IsActive():
            if item.needsChildrenPopulated() or childrenToAdd:
                # Populate all the children.
                if not childrenToAdd:
                    childrenToAdd = self._getFilteredChildren(item.prim)
                item.addChildren([self._populateItem(child, depth+1, maxDepth)
                                                    for child in childrenToAdd])
            elif depth + 1 < maxDepth:
                # The children already exist but we're recursing deeper.
                for i in xrange(item.childCount()):
                    self._populateChildren(item.child(i), depth+1, maxDepth)

    def _populateItem(self, prim, depth=0, maxDepth=0):
        """Populates a prim viewer item."""
        item = self._primToItemMap.get(prim)
        if not item:
            # Create a new item.  If we want its children we obviously
            # have to create those too.
            children = self._getFilteredChildren(prim)
            item = PrimViewItem(prim, self, len(children) != 0)
            self._primToItemMap[prim] = item
            self._populateChildren(item, depth, maxDepth, children)
            # Push the item after the children so ancestors are processed
            # before descendants.
            self._itemsToPush.append(item)
        else:
            # Item already exists.  Its children may or may not exist.
            # Either way, we need to have them to get grandchildren.
            self._populateChildren(item, depth, maxDepth)
        return item

    def _populateRoots(self):
        invisibleRootItem = self._ui.primView.invisibleRootItem()
        rootPrim = self._dataModel.stage.GetPseudoRoot()
        rootItem = self._populateItem(rootPrim)
        self._populateChildren(rootItem)

        if self._dataModel.viewSettings.showAllMasterPrims:
            self._populateChildren(rootItem,
                childrenToAdd=self._dataModel.stage.GetMasters())

        # Add all descendents all at once.
        invisibleRootItem.addChild(rootItem)

    def _getFilteredChildren(self, prim):
        return prim.GetFilteredChildren(self._displayPredicate)

    def _computeDisplayPredicate(self):
        # Take current browser filtering into account when discovering
        # prims while traversing

        self._displayPredicate = None

        if not self._dataModel.viewSettings.showInactivePrims:
            self._displayPredicate = Usd.PrimIsActive \
                if self._displayPredicate is None \
                else self._displayPredicate & Usd.PrimIsActive
        if not self._dataModel.viewSettings.showUndefinedPrims:
            self._displayPredicate = Usd.PrimIsDefined \
                if self._displayPredicate is None \
                else self._displayPredicate & Usd.PrimIsDefined
        if not self._dataModel.viewSettings.showAbstractPrims:
            self._displayPredicate = ~Usd.PrimIsAbstract \
                if self._displayPredicate is None \
                else self._displayPredicate & ~Usd.PrimIsAbstract
        if self._displayPredicate is None:
            self._displayPredicate = Usd._PrimFlagsPredicate.Tautology()

        # Unless user experience indicates otherwise, we think we always
        # want to show instance proxies
        self._displayPredicate = Usd.TraverseInstanceProxies(self._displayPredicate)


    def _getItemAtPath(self, path, ensureExpanded=False):
        # If the prim hasn't been expanded yet, drill down into it.
        # Note the explicit str(path) in the following expr is necessary
        # because path may be a QString.
        path = path if isinstance(path, Sdf.Path) else Sdf.Path(str(path))
        parent = self._dataModel.stage.GetPrimAtPath(path)
        if not parent:
            raise RuntimeError("Prim not found at path in stage: %s" % str(path))
        pseudoRoot = self._dataModel.stage.GetPseudoRoot()
        if parent not in self._primToItemMap:
            # find the first loaded parent
            childList = []

            while parent != pseudoRoot \
                        and not parent in self._primToItemMap:
                childList.append(parent)
                parent = parent.GetParent()

            # go one step further, since the first item found could be hidden
            # under a norgie and we would want to populate its siblings as well
            if parent != pseudoRoot:
                childList.append(parent)

            # now populate down to the child
            for parent in reversed(childList):
                try:
                    item = self._primToItemMap[parent]
                    self._populateChildren(item)
                    if ensureExpanded:
                        item.setExpanded(True)
                except:
                    item = None

        # finally, return the requested item, which now should be in
        # the map. If something has been added, this can fail. Not
        # sure how to rebuild or add this to the map in a minimal way,
        # but after the first hiccup, I don't see any ill
        # effects. Would love to know a better way... 
        # - wave 04.17.2018
        prim = self._dataModel.stage.GetPrimAtPath(path)
        try:
            item = self._primToItemMap[prim]
        except:
            item = None
        return item

    def selectPseudoroot(self):
        """Selects only the pseudoroot."""
        self._dataModel.selection.clearPrims()

    def selectEnclosingModel(self):
        """Iterates through all selected prims, selecting their containing model
        instead if they are not a model themselves.
        """
        oldPrims = self._dataModel.selection.getPrims()

        with self._dataModel.selection.batchPrimChanges:
            self._dataModel.selection.clearPrims()
            for prim in oldPrims:
                model = GetEnclosingModelPrim(prim)
                if model:
                    self._dataModel.selection.addPrim(model)
                else:
                    self._dataModel.selection.addPrim(prim)

    def selectBoundMaterialForPurpose(self, materialPurpose):
        """Iterates through all selected prims, selecting their bound preview
           materials.
        """
        oldPrims = self._dataModel.selection.getPrims()
        with self._dataModel.selection.batchPrimChanges:
            self._dataModel.selection.clearPrims()
            for prim in oldPrims:
                (boundMaterial, bindingRel) = \
                    UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                        materialPurpose=materialPurpose)
                if boundMaterial:
                    self._dataModel.selection.addPrim(boundMaterial.GetPrim())

    def selectBindingRelForPurpose(self, materialPurpose):
        """Iterates through all selected prims, selecting their bound preview
           materials.
        """
        relsToSelect = []
        oldPrims = self._dataModel.selection.getPrims()
        with self._dataModel.selection.batchPrimChanges:
            self._dataModel.selection.clearPrims()
            for prim in oldPrims:
                (boundMaterial, bindingRel) = \
                    UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                        materialPurpose=materialPurpose)
                if boundMaterial and bindingRel:
                    self._dataModel.selection.addPrim(bindingRel.GetPrim())
                    relsToSelect.append(bindingRel)

        with self._dataModel.selection.batchPropChanges:
            self._dataModel.selection.clearProps()
            for rel in relsToSelect:
                self._dataModel.selection.addProp(rel)

    def selectBoundPreviewMaterial(self):
        """Iterates through all selected prims, selecting their bound preview
           materials.
        """
        self.selectBoundMaterialForPurpose(
            materialPurpose=UsdShade.Tokens.preview)

    def selectBoundFullMaterial(self):
        """Iterates through all selected prims, selecting their bound preview
           materials.
        """
        self.selectBoundMaterialForPurpose(
            materialPurpose=UsdShade.Tokens.full)

    def selectPreviewBindingRel(self):
        """Iterates through all selected prims, computing their resolved 
        "preview" bindings and selecting the cooresponding binding relationship.
        """
        self.selectBindingRelForPurpose(materialPurpose=UsdShade.Tokens.preview)

    def selectFullBindingRel(self):
        """Iterates through all selected prims, computing their resolved 
        "full" bindings and selecting the cooresponding binding relationship.
        """
        self.selectBindingRelForPurpose(materialPurpose=UsdShade.Tokens.full)    

    def _getCommonPrims(self, pathsList):
        commonPrefix = os.path.commonprefix(pathsList)
        ### To prevent /Canopies/TwigA and /Canopies/TwigB
        ### from registering /Canopies/Twig as prefix
        return commonPrefix.rsplit('/', 1)[0]


    def _primSelectionChanged(self, added, removed):
        """Called when the prim selection is updated in the data model. Updates
        any UI that depends on the state of the selection.
        """

        with self._primViewSelectionBlocker:
            self._updatePrimViewSelection(added, removed)
        self._updatePrimPathText()
        if self._stageView:
            self._updateHUDPrimStats()
            self._updateHUDGeomCounts()
            self._stageView.updateView()
        self._updatePropertyInspector(
            obj=self._dataModel.selection.getFocusPrim())
        self._updatePropertyView()
        self._refreshAttributeValue()

    def _getPrimsFromPaths(self, paths):
        """Get all prims from a list of paths."""

        prims = []
        for path in paths:

            # Ensure we have an Sdf.Path, not a string.
            sdfPath = Sdf.Path(str(path))

            prim = self._dataModel.stage.GetPrimAtPath(
                sdfPath.GetAbsoluteRootOrPrimPath())
            if not prim:
                raise PrimNotFoundException(sdfPath)

            prims.append(prim)

        return prims

    def _updatePrimPathText(self):
        self._ui.currentPathWidget.setText(
            ', '.join([str(prim.GetPath())
                for prim in self._dataModel.selection.getPrims()]))

    def _currentPathChanged(self):
        """Called when the currentPathWidget text is changed"""
        newPaths = self._ui.currentPathWidget.text()
        pathList = re.split(", ?", newPaths)
        pathList = filter(lambda path: len(path) != 0, pathList)

        try:
            prims = self._getPrimsFromPaths(pathList)
        except PrimNotFoundException as ex:
            # _getPrimsFromPaths couldn't find one of the prims
            sys.stderr.write("ERROR: %s\n" % ex.message)
            self._updatePrimPathText()
            return

        explicitProps = any(Sdf.Path(str(path)).IsPropertyPath()
            for path in pathList)

        if len(prims) == 1 and not explicitProps:
            self._dataModel.selection.switchToPrimPath(prims[0].GetPath())
        else:
            with self._dataModel.selection.batchPrimChanges:
                self._dataModel.selection.clearPrims()
                for prim in prims:
                    self._dataModel.selection.addPrim(prim)

            with self._dataModel.selection.batchPropChanges:
                self._dataModel.selection.clearProps()
                for path, prim in zip(pathList, prims):
                    sdfPath = Sdf.Path(str(path))
                    if sdfPath.IsPropertyPath():
                        self._dataModel.selection.addPropPath(path)

            self._dataModel.selection.clearComputedProps()

    def _refreshPrimViewSelection(self):
        """Refresh the selected prim view items to match the selection data
        model.
        """
        self._ui.primView.clearSelection()
        selectedItems = [
            self._getItemAtPath(prim.GetPath(), ensureExpanded=True)
            for prim in self._dataModel.selection.getPrims()]
        if len(selectedItems) > 0:
            self._ui.primView.setCurrentItem(selectedItems[0])
        self._ui.primView.updateSelection(selectedItems, [])

    def _updatePrimViewSelection(self, added, removed):
        """Do an incremental update to primView's selection using the added and
        removed prim paths from the selectionDataModel.
        """
        addedItems = [ 
            self._getItemAtPath(path, ensureExpanded=True) 
            for path in added ]
        removedItems = [ self._getItemAtPath(path) for path in removed ]
        self._ui.primView.updateSelection(addedItems, removedItems)

    def _primsFromSelectionRanges(self, ranges):
        """Iterate over all prims in a QItemSelection from primView."""
        for itemRange in ranges:
            for index in itemRange.indexes():
                if index.column() == 0:
                    item = self._ui.primView.itemFromIndex(index)
                    yield item.prim

    def _selectionChanged(self, added, removed):
        """Called when primView's selection is changed. If the selection was
        changed by a user, update the selection data model with the changes.
        """
        if self._primViewSelectionBlocker.blocked():
            return

        items = self._ui.primView.selectedItems()
        if len(items) == 1:
            self._dataModel.selection.switchToPrimPath(items[0].prim.GetPath())
        else:
            with self._dataModel.selection.batchPrimChanges:
                for prim in self._primsFromSelectionRanges(added):
                    self._dataModel.selection.addPrim(prim)
                for prim in self._primsFromSelectionRanges(removed):
                    self._dataModel.selection.removePrim(prim)

    def _itemClicked(self, item, col):
        # If user clicked in a selected row, we will toggle all selected items;
        # otherwise, just the clicked one.
        if col == PrimViewColumnIndex.VIS:
            itemsToToggle = [ item ]
            if item.isSelected():
                itemsToToggle =  [
                    self._getItemAtPath(prim.GetPath(), ensureExpanded=True)
                    for prim in self._dataModel.selection.getPrims()]
            changedAny = False
            with Timer() as t:
                for toToggle in itemsToToggle:
                    # toggleVis() returns True if the click caused a visibility
                    # change.
                    changedOne = toToggle.toggleVis()
                    if changedOne:
                        PrimViewItem.propagateVis(toToggle)
                        changedAny = True
            if changedAny:
                self.editComplete('Updated prim visibility')
            if self._printTiming:
                t.PrintTime("update vis column")
            

    def _itemPressed(self, item, col):
        if col == PrimViewColumnIndex.DRAWMODE:
            self._ui.primView.ShowDrawModeWidgetForItem(item)

    def _getPathsFromItems(self, items, prune = False):
        # this function returns a list of paths given a list of items if
        # prune=True, it excludes certain paths if a parent path is already
        # there this avoids double-rendering if both a prim and its parent
        # are selected.
        #
        # Don't include the pseudoroot, though, if it's still selected, because
        # leaving it in the pruned list will cause everything else to get
        # pruned away!
        allPaths = [itm.prim.GetPath() for itm in items]
        if not prune:
            return allPaths
        if len(allPaths) > 1:
            allPaths = [p for p in allPaths if p != Sdf.Path.absoluteRootPath]
        return Sdf.Path.RemoveDescendentPaths(allPaths)

    def _primViewContextMenu(self, point):
        item = self._ui.primView.itemAt(point)
        self._showPrimContextMenu(item)

    def _showPrimContextMenu(self, item):
        self.contextMenu = PrimContextMenu(self._mainWindow, item, self)
        self.contextMenu.exec_(QtGui.QCursor.pos())

    def setFrame(self, frame):
        """Set the `frame`.

        Args:
            frame (float): The new frame value.
        """
        frameIndex = self._findClosestFrameIndex(frame)
        self._setFrameIndex(frameIndex)

    def _setFrameIndex(self, frameIndex):
        """Set the `frameIndex`.

        Args:
            frameIndex (int): The new frame index value.
        """
        # Ensure the frameIndex exists, if not, return.
        try:
            frame = self._timeSamples[frameIndex]
        except IndexError:
            return

        currentFrame = Usd.TimeCode(frame)
        if self._dataModel.currentFrame != currentFrame:
            self._dataModel.currentFrame = currentFrame

            self._ui.frameSlider.setValue(frameIndex)

            self._updateOnFrameChange()

        self.setFrameField(self._dataModel.currentFrame.GetValue())

    def _updateGUIForFrameChange(self):
        """Called when the frame changes have finished.
        e.g When the playback/scrubbing has stopped.
        """
        # slow stuff that we do only when not playing
        # topology might have changed, recalculate
        self._updateHUDGeomCounts()
        self._updatePropertyView()
        self._refreshAttributeValue()

        # value sources of an attribute can change upon frame change
        # due to value clips, so we must update the layer stack.
        self._updateLayerStackView()

        # refresh the visibility column
        self._resetPrimViewVis(selItemsOnly=False, authoredVisHasChanged=False)

    def _updateOnFrameChange(self):
        """Called when the frame changes, updates the renderer and such"""
        # do not update HUD/BBOX if scrubbing or playing
        if not (self._dataModel.playing or self._ui.frameSlider.isSliderDown()):
            self._updateGUIForFrameChange()
        if self._stageView:
            # this is the part that renders
            if self._dataModel.playing:
                highlightMode = self._dataModel.viewSettings.selHighlightMode
                if highlightMode == SelectionHighlightModes.ALWAYS:
                    # We don't want to resend the selection to the renderer
                    # every frame during playback unless we are actually going
                    # to see the selection (which is only when highlight mode is
                    # ALWAYS).
                    self._stageView.updateSelection()
                self._stageView.updateForPlayback()
            else:
                self._stageView.updateSelection()
                self._stageView.updateView()

    def saveFrame(self, fileName):
        if self._stageView:
            pm =  QtGui.QPixmap.grabWindow(self._stageView.winId())
            pm.save(fileName, 'TIFF')

    def _getPropertiesDict(self):
        propertiesDict = OrderedDict()

        # leave attribute viewer empty if multiple prims selected
        if len(self._dataModel.selection.getPrims()) != 1:
            return propertiesDict

        prim = self._dataModel.selection.getFocusPrim()
        composed = _GetCustomAttributes(prim, self._dataModel)
        inheritedPrimvars = UsdGeom.PrimvarsAPI(prim).FindInheritablePrimvars() 

        # There may be overlap between inheritedProps and prim attributes,
        # but that's OK because propsDict will uniquify them below
        inheritedProps = [primvar.GetAttr() for primvar in inheritedPrimvars]
        props = prim.GetAttributes() + prim.GetRelationships()  + inheritedProps

        def cmpFunc(propA, propB):
            aName = propA.GetName()
            bName = propB.GetName()
            return cmp(aName.lower(), bName.lower())

        props.sort(cmp=cmpFunc)

        # Add the special composed attributes usdview generates
        # at the top of our property list.
        for prop in composed:
            propertiesDict[prop.GetName()] = prop

        for prop in props:
            propertiesDict[prop.GetName()] = prop

        return propertiesDict

    def _propertyViewDeselectItem(self, item):
        item.setSelected(False)
        for i in range(item.childCount()):
            item.child(i).setSelected(False)

    def _updatePropertyViewSelection(self):
        """Updates property view's selected items to match the data model."""

        focusPrim = self._dataModel.selection.getFocusPrim()
        propTargets = self._dataModel.selection.getPropTargets()
        computedProps = self._dataModel.selection.getComputedPropPaths()

        selectedPrimPropNames = dict()
        selectedPrimPropNames.update({prop.GetName(): targets
            for prop, targets in propTargets.items()})
        selectedPrimPropNames.update({propName: set()
            for primPath, propName in computedProps})

        rootItem = self._ui.propertyView.invisibleRootItem()

        with self._propertyViewSelectionBlocker:
            for i in range(rootItem.childCount()):
                item = rootItem.child(i)
                propName = str(item.text(PropertyViewIndex.NAME))
                if propName in selectedPrimPropNames:
                    item.setSelected(True)
                    # Select relationships and connections.
                    targets = {prop.GetPath()
                        for prop in selectedPrimPropNames[propName]}
                    for j in range(item.childCount()):
                        childItem = item.child(j)
                        targetPath = Sdf.Path(
                            str(childItem.text(PropertyViewIndex.NAME)))
                        if targetPath in targets:
                            childItem.setSelected(True)
                else:
                    self._propertyViewDeselectItem(item)

    def _updatePropertyViewInternal(self):
        frame = self._dataModel.currentFrame
        treeWidget = self._ui.propertyView
        treeWidget.setTextElideMode(QtCore.Qt.ElideMiddle)
        scrollPosition = treeWidget.verticalScrollBar().value()

        # get a dictionary of prim attribs/members and store it in self._propertiesDict
        self._propertiesDict = self._getPropertiesDict()
        with self._propertyViewSelectionBlocker:
            treeWidget.clear()
        self._populatePropertyInspector()

        curPrimSelection = self._dataModel.selection.getFocusPrim()

        currRow = 0
        for key, primProperty in self._propertiesDict.iteritems():
            targets = None
            isInheritedProperty = isinstance(primProperty, Usd.Property) and \
                (primProperty.GetPrim() != curPrimSelection)
            if type(primProperty) == Usd.Attribute:
                if primProperty.HasAuthoredConnections():
                    typeContent = PropertyViewIcons.ATTRIBUTE_WITH_CONNECTIONS()
                    typeRole = PropertyViewDataRoles.ATTRIBUTE_WITH_CONNNECTIONS
                    targets = primProperty.GetConnections()
                else:
                    typeContent = PropertyViewIcons.ATTRIBUTE()
                    typeRole = PropertyViewDataRoles.ATTRIBUTE
            elif isinstance(primProperty, ResolvedBoundMaterial):
                typeContent = PropertyViewIcons.COMPOSED()
                typeRole = PropertyViewDataRoles.RELATIONSHIP_WITH_TARGETS
            elif isinstance(primProperty, CustomAttribute):
                typeContent = PropertyViewIcons.COMPOSED()
                typeRole = PropertyViewDataRoles.COMPOSED
            elif isinstance(primProperty, Usd.Relationship):
                # Otherwise we have a relationship
                targets = primProperty.GetTargets()
                if targets:
                    typeContent = PropertyViewIcons.RELATIONSHIP_WITH_TARGETS()
                    typeRole = PropertyViewDataRoles.RELATIONSHIP_WITH_TARGETS
                else:
                    typeContent = PropertyViewIcons.RELATIONSHIP()
                    typeRole = PropertyViewDataRoles.RELATIONSHIP
            else:
                PrintWarning("Property '%s' has unknown property type <%s>." %
                    (key, type(primProperty)))
                continue

            attrText = GetShortString(primProperty, frame)
            treeWidget.addTopLevelItem(
                QtWidgets.QTreeWidgetItem(["", str(key), attrText]))

            treeWidget.topLevelItem(currRow).setIcon(PropertyViewIndex.TYPE, 
                    typeContent)
            treeWidget.topLevelItem(currRow).setData(PropertyViewIndex.TYPE,
                    QtCore.Qt.ItemDataRole.WhatsThisRole,
                    typeRole)

            currItem = treeWidget.topLevelItem(currRow)

            valTextFont = GetPropertyTextFont(primProperty, frame)
            if valTextFont:
                currItem.setFont(PropertyViewIndex.VALUE, valTextFont)
                currItem.setFont(PropertyViewIndex.NAME, valTextFont)
            else:
                currItem.setFont(PropertyViewIndex.NAME, UIFonts.BOLD)

            fgColor = GetPropertyColor(primProperty, frame)
            # Inherited properties are colored 15% darker, along with the 
            # addition of "(i)" in the type column.
            if isInheritedProperty:
                # Add "(i)" to the type column to indicate an inherited 
                # property.
                treeWidget.topLevelItem(currRow).setText(PropertyViewIndex.TYPE, 
                                                         "(i)")
                fgColor = fgColor.darker(115)
                currItem.setFont(PropertyViewIndex.TYPE, UIFonts.INHERITED)

            currItem.setForeground(PropertyViewIndex.NAME, fgColor)
            currItem.setForeground(PropertyViewIndex.VALUE, fgColor)

            if targets:
                childRow = 0
                for t in targets:
                    valTextFont = GetPropertyTextFont(primProperty, frame) or \
                            UIFonts.BOLD
                    # USD does not provide or infer values for relationship or
                    # connection targets, so we don't display them here.
                    currItem.addChild(
                            QtWidgets.QTreeWidgetItem(["", str(t), ""]))
                    currItem.setFont(PropertyViewIndex.VALUE, valTextFont)
                    child = currItem.child(childRow)

                    if typeRole == PropertyViewDataRoles.RELATIONSHIP_WITH_TARGETS:
                        child.setIcon(PropertyViewIndex.TYPE, 
                                      PropertyViewIcons.TARGET())
                        child.setData(PropertyViewIndex.TYPE,
                                      QtCore.Qt.ItemDataRole.WhatsThisRole,
                                      PropertyViewDataRoles.TARGET)
                    else:
                        child.setIcon(PropertyViewIndex.TYPE,
                                      PropertyViewIcons.CONNECTION())
                        child.setData(PropertyViewIndex.TYPE,
                                      QtCore.Qt.ItemDataRole.WhatsThisRole,
                                      PropertyViewDataRoles.CONNECTION)
                    childRow += 1

            currRow += 1

        self._updatePropertyViewSelection()

        # For some reason, resetting the scrollbar position here only works on a
        # frame change, not when the prim changes. When the prim changes, the
        # scrollbar always stays at the top of the list and setValue() has no
        # effect.
        treeWidget.verticalScrollBar().setValue(scrollPosition)

    def _updatePropertyView(self):
        """ Sets the contents of the attribute value viewer """
        cursorOverride = not self._timer.isActive()
        if cursorOverride:
            QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.BusyCursor)
        try:
            self._updatePropertyViewInternal()
        except Exception as err:
            print "Problem encountered updating attribute view: %s" % err
            raise
        finally:
            if cursorOverride:
                QtWidgets.QApplication.restoreOverrideCursor()

    def _getSelectedObject(self):
        focusPrim = self._dataModel.selection.getFocusPrim()

        attrs = self._ui.propertyView.selectedItems()
        if len(attrs) == 0:
            return focusPrim

        selectedAttribute = attrs[0]
        attrName = str(selectedAttribute.text(PropertyViewIndex.NAME))
        
        if PropTreeWidgetTypeIsRel(selectedAttribute):
            return focusPrim.GetRelationship(attrName)

        obj = focusPrim.GetAttribute(attrName)
        if not obj:
            # Check if it is an inherited primvar.
            inheritedPrimvar = UsdGeom.PrimvarsAPI(
                    focusPrim).FindPrimvarWithInheritance(attrName)
            if inheritedPrimvar:
                obj = inheritedPrimvar.GetAttr()
        return obj

    def _findIndentPos(self, s):
        for index, char in enumerate(s):
            if char != ' ':
                return index

        return len(s) - 1

    def _maxToolTipWidth(self):
        return 90

    def _maxToolTipHeight(self):
        return 32

    def _trimWidth(self, s, isList=False):
        # We special-case the display offset because list
        # items will have </li> tags embedded in them.
        offset = 10 if isList else 5

        if len(s) >= self._maxToolTipWidth():
            # For strings, well do special ellipsis behavior
            # which displays the last 5 chars with an ellipsis
            # in between. For other values, we simply display a
            # trailing ellipsis to indicate more data.
            if s[0] == '\'' and s[-1] == '\'':
                return (s[:self._maxToolTipWidth() - offset]
                        + '...'
                        + s[len(s) - offset:])
            else:
                return s[:self._maxToolTipWidth()] + '...'
        return s

    def _limitToolTipSize(self, s, isList=False):
        ttStr = ''

        lines = s.split('<br>')
        for index, line in enumerate(lines):
            if index+1 > self._maxToolTipHeight():
                break
            ttStr += self._trimWidth(line, isList)
            if not isList and index != len(lines)-1:
                ttStr += '<br>'

        if (len(lines) > self._maxToolTipHeight()):
            ellipsis = ' '*self._findIndentPos(line) + '...'
            if isList:
                ellipsis = '<li>' + ellipsis + '</li>'
            else:
                ellipsis += '<br>'

            ttStr += ellipsis
            ttStr += self._trimWidth(lines[len(lines)-2], isList)

        return ttStr

    def _addRichTextIndicators(self, s):
        # - We'll need to use html-style spaces to ensure they are respected
        # in the toolTip which uses richtext formatting.
        # - We wrap the tooltip as a paragraph to ensure &nbsp; 's are
        # respected by Qt's rendering engine.
        return '<p>' + s.replace(' ', '&nbsp;') + '</p>'

    def _limitValueDisplaySize(self, s):
        maxValueChars = 300
        return s[:maxValueChars]

    def _cleanStr(self, s, repl):
        # Remove redundant char seqs and strip newlines.
        replaced = str(s).replace('\n', repl)
        filtered = [u for (u, _) in groupby(replaced.split())]
        return ' '.join(filtered)

    def _formatMetadataValueView(self, val):
        from pprint import pformat, pprint

        valStr = self._cleanStr(val, ' ')
        ttStr  = ''
        isList = False

        # For iterable things, like VtArrays and lists, we want to print
        # a nice numbered list.
        if isinstance(val, list) or getattr(val, "_isVtArray", False):
            isList = True

            # We manually supply the index for our list elements
            # because Qt's richtext processor starts the <ol> numbering at 1.
            for index, value in enumerate(val):
                last = len(val) - 1
                trimmed = self._cleanStr(value, ' ')
                ttStr += ("<li>" + str(index) + ":  " + trimmed + "</li><br>")

        elif isinstance(val, dict):
            # We stringify all dict elements so they display more nicely.
            # For example, by default, the pprint operation would print a
            # Vt Array as Vt.Array(N, (E1, ....). By running it through
            # str(..). we'd get [(E1, E2), ....] which is more useful to
            # the end user trying to examine their data.
            for k, v in val.items():
                val[k] = str(v)

            # We'll need to strip the quotes generated by the str' operation above
            stripQuotes = lambda s: s.replace('\'', '').replace('\"', "")

            valStr = stripQuotes(self._cleanStr(val, ' '))

            formattedDict = pformat(val)
            formattedDictLines = formattedDict.split('\n')
            for index, line in enumerate(formattedDictLines):
                ttStr += (stripQuotes(line)
                    + ('' if index == len(formattedDictLines) - 1 else '<br>'))
        else:
            ttStr = self._cleanStr(val, '<br>')

        valStr = self._limitValueDisplaySize(valStr)
        ttStr = self._addRichTextIndicators(
                    self._limitToolTipSize(ttStr, isList))
        return valStr, ttStr

    def _updateMetadataView(self, obj=None):
        """ Sets the contents of the metadata viewer"""

        # XXX: this method gets called multiple times on selection, it
        # would be nice to clean that up and ensure we only update as needed.

        tableWidget = self._ui.metadataView
        self._propertiesDict = self._getPropertiesDict()

        # Setup table widget
        tableWidget.clearContents()
        tableWidget.setRowCount(0)

        if obj is None:
            obj = self._getSelectedObject()

        if not obj:
            return

        m = obj.GetAllMetadata()

        # We have to explicitly add in metadata related to composition arcs
        # and value clips here, since GetAllMetadata prunes them out.
        #
        # XXX: Would be nice to have some official facility to query
        # this.
        compKeys = [# composition related metadata
                    "references", "inheritPaths", "specializes",
                    "payload", "subLayers",

                    # non-template clip metadata
                    "clipAssetPaths", "clipTimes", "clipManifestAssetPath",
                    "clipActive", "clipPrimPath",

                    # template clip metadata
                    "clipTemplateAssetPath",
                    "clipTemplateStartTime", "clipTemplateEndTime",
                    "clipTemplateStride"]


        for k in compKeys:
            v = obj.GetMetadata(k)
            if not v is None:
                m[k] = v

        m["[object type]"] = "Attribute" if type(obj) is Usd.Attribute \
                       else "Prim" if type(obj) is Usd.Prim \
                       else "Relationship" if type(obj) is Usd.Relationship \
                       else "Unknown"
        m["[path]"] = str(obj.GetPath())

        clipMetadata = obj.GetMetadata("clips")
        if clipMetadata is None:
            clipMetadata = {}
        numClipRows = 0
        for (clip, data) in clipMetadata.items():
            numClipRows += len(data)
        m["clips"] = clipMetadata
        
        numMetadataRows = (len(m) - 1) + numClipRows

        variantSets = {}
        if (isinstance(obj, Usd.Prim)):
            variantSetNames = obj.GetVariantSets().GetNames()
            for variantSetName in variantSetNames:
                variantSet = obj.GetVariantSet(variantSetName)
                variantNames = variantSet.GetVariantNames()
                variantSelection = variantSet.GetVariantSelection()
                combo = VariantComboBox(None, obj, variantSetName, self._mainWindow)
                # First index is always empty to indicate no (or invalid)
                # variant selection.
                combo.addItem('')
                for variantName in variantNames:
                    combo.addItem(variantName)
                indexToSelect = combo.findText(variantSelection)
                combo.setCurrentIndex(indexToSelect)
                variantSets[variantSetName] = combo

        tableWidget.setRowCount(numMetadataRows + len(variantSets))

        rowIndex = 0
        for key in sorted(m.keys()):
            if key == "clips":
                for (clip, metadataGroup) in m[key].items():
                    attrName = QtWidgets.QTableWidgetItem(str('clip:' + clip))
                    tableWidget.setItem(rowIndex, 0, attrName)
                    for metadata in metadataGroup.keys():
                        dataPair = (metadata, metadataGroup[metadata])
                        valStr, ttStr = self._formatMetadataValueView(dataPair)
                        attrVal = QtWidgets.QTableWidgetItem(valStr)
                        attrVal.setToolTip(ttStr)
                        tableWidget.setItem(rowIndex, 1, attrVal)
                        rowIndex += 1
            else:
                attrName = QtWidgets.QTableWidgetItem(str(key))
                tableWidget.setItem(rowIndex, 0, attrName)
                # Get metadata value
                if key == "customData":
                    val = obj.GetCustomData()
                else:
                    val = m[key]

                valStr, ttStr = self._formatMetadataValueView(val)
                attrVal = QtWidgets.QTableWidgetItem(valStr)
                attrVal.setToolTip(ttStr)

                tableWidget.setItem(rowIndex, 1, attrVal)
                rowIndex += 1

        for variantSetName, combo in variantSets.iteritems():
            attrName = QtWidgets.QTableWidgetItem(str(variantSetName+ ' variant'))
            tableWidget.setItem(rowIndex, 0, attrName)
            tableWidget.setCellWidget(rowIndex, 1, combo)
            combo.currentIndexChanged.connect(
                lambda i, combo=combo: combo.updateVariantSelection(
                    i, self._printTiming))
            rowIndex += 1

        tableWidget.resizeColumnToContents(0)

    def _updateCompositionView(self, obj=None):
        """ Sets the contents of the composition tree view"""
        treeWidget = self._ui.compositionTreeWidget
        treeWidget.clear()

        # Update current spec & current layer, and push those updates
        # to the python console
        self._onCompositionSelectionChanged()

        # If no prim or attribute selected, nothing to show.
        if obj is None:
            obj = self._getSelectedObject()
        if not obj:
            return

        # For brevity, we display only the basename of layer paths.
        def LabelForLayer(l):
            return ('~session~' if l == self._dataModel.stage.GetSessionLayer()
                    else l.GetDisplayName())

        # Create treeview items for all sublayers in the layer tree.
        def WalkSublayers(parent, node, layerTree, sublayer=False):
            layer = layerTree.layer
            spec = layer.GetObjectAtPath(node.path)
            item = QtWidgets.QTreeWidgetItem(
                parent,
                [
                    LabelForLayer(layer),
                    'sublayer' if sublayer else node.arcType.displayName,
                    str(node.GetPathAtIntroduction()),
                    'yes' if bool(spec) else 'no'
                ] )

            # attributes for selection:
            item.layer = layer
            item.spec = spec
            item.identifier = layer.identifier

            # attributes for LayerStackContextMenu:
            if layer.realPath:
                item.layerPath = layer.realPath
            if spec:
                item.path = node.path

            item.setExpanded(True)
            item.setToolTip(0, layer.identifier)
            if not spec:
                for i in range(item.columnCount()):
                    item.setForeground(i, UIPropertyValueSourceColors.NONE)
            for subtree in layerTree.childTrees:
                WalkSublayers(item, node, subtree, True)
            return item

        # Create treeview items for all nodes in the composition index.
        def WalkNodes(parent, node):
            nodeItem = WalkSublayers(parent, node, node.layerStack.layerTree)
            for child in node.children:
                WalkNodes(nodeItem, child)

        path = obj.GetPath().GetAbsoluteRootOrPrimPath()
        prim = self._dataModel.stage.GetPrimAtPath(path)
        if not prim:
            return

        # Populate the treeview with items from the prim index.
        index = prim.GetPrimIndex()
        if index.IsValid():
            WalkNodes(treeWidget, index.rootNode)


    def _updateLayerStackView(self, obj=None):
        """ Sets the contents of the layer stack viewer"""

        tableWidget = self._ui.layerStackView

        # Setup table widget
        tableWidget.clearContents()
        tableWidget.setRowCount(0)

        if obj is None:
            obj = self._getSelectedObject()

        if not obj:
            return

        path = obj.GetPath()

        # The pseudoroot is different enough from prims and properties that
        # it makes more sense to process it separately
        if path == Sdf.Path.absoluteRootPath:
            layers = GetRootLayerStackInfo(
                self._dataModel.stage.GetRootLayer())
            tableWidget.setColumnCount(2)
            tableWidget.horizontalHeaderItem(1).setText('Layer Offset')

            tableWidget.setRowCount(len(layers))

            for i, layer in enumerate(layers):
                layerItem = QtWidgets.QTableWidgetItem(layer.GetHierarchicalDisplayString())
                layerItem.layerPath = layer.layer.realPath
                layerItem.identifier = layer.layer.identifier
                toolTip = "<b>identifier:</b> @%s@ <br> <b>resolved path:</b> %s" % \
                    (layer.layer.identifier, layerItem.layerPath)
                toolTip = self._limitToolTipSize(toolTip)
                layerItem.setToolTip(toolTip)
                tableWidget.setItem(i, 0, layerItem)

                offsetItem = QtWidgets.QTableWidgetItem(layer.GetOffsetString())
                offsetItem.layerPath = layer.layer.realPath
                offsetItem.identifier = layer.layer.identifier
                toolTip = self._limitToolTipSize(str(layer.offset))
                offsetItem.setToolTip(toolTip)
                tableWidget.setItem(i, 1, offsetItem)

            tableWidget.resizeColumnToContents(0)
        else:
            specs = []
            tableWidget.setColumnCount(3)
            header = tableWidget.horizontalHeader()
            header.setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeToContents)
            header.setSectionResizeMode(1, QtWidgets.QHeaderView.Stretch)
            header.setSectionResizeMode(2, QtWidgets.QHeaderView.ResizeToContents)
            tableWidget.horizontalHeaderItem(1).setText('Path')

            if path.IsPropertyPath():
                prop = obj.GetPrim().GetProperty(path.name)
                specs = prop.GetPropertyStack(self._dataModel.currentFrame)
                c3 = "Value" if (len(specs) == 0 or
                                 isinstance(specs[0], Sdf.AttributeSpec)) else "Target Paths"
                tableWidget.setHorizontalHeaderItem(2,
                                                    QtWidgets.QTableWidgetItem(c3))
            else:
                specs = obj.GetPrim().GetPrimStack()
                tableWidget.setHorizontalHeaderItem(2,
                    QtWidgets.QTableWidgetItem('Metadata'))

            tableWidget.setRowCount(len(specs))

            for i, spec in enumerate(specs):
                layerItem = QtWidgets.QTableWidgetItem(spec.layer.GetDisplayName())
                layerItem.setToolTip(self._limitToolTipSize(spec.layer.realPath))
                tableWidget.setItem(i, 0, layerItem)

                pathItem = QtWidgets.QTableWidgetItem(spec.path.pathString)
                pathItem.setToolTip(self._limitToolTipSize(spec.path.pathString))
                tableWidget.setItem(i, 1, pathItem)

                if path.IsPropertyPath():
                    valStr = GetShortString(
                        spec, self._dataModel.currentFrame)
                    ttStr = valStr
                    valueItem = QtWidgets.QTableWidgetItem(valStr)
                    sampleBased = (spec.HasInfo('timeSamples') and
                        spec.layer.GetNumTimeSamplesForPath(path) != -1)
                    valueItemColor = (UIPropertyValueSourceColors.TIME_SAMPLE if
                        sampleBased else UIPropertyValueSourceColors.DEFAULT)
                    valueItem.setForeground(valueItemColor)
                    valueItem.setToolTip(ttStr)

                else:
                    metadataKeys = spec.GetMetaDataInfoKeys()
                    metadataDict = {}
                    for mykey in metadataKeys:
                        if spec.HasInfo(mykey):
                            metadataDict[mykey] = spec.GetInfo(mykey)
                    valStr, ttStr = self._formatMetadataValueView(metadataDict)
                    valueItem = QtWidgets.QTableWidgetItem(valStr)
                    valueItem.setToolTip(ttStr)

                tableWidget.setItem(i, 2, valueItem)
                # Add the data the context menu needs
                for j in range(3):
                    item = tableWidget.item(i, j)
                    item.layerPath = spec.layer.realPath
                    item.path = spec.path.pathString
                    item.identifier = spec.layer.identifier

    def _isHUDVisible(self):
        """Checks if the upper HUD is visible by looking at the global HUD
        visibility menu as well as the 'Subtree Info' menu"""
        return self._dataModel.viewSettings.showHUD and self._dataModel.viewSettings.showHUD_Info

    def _updateCameraMaskMenu(self):
        if self._ui.actionCameraMask_Full.isChecked():
            self._dataModel.viewSettings.cameraMaskMode = CameraMaskModes.FULL
        elif self._ui.actionCameraMask_Partial.isChecked():
            self._dataModel.viewSettings.cameraMaskMode = CameraMaskModes.PARTIAL
        else:
            self._dataModel.viewSettings.cameraMaskMode = CameraMaskModes.NONE

    def _updateCameraMaskOutlineMenu(self):
        self._dataModel.viewSettings.showMask_Outline = (
            self._ui.actionCameraMask_Outline.isChecked())

    def _pickCameraMaskColor(self):
        QtWidgets.QColorDialog.setCustomColor(0, 0xFF000000)
        QtWidgets.QColorDialog.setCustomColor(1, 0xFF808080)
        color = QtWidgets.QColorDialog.getColor()
        color = (
                color.redF(),
                color.greenF(),
                color.blueF(),
                color.alphaF()
        )
        self._dataModel.viewSettings.cameraMaskColor = color

    def _updateCameraReticlesInsideMenu(self):
        self._dataModel.viewSettings.showReticles_Inside = (
            self._ui.actionCameraReticles_Inside.isChecked())

    def _updateCameraReticlesOutsideMenu(self):
        self._dataModel.viewSettings.showReticles_Outside = (
            self._ui.actionCameraReticles_Outside.isChecked())

    def _pickCameraReticlesColor(self):
        QtWidgets.QColorDialog.setCustomColor(0, 0xFF000000)
        QtWidgets.QColorDialog.setCustomColor(1, 0xFF0080FF)
        color = QtWidgets.QColorDialog.getColor()
        color = (
                color.redF(),
                color.greenF(),
                color.blueF(),
                color.alphaF()
        )
        self._dataModel.viewSettings.cameraReticlesColor = color

    def _showHUDChanged(self):
        self._dataModel.viewSettings.showHUD = self._ui.actionHUD.isChecked()

    def _showHUD_InfoChanged(self):
        self._dataModel.viewSettings.showHUD_Info = (
            self._ui.actionHUD_Info.isChecked())

    def _showHUD_ComplexityChanged(self):
        self._dataModel.viewSettings.showHUD_Complexity = (
            self._ui.actionHUD_Complexity.isChecked())

    def _showHUD_PerformanceChanged(self):
        self._dataModel.viewSettings.showHUD_Performance = (
            self._ui.actionHUD_Performance.isChecked())

    def _showHUD_GPUstatsChanged(self):
        self._dataModel.viewSettings.showHUD_GPUstats = (
            self._ui.actionHUD_GPUstats.isChecked())

    def _getHUDStatKeys(self):
        ''' returns the keys of the HUD with PRIM and NOTYPE and the top and
         CV, VERT, and FACE at the bottom.'''
        keys = [k for k in self._upperHUDInfo.keys() if k not in (
            HUDEntries.CV, HUDEntries.VERT, HUDEntries.FACE, HUDEntries.PRIM, HUDEntries.NOTYPE)]
        keys = [HUDEntries.PRIM, HUDEntries.NOTYPE] + keys + [HUDEntries.CV, HUDEntries.VERT, HUDEntries.FACE]
        return keys

    def _updateHUDPrimStats(self):
        """update the upper HUD with the proper prim information"""
        self._upperHUDInfo = dict()

        if self._isHUDVisible():
            currentPaths = [n.GetPath()
                for n in self._dataModel.selection.getLCDPrims()
                if n.IsActive()]

            for pth in currentPaths:
                count,types = self._tallyPrimStats(
                    self._dataModel.stage.GetPrimAtPath(pth))
                # no entry for Prim counts? initilize it
                if not self._upperHUDInfo.has_key(HUDEntries.PRIM):
                    self._upperHUDInfo[HUDEntries.PRIM] = 0
                self._upperHUDInfo[HUDEntries.PRIM] += count

                for type in types.iterkeys():
                    # no entry for this prim type? initilize it
                    if not self._upperHUDInfo.has_key(type):
                        self._upperHUDInfo[type] = 0
                    self._upperHUDInfo[type] += types[type]

            if self._stageView:
                self._stageView.upperHUDInfo = self._upperHUDInfo
                self._stageView.HUDStatKeys = self._getHUDStatKeys()

    def _updateHUDGeomCounts(self):
        """updates the upper HUD with the right geom counts
        calls _getGeomCounts() to get the info, which means it could be cached"""
        if not self._isHUDVisible():
            return

        # we get multiple geom dicts, if we have multiple prims selected
        geomDicts = [self._getGeomCounts(n, self._dataModel.currentFrame)
                        for n in self._dataModel.selection.getLCDPrims()]

        for key in (HUDEntries.CV, HUDEntries.VERT, HUDEntries.FACE):
            self._upperHUDInfo[key] = 0
            for gDict in geomDicts:
                self._upperHUDInfo[key] += gDict[key]

        if self._stageView:
            self._stageView.upperHUDInfo = self._upperHUDInfo
            self._stageView.HUDStatKeys = self._getHUDStatKeys()

    def _clearGeomCountsForPrimPath(self, primPath):
        entriesToRemove = []
        # Clear all entries whose prim is either an ancestor or a descendant
        # of the given prim path.
        for (p, frame) in self._geomCounts:
            if (primPath.HasPrefix(p.GetPath()) or p.GetPath().HasPrefix(primPath)):
                entriesToRemove.append((p, frame))
        for entry in entriesToRemove:
            del self._geomCounts[entry]

    def _getGeomCounts( self, prim, frame ):
        """returns cached geom counts if available, or calls _calculateGeomCounts()"""
        if not self._geomCounts.has_key((prim,frame)):
            self._calculateGeomCounts( prim, frame )

        return self._geomCounts[(prim,frame)]

    def _accountForFlattening(self,shape):
        """Helper function for computing geomCounts"""
        if len(shape) == 1:
            return shape[0] / 3
        else:
            return shape[0]

    def _calculateGeomCounts(self, prim, frame):
        """Computes the number of CVs, Verts, and Faces for each prim and each
        frame in the stage (for use by the HUD)"""

        # This is expensive enough that we should give the user feedback
        # that something is happening...
        QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.BusyCursor)
        try:
            thisDict = {HUDEntries.CV: 0, HUDEntries.VERT: 0, HUDEntries.FACE: 0}

            if prim.IsA(UsdGeom.Curves):
                curves = UsdGeom.Curves(prim)
                vertexCounts = curves.GetCurveVertexCountsAttr().Get(frame)
                if vertexCounts is not None:
                    for count in vertexCounts:
                        thisDict[HUDEntries.CV] += count

            elif prim.IsA(UsdGeom.Mesh):
                mesh = UsdGeom.Mesh(prim)
                faceVertexCount = mesh.GetFaceVertexCountsAttr().Get(frame)
                faceVertexIndices = mesh.GetFaceVertexIndicesAttr().Get(frame)
                if faceVertexCount is not None and faceVertexIndices is not None:
                    uniqueVerts = set(faceVertexIndices)

                    thisDict[HUDEntries.VERT] += len(uniqueVerts)
                    thisDict[HUDEntries.FACE] += len(faceVertexCount)

            self._geomCounts[(prim,frame)] = thisDict

            for child in prim.GetChildren():
                childResult = self._getGeomCounts(child, frame)

                for key in (HUDEntries.CV, HUDEntries.VERT, HUDEntries.FACE):
                    self._geomCounts[(prim,frame)][key] += childResult[key]
        except Exception as err:
            print "Error encountered while computing prim subtree HUD info: %s" % err
        finally:
            QtWidgets.QApplication.restoreOverrideCursor()

    def _updateNavigationMenu(self):
        """Make the Navigation menu items enabled or disabled depending on the
        selected prim."""
        anyModels = False
        anyBoundPreviewMaterials = False
        anyBoundFullMaterials = False

        for prim in self._dataModel.selection.getPrims():
            if prim.IsA(UsdGeom.Imageable):
                imageable = UsdGeom.Imageable(prim)
            anyModels = anyModels or GetEnclosingModelPrim(prim) is not None
            
            (previewMat,previewBindingRel) =\
                UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                    materialPurpose=UsdShade.Tokens.preview)
            anyBoundPreviewMaterials |= bool(previewMat)
            
            (fullMat,fullBindingRel) =\
                UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                    materialPurpose=UsdShade.Tokens.full)            
            anyBoundFullMaterials |= bool(fullMat)

        self._ui.actionSelect_Model_Root.setEnabled(anyModels)
        self._ui.actionSelect_Bound_Preview_Material.setEnabled(
                anyBoundPreviewMaterials)
        self._ui.actionSelect_Preview_Binding_Relationship.setEnabled(
                anyBoundPreviewMaterials)
        self._ui.actionSelect_Bound_Full_Material.setEnabled(
                anyBoundFullMaterials)
        self._ui.actionSelect_Full_Binding_Relationship.setEnabled(
                anyBoundFullMaterials)

    def _updateEditMenu(self):
        """Make the Edit Prim menu items enabled or disabled depending on the
        selected prim."""
        
        # Use the descendent-pruned selection set to avoid redundant
        # traversal of the stage to answer isLoaded...
        anyLoadable, unused = GetPrimsLoadability(
            self._dataModel.selection.getLCDPrims())
        removeEnabled = False
        anyImageable = False
        anyActive = False
        anyInactive = False
        for prim in self._dataModel.selection.getPrims():
            if prim.IsA(UsdGeom.Imageable):
                imageable = UsdGeom.Imageable(prim)
                anyImageable = anyImageable or bool(imageable)
                removeEnabled = removeEnabled or HasSessionVis(prim)            
            if prim.IsActive():
                anyActive = True
            else:
                anyInactive = True
        
        self._ui.actionRemove_Session_Visibility.setEnabled(removeEnabled)
        self._ui.actionMake_Visible.setEnabled(anyImageable)
        self._ui.actionVis_Only.setEnabled(anyImageable)
        self._ui.actionMake_Invisible.setEnabled(anyImageable)
        self._ui.actionLoad.setEnabled(anyLoadable)
        self._ui.actionUnload.setEnabled(anyLoadable)
        self._ui.actionActivate.setEnabled(anyInactive)
        self._ui.actionDeactivate.setEnabled(anyActive)

    def getSelectedItems(self):
        return [self._primToItemMap[n]
            for n in self._dataModel.selection.getPrims()
            if n in self._primToItemMap]

    def _getPrimFromPropString(self, p):
        return self._dataModel.stage.GetPrimAtPath(p.split('.')[0])

    def visSelectedPrims(self):
        with BusyContext():
            for item in self.getSelectedItems():
                item.makeVisible()
            self.editComplete('Made selected prims visible')
            # makeVisible may cause aunt and uncle prims' authored vis
            # to change, so we need to fix up the whole shebang
            self._resetPrimViewVis(selItemsOnly=False)

    def visOnlySelectedPrims(self):
        with BusyContext():
            ResetSessionVisibility(self._dataModel.stage)
            InvisRootPrims(self._dataModel.stage)
            for item in self.getSelectedItems():
                item.makeVisible()
            self.editComplete('Made ONLY selected prims visible')
            # QTreeWidget does not honor setUpdatesEnabled, and updating
            # the Vis column for all widgets is pathologically slow.
            # It is sadly much much faster to regenerate the entire view
            self._resetPrimView()

    def invisSelectedPrims(self):
        with BusyContext():
            for item in self.getSelectedItems():
                item.setVisible(False)
            self.editComplete('Made selected prims invisible')
            self._resetPrimViewVis()

    def removeVisSelectedPrims(self):
        with BusyContext():
            for item in self.getSelectedItems():
                item.removeVisibility()
            self.editComplete("Removed selected prims' visibility opinions")
            self._resetPrimViewVis()

    def resetSessionVisibility(self):
        with BusyContext():
            ResetSessionVisibility(self._dataModel.stage)
            self.editComplete('Removed ALL session visibility opinions.')
            # QTreeWidget does not honor setUpdatesEnabled, and updating
            # the Vis column for all widgets is pathologically slow.
            # It is sadly much much faster to regenerate the entire view
            self._resetPrimView()

    def _setSelectedPrimsActivation(self, active):
        """Activate or deactivate all selected prims."""

        with BusyContext():

            # We can only activate/deactivate prims which are not in a master.
            paths = []
            for item in self.getSelectedItems():
                if item.prim.IsPseudoRoot():
                    print("WARNING: Cannot change activation of pseudoroot.")
                elif item.isInMaster:
                    print("WARNING: The prim <" + str(item.prim.GetPrimPath()) +
                        "> is in a master. Cannot change activation.")
                else:
                    paths.append(item.prim.GetPrimPath())

            # If we are deactivating prims, clear the selection so it doesn't
            # hold onto paths from inactive prims.
            if not active:
                self._dataModel.selection.clear()

            # If we try to deactivate prims one at a time in Usd, some may have
            # become invalid by the time we get to them. Instead, we set the
            # active state all at once through Sdf.
            layer = self._dataModel.stage.GetEditTarget().GetLayer()
            with Sdf.ChangeBlock():
                for path in paths:
                    sdfPrim = Sdf.CreatePrimInLayer(layer, path)
                    sdfPrim.active = active

            pathNames = ", ".join(path.name for path in paths)
            if active:
                self.editComplete("Activated {}.".format(pathNames))
            else:
                self.editComplete("Deactivated {}.".format(pathNames))

    def activateSelectedPrims(self):
        self._setSelectedPrimsActivation(True)

    def deactivateSelectedPrims(self):
        self._setSelectedPrimsActivation(False)

    def loadSelectedPrims(self):
        with BusyContext():
            primNames=[]
            for item in self.getSelectedItems():
                item.setLoaded(True)
                primNames.append(item.name)
            self.editComplete("Loaded %s." % primNames)

    def unloadSelectedPrims(self):
        with BusyContext():
            primNames=[]
            for item in self.getSelectedItems():
                item.setLoaded(False)
                primNames.append(item.name)
            self.editComplete("Unloaded %s." % primNames)

    def onStageViewMouseDrag(self):
        return

    def onPrimSelected(self, path, instanceIndex, point, button, modifiers):

        # Ignoring middle button until we have something
        # meaningfully different for it to do
        if button in [QtCore.Qt.LeftButton, QtCore.Qt.RightButton]:
            # Expected context-menu behavior is that even with no
            # modifiers, if we are activating on something already selected,
            # do not change the selection
            doContext = (button == QtCore.Qt.RightButton and path
                         and path != Sdf.Path.emptyPath)
            doSelection = True
            if doContext:
                for selPrim in self._dataModel.selection.getPrims():
                    selPath = selPrim.GetPath()
                    if (selPath != Sdf.Path.absoluteRootPath and
                        path.HasPrefix(selPath)):
                        doSelection = False
                        break
            if doSelection:
                self._dataModel.selection.setPoint(point)

                shiftPressed = modifiers & QtCore.Qt.ShiftModifier
                ctrlPressed = modifiers & QtCore.Qt.ControlModifier

                if path != Sdf.Path.emptyPath:
                    prim = self._dataModel.stage.GetPrimAtPath(path)

                    if self._dataModel.viewSettings.pickMode == PickModes.MODELS:
                        if prim.IsModel():
                            model = prim
                        else:
                            model = GetEnclosingModelPrim(prim)
                        if model:
                            prim = model

                    if self._dataModel.viewSettings.pickMode != PickModes.INSTANCES:
                        instanceIndex = ALL_INSTANCES

                    instance = instanceIndex
                    if instanceIndex != ALL_INSTANCES:
                        instanceId = GetInstanceIdForIndex(prim, instanceIndex,
                            self._dataModel.currentFrame)
                        if instanceId is not None:
                            instance = instanceId

                    if shiftPressed:
                        # Clicking prim while holding shift adds it to the
                        # selection.
                        self._dataModel.selection.addPrim(prim, instance)
                    elif ctrlPressed:
                        # Clicking prim while holding ctrl toggles it in the
                        # selection.
                        self._dataModel.selection.togglePrim(prim, instance)
                    else:
                        # Clicking prim with no modifiers sets it as the
                        # selection.
                        self._dataModel.selection.switchToPrimPath(
                            prim.GetPath(), instance)

                elif not shiftPressed and not ctrlPressed:
                    # Clicking the background with no modifiers clears the
                    # selection.
                    self._dataModel.selection.clear()

            if doContext:
                item = self._getItemAtPath(path)
                self._showPrimContextMenu(item)

                # context menu steals mouse release event from the StageView.
                # We need to give it one so it can track its interaction
                # mode properly
                mrEvent = QtGui.QMouseEvent(QtCore.QEvent.MouseButtonRelease,
                                            QtGui.QCursor.pos(),
                                            QtCore.Qt.RightButton,
                                            QtCore.Qt.MouseButtons(QtCore.Qt.RightButton),
                                            QtCore.Qt.KeyboardModifiers())
                QtWidgets.QApplication.sendEvent(self._stageView, mrEvent)

    def onRollover(self, path, instanceIndex, modifiers):
        prim = self._dataModel.stage.GetPrimAtPath(path)
        if prim:
            headerStr = ""
            propertyStr = ""
            materialStr = ""
            aiStr = ""
            vsStr = ""
            model = GetEnclosingModelPrim(prim)

            def _MakeModelRelativePath(path, model,
                                       boldPrim=True, boldModel=False):
                makeRelative = model and path.HasPrefix(model.GetPath())
                if makeRelative:
                    path = path.MakeRelativePath(model.GetPath().GetParentPath())
                pathParts = str(path).split('/')
                if boldModel and makeRelative:
                    pathParts[0] = "<b>%s</b>" % pathParts[0]
                if boldPrim:
                    pathParts[-1] = "<b>%s</b>" % pathParts[-1]
                return '/'.join(pathParts)

            def _HTMLEscape(s):
                return s.replace('&', '&amp;'). \
                         replace('<', '&lt;'). \
                         replace('>', '&gt;')

            # First add in all model-related data, if present
            if model:
                groupPath = model.GetPath().GetParentPath()
                # Make the model name and prim name bold.
                primModelPath = _MakeModelRelativePath(prim.GetPath(),
                                                       model, True, True)
                headerStr = "%s<br><nobr><small>in group:</small> %s</nobr>" % \
                    (str(primModelPath),str(groupPath))

                # asset info, including computed creation date
                mAPI = Usd.ModelAPI(model)
                assetInfo = mAPI.GetAssetInfo()
                aiStr = "<hr><b>assetInfo</b> for %s:" % model.GetName()
                if assetInfo and len(assetInfo) > 0:
                    specs = model.GetPrimStack()
                    name, time, owner = GetAssetCreationTime(specs,
                                                   mAPI.GetAssetIdentifier())
                    for key, value in assetInfo.iteritems():
                        aiStr += "<br> -- <em>%s</em> : %s" % (key, _HTMLEscape(str(value)))
                    aiStr += "<br><em><small>%s created on %s by %s</small></em>" % \
                        (_HTMLEscape(name), _HTMLEscape(time), 
                         _HTMLEscape(owner))
                else:
                    aiStr += "<br><small><em>No assetInfo!</em></small>"

                # variantSets are by no means required/expected, so if there
                # are none, don't bother to declare so.
                mVarSets = model.GetVariantSets()
                setNames = mVarSets.GetNames()
                if len(setNames) > 0:
                    vsStr = "<hr><b>variantSets</b> on %s:" % model.GetName()
                    for name in setNames:
                        sel = mVarSets.GetVariantSelection(name)
                        vsStr += "<br> -- <em>%s</em> = %s" % (name, sel)

            else:
                headerStr = _MakeModelRelativePath(path, None)

            # Property info: advise about rare visibility and purpose conditions
            img = UsdGeom.Imageable(prim)
            propertyStr = "<hr><b>Property Summary for %s '%s':</b>" % \
                (prim.GetTypeName(), prim.GetName())
            # Now cherry pick "important" attrs... could do more, here
            if img:
                if img.GetVisibilityAttr().ValueMightBeTimeVarying():
                    propertyStr += "<br> -- <em>visibility</em> varies over time"
                purpose = img.GetPurposeAttr().Get()
                inheritedPurpose = img.ComputePurpose()
                if inheritedPurpose != UsdGeom.Tokens.default_:
                    propertyStr += "<br> -- <em>purpose</em> is <b>%s</b>%s " %\
                        (inheritedPurpose, "" if purpose == inheritedPurpose \
                             else ", <small>(inherited)</small>")
            gprim = UsdGeom.Gprim(prim)
            if gprim:
                ds = gprim.GetDoubleSidedAttr().Get()
                orient = gprim.GetOrientationAttr().Get()
                propertyStr += "<br> -- <em>doubleSided</em> = %s" % \
                    ( "true" if ds else "false")
                propertyStr += "<br> -- <em>orientation</em> = %s" % orient
            ptBased = UsdGeom.PointBased(prim)
            if ptBased:
                # XXX WBN to not have to read points in to get array size
                # XXX2 Should try to determine varying topology
                points = ptBased.GetPointsAttr().Get(
                    self._dataModel.currentFrame)
                propertyStr += "<br> -- %d points" % len(points)
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                propertyStr += "<br> -- <em>subdivisionScheme</em> = %s" %\
                    mesh.GetSubdivisionSchemeAttr().Get()
            pi = UsdGeom.PointInstancer(prim)
            if pi:
                indices = pi.GetProtoIndicesAttr().Get(
                    self._dataModel.currentFrame)
                propertyStr += "<br> -- <em>%d instances</em>" % len(indices)
                protos = pi.GetPrototypesRel().GetForwardedTargets()
                propertyStr += "<br> -- <em>%d unique prototypes</em>" % len(protos)
                if instanceIndex >= 0 and instanceIndex < len(indices):
                    protoIndex = indices[instanceIndex]
                    if protoIndex < len(protos):
                        currProtoPath = protos[protoIndex]
                        # If, as is common, proto is beneath the PI,
                        # strip the PI's prefix for display
                        if currProtoPath.HasPrefix(path):
                            currProtoPath = currProtoPath.MakeRelativePath(path)
                        propertyStr += "<br> -- <em>instance of prototype &lt;%s&gt;</em>" % str(currProtoPath)

            # Material info - this IS expected
            materialStr = "<hr><b>Material assignment:</b>"
            materialAssigns = {}
            materialAssigns['generic'] = (genericMat, genericBindingRel) = \
                UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                    materialPurpose=UsdShade.Tokens.allPurpose)
            materialAssigns[UsdShade.Tokens.preview] = \
                UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                    materialPurpose=UsdShade.Tokens.preview)
            materialAssigns[UsdShade.Tokens.full] = \
                UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial(
                    materialPurpose=UsdShade.Tokens.full)

            gotValidMaterial = False
            for purpose, materialAssign in materialAssigns.iteritems():
                (material, bindingRel) = materialAssign
                if not material:
                    continue

                gotValidMaterial = True

                # skip specific purpose binding display if it is the same
                # as the generic binding.
                if purpose != 'generic' and bindingRel == genericBindingRel:
                    continue

                # if the material is in the same model, make path 
                # model-relative
                materialStr += "<br><em>%s</em>: %s" % (purpose, 
                        _MakeModelRelativePath(material.GetPath(), model))

                bindingRelPath = _MakeModelRelativePath(
                        bindingRel.GetPath(), model)
                materialStr += "<br><small><em>Material binding "\
                    "relationship: %s</em></small>" % str(bindingRelPath)

            if not gotValidMaterial:
                materialStr += "<small><em>No assigned Material!</em></small>"

            # Instance / master info, if this prim is a native instance, else
            # instance index/id if it's from a PointInstancer
            instanceStr = ""
            if prim.IsInstance():
                instanceStr = "<hr><b>Instancing:</b><br>"
                instanceStr += "<nobr><small><em>Instance of master:</em></small> %s</nobr>" % \
                    str(prim.GetMaster().GetPath())
            elif instanceIndex != -1:
                instanceStr = "<hr><b>Instance Index:</b> %d" % instanceIndex
                instanceId = GetInstanceIdForIndex(prim, instanceIndex,
                    self._dataModel.currentFrame)
                if instanceId is not None:
                    instanceStr += "<br><b>Instance Id:</b> %d" % instanceId

            # Then put it all together
            tip = headerStr + propertyStr + materialStr + instanceStr + aiStr + vsStr

        else:
            tip = ""
        QtWidgets.QToolTip.showText(QtGui.QCursor.pos(), tip, self._stageView)

    def processNavKeyEvent(self, kpEvent):
        # This method is a standin for a hotkey processor... for now greatly
        # limited in scope, as we mostly use Qt's builtin hotkey dispatch.
        # Since we want navigation keys to be hover-context-sensitive, we
        # cannot use the native mechanism.
        key = kpEvent.key()
        if key == QtCore.Qt.Key_Right:
            self._advanceFrame()
            return True
        elif key == QtCore.Qt.Key_Left:
            self._retreatFrame()
            return True
        return False

    def _viewSettingChanged(self):
        self._refreshViewMenubar()
        self._displayPurposeChanged()
        self._HUDInfoChanged()

    def _refreshViewMenubar(self):
        """Refresh the menubar actions associated with a view setting. This
        includes updating checked/unchecked and enabled/disabled states for
        actions and submenus to match the values in the ViewSettingsDataModel.
        """
        self._refreshRenderModeMenu()
        self._refreshColorCorrectionModeMenu()
        self._refreshPickModeMenu()
        self._refreshComplexityMenu()
        self._refreshBBoxMenu()
        self._refreshLightsMenu()
        self._refreshClearColorsMenu()
        self._refreshCameraMenu()
        self._refreshCameraGuidesMenu()
        self._refreshCameraMaskMenu()
        self._refreshCameraReticlesMenu()
        self._refreshDisplayPurposesMenu()
        self._refreshViewMenu()
        self._refreshHUDMenu()
        self._refreshShowPrimMenu()
        self._refreshRedrawOnScrub()
        self._refreshRolloverPrimInfoMenu()
        self._refreshSelectionHighlightingMenu()
        self._refreshSelectionHighlightColorMenu()

    def _refreshRenderModeMenu(self):
        for action in self._renderModeActions:
            action.setChecked(
                str(action.text()) == self._dataModel.viewSettings.renderMode)

    def _refreshColorCorrectionModeMenu(self):
        for action in self._colorCorrectionActions:
            action.setChecked(
                str(action.text()) == self._dataModel.viewSettings.colorCorrectionMode)

    def _refreshPickModeMenu(self):
        for action in self._pickModeActions:
            action.setChecked(
                str(action.text()) == self._dataModel.viewSettings.pickMode)

    def _refreshComplexityMenu(self):
        complexityName = self._dataModel.viewSettings.complexity.name
        for action in self._complexityActions:
            action.setChecked(str(action.text()) == complexityName)

    def _refreshBBoxMenu(self):
        self._ui.showBBoxes.setChecked(self._dataModel.viewSettings.showBBoxes)
        self._ui.showAABBox.setChecked(self._dataModel.viewSettings.showAABBox)
        self._ui.showOBBox.setChecked(self._dataModel.viewSettings.showOBBox)
        self._ui.showBBoxPlayback.setChecked(
            self._dataModel.viewSettings.showBBoxPlayback)

    def _refreshLightsMenu(self):
        # lighting is not activated until a shaded mode is selected
        self._ui.menuLights.setEnabled(self._dataModel.viewSettings.renderMode in ShadedRenderModes)

        # three point lights not activated until ambient is deselected
        self._ui.threePointLights.setEnabled(
            not self._dataModel.viewSettings.ambientLightOnly)

        self._ui.actionAmbient_Only.setChecked(
            self._dataModel.viewSettings.ambientLightOnly)
        self._ui.actionKey.setChecked(
            self._dataModel.viewSettings.keyLightEnabled)
        self._ui.actionFill.setChecked(
            self._dataModel.viewSettings.fillLightEnabled)
        self._ui.actionBack.setChecked(
            self._dataModel.viewSettings.backLightEnabled)

    def _refreshClearColorsMenu(self):
        clearColorText = self._dataModel.viewSettings.clearColorText
        for action in self._clearColorActions:
            action.setChecked(str(action.text()) == clearColorText)

    def _refreshCameraMenu(self):
        cameraPath = self._dataModel.viewSettings.cameraPath
        for action in self._ui.menuCamera.actions():
            action.setChecked(action.data() == cameraPath)

    def _refreshCameraGuidesMenu(self):
        self._ui.actionDisplay_Camera_Oracles.setChecked(
            self._dataModel.viewSettings.displayCameraOracles)
        self._ui.actionCameraMask_Outline.setChecked(
            self._dataModel.viewSettings.showMask_Outline)

    def _refreshCameraMaskMenu(self):
        viewSettings = self._dataModel.viewSettings
        self._ui.actionCameraMask_Full.setChecked(
            viewSettings.cameraMaskMode == CameraMaskModes.FULL)
        self._ui.actionCameraMask_Partial.setChecked(
            viewSettings.cameraMaskMode == CameraMaskModes.PARTIAL)
        self._ui.actionCameraMask_None.setChecked(
            viewSettings.cameraMaskMode == CameraMaskModes.NONE)

    def _refreshCameraReticlesMenu(self):
        self._ui.actionCameraReticles_Inside.setChecked(
            self._dataModel.viewSettings.showReticles_Inside)
        self._ui.actionCameraReticles_Outside.setChecked(
            self._dataModel.viewSettings.showReticles_Outside)

    def _refreshDisplayPurposesMenu(self):
        self._ui.actionDisplay_Guide.setChecked(
            self._dataModel.viewSettings.displayGuide)
        self._ui.actionDisplay_Proxy.setChecked(
            self._dataModel.viewSettings.displayProxy)
        self._ui.actionDisplay_Render.setChecked(
            self._dataModel.viewSettings.displayRender)

    def _refreshViewMenu(self):
        self._ui.actionEnable_Scene_Materials.setChecked(
            self._dataModel.viewSettings.enableSceneMaterials)
        self._ui.actionDisplay_PrimId.setChecked(
            self._dataModel.viewSettings.displayPrimId)
        self._ui.actionCull_Backfaces.setChecked(
            self._dataModel.viewSettings.cullBackfaces)
        self._ui.actionAuto_Compute_Clipping_Planes.setChecked(
            self._dataModel.viewSettings.autoComputeClippingPlanes)

    def _refreshHUDMenu(self):
        self._ui.actionHUD.setChecked(self._dataModel.viewSettings.showHUD)
        self._ui.actionHUD_Info.setChecked(
            self._dataModel.viewSettings.showHUD_Info)
        self._ui.actionHUD_Complexity.setChecked(
            self._dataModel.viewSettings.showHUD_Complexity)
        self._ui.actionHUD_Performance.setChecked(
            self._dataModel.viewSettings.showHUD_Performance)
        self._ui.actionHUD_GPUstats.setChecked(
            self._dataModel.viewSettings.showHUD_GPUstats)

    def _refreshShowPrimMenu(self):
        self._ui.actionShow_Inactive_Prims.setChecked(
            self._dataModel.viewSettings.showInactivePrims)
        self._ui.actionShow_All_Master_Prims.setChecked(
            self._dataModel.viewSettings.showAllMasterPrims)
        self._ui.actionShow_Undefined_Prims.setChecked(
            self._dataModel.viewSettings.showUndefinedPrims)
        self._ui.actionShow_Abstract_Prims.setChecked(
            self._dataModel.viewSettings.showAbstractPrims)

    def _refreshRedrawOnScrub(self):
        self._ui.redrawOnScrub.setChecked(
            self._dataModel.viewSettings.redrawOnScrub)

    def _refreshRolloverPrimInfoMenu(self):
        self._ui.actionRollover_Prim_Info.setChecked(
            self._dataModel.viewSettings.rolloverPrimInfo)

    def _refreshSelectionHighlightingMenu(self):
        for action in self._selHighlightActions:
            action.setChecked(
                str(action.text())
                == self._dataModel.viewSettings.selHighlightMode)

    def _refreshSelectionHighlightColorMenu(self):
        for action in self._selHighlightColorActions:
            action.setChecked(
                str(action.text())
                == self._dataModel.viewSettings.highlightColorName)

    def _displayPurposeChanged(self):
        self._updatePropertyView()
        if self._stageView:
            self._stageView.updateBboxPurposes()
            self._stageView.updateView()

    def _HUDInfoChanged(self):
        """Called when a HUD setting that requires info refresh has changed."""
        if self._isHUDVisible():
            self._updateHUDPrimStats()
            self._updateHUDGeomCounts()

    def _onPrimsChanged(self, primsChange, propertiesChange):
        """Called when prims in the USD stage have changed."""
        from rootDataModel import ChangeNotice
        self._updateForStageChanges(
            hasPrimResync=(primsChange==ChangeNotice.RESYNC))
