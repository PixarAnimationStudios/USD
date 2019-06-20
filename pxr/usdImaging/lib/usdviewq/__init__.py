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
import sys, argparse, os

from qt import QtWidgets
from common import Timer
from appController import AppController

from pxr import UsdAppUtils


class InvalidUsdviewOption(Exception):
    """Raised when an invalid Usdview option is found in
    Launcher.ValidateOptions or any methods which override it.
    """
    pass


class Launcher(object):
    '''
    Base class for argument parsing, validation, and initialization for UsdView

    Subclasses can choose to override
      -- GetHelpDescription()
      -- RegisterOptions()
      -- ParseOptions()
      -- ValidateOptions()
      -- GetResolverContext()
    '''
    def __init__(self):
        pass

    def Run(self):
        '''
        The main entry point to launch a process using UsdView.
        '''

        parser = argparse.ArgumentParser(prog=sys.argv[0],
                                         description=self.GetHelpDescription())

        with Timer() as totalTimer:
            self.RegisterPositionals(parser)
            self.RegisterOptions(parser, debug='--debug' in sys.argv[1:])
            arg_parse_result = self.ParseOptions(parser)
            self.ValidateOptions(arg_parse_result)
            self.__LaunchProcess(arg_parse_result)

        if arg_parse_result.timing and arg_parse_result.quitAfterStartup:
            totalTimer.PrintTime('open and close usdview')

    def GetHelpDescription(self):
        '''return the help description'''
        return 'View a usd file'

    def RegisterPositionals(self, parser):
        '''
        register positional arguments on the ArgParser
        '''
        parser.add_argument('usdFile', action='store',
                            type=str,
                            help='The file to view')

    def RegisterOptions(self, parser, debug=False):
        '''
        register optional arguments on the ArgParser
        '''
        from pxr import UsdUtils

        parser.add_argument('--renderer', action='store',
                            type=str, dest='renderer',
                            choices=AppController.GetRendererOptionChoices(),
                            help="Which render backend to use (named as it "
                            "appears in the menu).  Use '%s' to "
                            "turn off Hydra renderers." %
                            AppController.HYDRA_DISABLED_OPTION_STRING,
                            default='')
        
        parser.add_argument('--select', action='store', default='/',
                            dest='primPath', type=str,
                            help='A prim path to initially select and frame')

        UsdAppUtils.cameraArgs.AddCmdlineArgs(parser,
            altHelpText=(
                "Which camera to set the view to on open - may be given as "
                "either just the camera's prim name (ie, just the last "
                "element in the prim path), or as a full prim path.  Note "
                "that if only the prim name is used, and more than one camera "
                "exists with the name, which is used will be effectively "
                "random (default=%(default)s)"))

        parser.add_argument('--mask', action='store',
                            dest='populationMask',
                            metavar='PRIMPATH[,PRIMPATH...]',
                            help='Limit stage population to these prims, '
                            'their descendants and ancestors.  To specify '
                            'multiple paths, either use commas with no spaces '
                            'or quote the argument and separate paths by '
                            'commas and/or spaces.')

        parser.add_argument('--clearsettings', action='store_true',
                            dest='clearSettings',
                            help='Restores usdview settings to default')

        parser.add_argument('--defaultsettings', action='store_true',
                            dest='defaultSettings',
                            help='Launch usdview with default settings')

        parser.add_argument('--norender', action='store_true',
                            dest='noRender',
                            help='Display only hierarchy browser')

        parser.add_argument('--noplugins', action='store_true',
                            dest='noPlugins',
                            help='Do not load plugins')

        parser.add_argument('--unloaded', action='store_true',
                            dest='unloaded',
                            help='Do not load payloads')

        parser.add_argument('--timing', action='store_true',
                            dest='timing',
                            help='echo timing stats to console. NOTE: timings will be unreliable when the --mallocTagStats option is also in use')

        parser.add_argument('--memstats', action='store', default='none',
                            dest='mallocTagStats', type=str,
                            choices=['none', 'stage', 'stageAndImaging'],
                            help='Use the Pxr MallocTags memory accounting system to profile USD, saving results to a tmp file, with a summary to the console.  Will have no effect if MallocTags are not supported in the USD installation.')

        parser.add_argument('--numThreads', action='store',
                            type=int, default=0,
                            help='Number of threads used for processing'
                                 '(0 is max, negative numbers imply max - N)')

        parser.add_argument('--ff', action='store',
                            dest='firstframe', type=int,
                            help='Set the first frame of the viewer')

        parser.add_argument('--lf', action='store',
                            dest='lastframe', type=int,
                            help='Set the last frame of the viewer')

        parser.add_argument('--cf', action='store',
                            dest='currentframe', type=int,
                            help='Set the current frame of the viewer')

        UsdAppUtils.complexityArgs.AddCmdlineArgs(parser,
            altHelpText=(
                'Set the initial mesh refinement complexity (%(default)s).'))

        parser.add_argument('--quitAfterStartup', action='store_true',
                            dest='quitAfterStartup',
                            help='quit immediately after start up')
                            
        parser.add_argument('--sessionLayer', default=None, type=str,
                            help= "If specified, the stage will be opened "
                            "with the 'sessionLayer' in place of the default "
                            "anonymous layer. As this changes the session "
                            "layer from anonymous to persistent, be "
                            "aware that layers saved from Export Overrides "
                            "will include the opinions in the persistent "
                            "session layer.")

        parser.add_argument('--debug', action='store_true',
                            help='Display additional help information about '
                            'debugging options')
        
        parser.add_argument('--delegateID',
                    help='Specify a prefix to be added before all ' 
                    'hydra renderIndex paths; useful when debugging, '
                    'or investigating the inner workings of hydra, but '
                    'may have a performance impact for very large scenes' 
                    if debug else argparse.SUPPRESS)

    def ParseOptions(self, parser):
        '''
        runs the parser on the arguments
        '''
        return parser.parse_args()

    def ValidateOptions(self, arg_parse_result):
        '''
        Called by Run(), after ParseOptions() is called. Validates and 
        potentially modifies the parsed arguments. Raises InvalidUsdviewOption 
        if an invalid option is found. If a derived class has overridden 
        ParseOptions(), ValidateOptions() is an opportunity to process the
        options and transmute other "core" options in response.  If 
        overridden, derived classes should likely first call the base method.
        '''

        # split arg_parse_result.populationMask into paths.
        if arg_parse_result.populationMask:
            arg_parse_result.populationMask = (
                arg_parse_result.populationMask.replace(',', ' ').split())

        # Verify that the camera path is either an absolute path, or is just
        # the name of a camera.
        if arg_parse_result.camera:
            camPath = arg_parse_result.camera
            if camPath.isEmpty:
                raise InvalidUsdviewOption(
                    "invalid camera path - %r" % camPath)
            if not camPath.IsPrimPath():
                raise InvalidUsdviewOption(
                    "invalid camera path - must be a raw prim path with no "
                    "variant selections or properties - got: %r" % camPath)

            # If it's a multi-element path, make sure it is absolute.
            if camPath.pathElementCount > 1:
                if not camPath.IsAbsolutePath():
                    # perhaps we should error here? For now just pre-pending
                    # root, and printing warning...
                    from pxr import Sdf
                    print >> sys.stderr, (
                        "WARNING: camera path %r was not absolute, prepending "
                        "%r to make it absolute" % (str(camPath),
                            str(Sdf.Path.absoluteRootPath)))
                    arg_parse_result.camera = camPath.MakeAbsolutePath(
                        Sdf.Path.absoluteRootPath)

        if arg_parse_result.clearSettings and arg_parse_result.defaultSettings:
            raise InvalidUsdviewOption(
                "cannot supply both --clearsettings and --defaultsettings.")
            
        if arg_parse_result.delegateID:
            from pxr import Sdf
            isValid = Sdf.Path.IsValidPathString(arg_parse_result.delegateID)
            if isinstance(isValid, tuple) and not isValid[0]:
                raise InvalidUsdviewOption(
                    "given delegateID was not a valid sdf path - %r: %s"
                    % (arg_parse_result.delegateID, isValid[1]))
            arg_parse_result.delegateID = Sdf.Path(arg_parse_result.delegateID)
            arg_parse_result.delegateID = \
                arg_parse_result.delegateID.MakeAbsolutePath(
                    Sdf.Path.absoluteRootPath)
            if not arg_parse_result.delegateID.IsAbsoluteRootOrPrimPath():
                raise InvalidUsdviewOption(
                    "given delegateID was not a prim path - %r"
                    % arg_parse_result.delegateID)
 
    def GetResolverContext(self, usdFile):
        """
        Create and return the ArResolverContext that will be used to Open
        the Stage for the given usdFile.  Base implementation
        creates a default asset context for the usdFile asset, but derived
        classes can do more sophisticated resolver and context configuration.
        
        Will be called each time a new stage is opened.

        It is not necessary to create an ArResolverContext for every UsdStage
        one opens, as the Stage will use reasonable fallback behavior if no
        context is provided.  For usdview, configuring an asset context by
        default is reasonable, and allows clients that embed usdview to 
        achieve different behavior when needed.
        """
        from pxr import Ar
        
        r = Ar.GetResolver()
        r.ConfigureResolverForAsset(usdFile)
        return r.CreateDefaultContextForAsset(usdFile)


    def LaunchPreamble(self, arg_parse_result):
        # Initialize concurrency limit as early as possible so that it is
        # respected by subsequent imports.
        from pxr import Work
        Work.SetConcurrencyLimitArgument(arg_parse_result.numThreads)

        if arg_parse_result.clearSettings:
            AppController.clearSettings()

        # Find the resource directory
        resourceDir = os.path.dirname(os.path.realpath(__file__)) + "/"

        # Create the Qt application
        app = QtWidgets.QApplication(sys.argv)

        # Apply the style sheet to it
        sheet = open(os.path.join(resourceDir, 'usdviewstyle.qss'), 'r')

        # Qt style sheet accepts only forward slashes as path separators
        sheetString = sheet.read().replace('RESOURCE_DIR',
                                           resourceDir.replace("\\", "/"))
        app.setStyleSheet(sheetString)

        contextCreator = lambda usdFile: self.GetResolverContext(usdFile)
        appController = AppController(arg_parse_result, contextCreator)

        return (app, appController)

    def __LaunchProcess(self, arg_parse_result):
        '''
        after the arguments have been parsed, launch the UI in a forked process
        '''
        # Initialize concurrency limit as early as possible so that it is
        # respected by subsequent imports.
        (app, appController) = self.LaunchPreamble(arg_parse_result)
        
        if arg_parse_result.quitAfterStartup:
            # Before we quit, process events one more time to make sure the
            # UI is fully populated (and to capture all the timing information
            # we'd want).
            app.processEvents()
            appController._cleanAndClose()
            return

        app.exec_()

