#
# Copyright 2017 Pixar
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
from __future__ import print_function

import argparse
import codecs
import contextlib
import ctypes
import datetime
import distutils
import fnmatch
import glob
import locale
import multiprocessing
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import zipfile

if sys.version_info.major >= 3:
    from urllib.request import urlopen
    from shutil import which
else:
    from urllib2 import urlopen

    # Doesn't deal with .bat / .cmd like shutil.which, but only option
    # available with stock python-2
    from distutils.spawn import find_executable as which

# Helpers for printing output
verbosity = 1

def Print(msg):
    if verbosity > 0:
        print(msg)

def PrintWarning(warning):
    if verbosity > 0:
        print("WARNING:", warning)

def PrintStatus(status):
    if verbosity >= 1:
        print("STATUS:", status)

def PrintInfo(info):
    if verbosity >= 2:
        print("INFO:", info)

def PrintCommandOutput(output):
    if verbosity >= 3:
        sys.stdout.write(output)

def PrintError(error):
    if verbosity >= 3 and sys.exc_info()[1] is not None:
        import traceback
        traceback.print_exc()
    print ("ERROR:", error)

# Helpers for determining platform
def Windows():
    return platform.system() == "Windows"
def Linux():
    return platform.system() == "Linux"
def MacOS():
    return platform.system() == "Darwin"

def Python3():
    return sys.version_info.major == 3

def GetLocale():
    return sys.stdout.encoding or locale.getdefaultlocale()[1] or "UTF-8"

def GetCommandOutput(command):
    """Executes the specified command and returns output or None."""
    try:
        return subprocess.check_output(
            shlex.split(command), 
            stderr=subprocess.STDOUT).decode(GetLocale(), 'replace').strip()
    except subprocess.CalledProcessError:
        pass
    return None

def GetXcodeDeveloperDirectory():
    """Returns the active developer directory as reported by 'xcode-select -p'.
    Returns None if none is set."""
    if not MacOS():
        return None

    return GetCommandOutput("xcode-select -p")

def GetVisualStudioCompilerAndVersion():
    """Returns a tuple containing the path to the Visual Studio compiler
    and a tuple for its version, e.g. (14, 0). If the compiler is not found
    or version number cannot be determined, returns None."""
    if not Windows():
        return None

    msvcCompiler = which('cl')
    if msvcCompiler:
        # VisualStudioVersion environment variable should be set by the
        # Visual Studio Command Prompt.
        match = re.search(
            r"(\d+)\.(\d+)",
            os.environ.get("VisualStudioVersion", ""))
        if match:
            return (msvcCompiler, tuple(int(v) for v in match.groups()))
    return None

def IsVisualStudioVersionOrGreater(desiredVersion):
    if not Windows():
        return False

    msvcCompilerAndVersion = GetVisualStudioCompilerAndVersion()
    if msvcCompilerAndVersion:
        _, version = msvcCompilerAndVersion
        return version >= desiredVersion
    return False

def IsVisualStudio2019OrGreater():
    VISUAL_STUDIO_2019_VERSION = (16, 0)
    return IsVisualStudioVersionOrGreater(VISUAL_STUDIO_2019_VERSION)

def IsVisualStudio2017OrGreater():
    VISUAL_STUDIO_2017_VERSION = (15, 0)
    return IsVisualStudioVersionOrGreater(VISUAL_STUDIO_2017_VERSION)

def IsVisualStudio2015OrGreater():
    VISUAL_STUDIO_2015_VERSION = (14, 0)
    return IsVisualStudioVersionOrGreater(VISUAL_STUDIO_2015_VERSION)

def IsMayaPython():
    """Determine whether we're running in Maya's version of Python. When 
    building against Maya's Python, there are some additional restrictions
    on what we're able to build."""
    try:
        import maya
        return True
    except:
        pass

    return False

def GetPythonInfo():
    """Returns a tuple containing the path to the Python executable, shared
    library, and include directory corresponding to the version of Python
    currently running. Returns None if any path could not be determined.

    This function is used to extract build information from the Python 
    interpreter used to launch this script. This information is used
    in the Boost and USD builds. By taking this approach we can support
    having USD builds for different Python versions built on the same
    machine. This is very useful, especially when developers have multiple
    versions installed on their machine, which is quite common now with 
    Python2 and Python3 co-existing.
    """
    # First we extract the information that can be uniformly dealt with across
    # the platforms:
    pythonExecPath = sys.executable
    pythonVersion = sysconfig.get_config_var("py_version_short")  # "2.7"
    pythonVersionNoDot = sysconfig.get_config_var("py_version_nodot") # "27"

    # Lib path is unfortunately special for each platform and there is no
    # config_var for it. But we can deduce it for each platform, and this
    # logic works for any Python version.
    def _GetPythonLibraryFilename():
        if Windows():
            return "python" + pythonVersionNoDot + ".lib"
        elif Linux():
            return sysconfig.get_config_var("LDLIBRARY")
        elif MacOS():
            return "libpython" + pythonVersion + ".dylib"
        else:
            raise RuntimeError("Platform not supported")

    # XXX: Handle the case where this script is being called using Maya's
    # Python since the sysconfig variables are set up differently in Maya.
    # Ideally we would not have any special Maya knowledge in here at all.
    if IsMayaPython():
        pythonBaseDir = sysconfig.get_config_var("base")

        # On Windows, the "base" path points to a "Python\" subdirectory
        # that contains the DLLs for site-package modules but not the
        # directories for the headers and .lib file we need -- those 
        # are one level up.
        if Windows():
            pythonBaseDir = os.path.dirname(pythonBaseDir)
        
        pythonIncludeDir = os.path.join(pythonBaseDir, "include",
                                        "python" + pythonVersion)
        pythonLibPath = os.path.join(pythonBaseDir, "lib",
                                     _GetPythonLibraryFilename())
    else:
        pythonIncludeDir = sysconfig.get_config_var("INCLUDEPY")
        if Windows():
            pythonBaseDir = sysconfig.get_config_var("base")
            pythonLibPath = os.path.join(pythonBaseDir, "libs",
                                         _GetPythonLibraryFilename())
        elif Linux():
            pythonLibDir = sysconfig.get_config_var("LIBDIR")
            pythonMultiarchSubdir = sysconfig.get_config_var("multiarchsubdir")
            if pythonMultiarchSubdir:
                pythonLibDir = pythonLibDir + pythonMultiarchSubdir
            pythonLibPath = os.path.join(pythonLibDir,
                                         _GetPythonLibraryFilename())
        elif MacOS():
            pythonBaseDir = sysconfig.get_config_var("base")
            pythonLibPath = os.path.join(pythonBaseDir, "lib",
                                         _GetPythonLibraryFilename())
        else:
            raise RuntimeError("Platform not supported")

    return (pythonExecPath, pythonLibPath, pythonIncludeDir, pythonVersion)

def GetCPUCount():
    try:
        return multiprocessing.cpu_count()
    except NotImplementedError:
        return 1

def Run(cmd, logCommandOutput = True):
    """Run the specified command in a subprocess."""
    PrintInfo('Running "{cmd}"'.format(cmd=cmd))

    with codecs.open("log.txt", "a", "utf-8") as logfile:
        logfile.write(datetime.datetime.now().strftime("%Y-%m-%d %H:%M"))
        logfile.write("\n")
        logfile.write(cmd)
        logfile.write("\n")

        # Let exceptions escape from subprocess calls -- higher level
        # code will handle them.
        if logCommandOutput:
            p = subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE, 
                                 stderr=subprocess.STDOUT)
            while True:
                l = p.stdout.readline().decode(GetLocale(), 'replace')
                if l:
                    logfile.write(l)
                    PrintCommandOutput(l)
                elif p.poll() is not None:
                    break
        else:
            p = subprocess.Popen(shlex.split(cmd))
            p.wait()

    if p.returncode != 0:
        # If verbosity >= 3, we'll have already been printing out command output
        # so no reason to print the log file again.
        if verbosity < 3:
            with open("log.txt", "r") as logfile:
                Print(logfile.read())
        raise RuntimeError("Failed to run '{cmd}'\nSee {log} for more details."
                           .format(cmd=cmd, log=os.path.abspath("log.txt")))

@contextlib.contextmanager
def CurrentWorkingDirectory(dir):
    """Context manager that sets the current working directory to the given
    directory and resets it to the original directory when closed."""
    curdir = os.getcwd()
    os.chdir(dir)
    try: yield
    finally: os.chdir(curdir)

def CopyFiles(context, src, dest):
    """Copy files like shutil.copy, but src may be a glob pattern."""
    filesToCopy = glob.glob(src)
    if not filesToCopy:
        raise RuntimeError("File(s) to copy {src} not found".format(src=src))

    instDestDir = os.path.join(context.instDir, dest)
    for f in filesToCopy:
        PrintCommandOutput("Copying {file} to {destDir}\n"
                           .format(file=f, destDir=instDestDir))
        shutil.copy(f, instDestDir)

def CopyDirectory(context, srcDir, destDir):
    """Copy directory like shutil.copytree."""
    instDestDir = os.path.join(context.instDir, destDir)
    if os.path.isdir(instDestDir):
        shutil.rmtree(instDestDir)    

    PrintCommandOutput("Copying {srcDir} to {destDir}\n"
                       .format(srcDir=srcDir, destDir=instDestDir))
    shutil.copytree(srcDir, instDestDir)

def FormatMultiProcs(numJobs, generator):
    tag = "-j"
    if generator:
        if "Visual Studio" in generator:
            tag = "/M:"
        elif "Xcode" in generator:
            tag = "-j "

    return "{tag}{procs}".format(tag=tag, procs=numJobs)

def RunCMake(context, force, extraArgs = None):
    """Invoke CMake to configure, build, and install a library whose 
    source code is located in the current working directory."""
    # Create a directory for out-of-source builds in the build directory
    # using the name of the current working directory.
    srcDir = os.getcwd()
    instDir = (context.usdInstDir if srcDir == context.usdSrcDir
               else context.instDir)
    buildDir = os.path.join(context.buildDir, os.path.split(srcDir)[1])
    if force and os.path.isdir(buildDir):
        shutil.rmtree(buildDir)

    if not os.path.isdir(buildDir):
        os.makedirs(buildDir)

    generator = context.cmakeGenerator

    # On Windows, we need to explicitly specify the generator to ensure we're
    # building a 64-bit project. (Surely there is a better way to do this?)
    # TODO: figure out exactly what "vcvarsall.bat x64" sets to force x64
    if generator is None and Windows():
        if IsVisualStudio2019OrGreater():
            generator = "Visual Studio 16 2019"
        elif IsVisualStudio2017OrGreater():
            generator = "Visual Studio 15 2017 Win64"
        else:
            generator = "Visual Studio 14 2015 Win64"

    if generator is not None:
        generator = '-G "{gen}"'.format(gen=generator)

    if IsVisualStudio2019OrGreater():
        generator = generator + " -A x64"

    toolset = context.cmakeToolset
    if toolset is not None:
        toolset = '-T "{toolset}"'.format(toolset=toolset)

    # On MacOS, enable the use of @rpath for relocatable builds.
    osx_rpath = None
    if MacOS():
        osx_rpath = "-DCMAKE_MACOSX_RPATH=ON"

    # We use -DCMAKE_BUILD_TYPE for single-configuration generators 
    # (Ninja, make), and --config for multi-configuration generators 
    # (Visual Studio); technically we don't need BOTH at the same
    # time, but specifying both is simpler than branching
    config=("Debug" if context.buildDebug else "Release")

    with CurrentWorkingDirectory(buildDir):
        Run('cmake '
            '-DCMAKE_INSTALL_PREFIX="{instDir}" '
            '-DCMAKE_PREFIX_PATH="{depsInstDir}" '
            '-DCMAKE_BUILD_TYPE={config} '
            '{osx_rpath} '
            '{generator} '
            '{toolset} '
            '{extraArgs} '
            '"{srcDir}"'
            .format(instDir=instDir,
                    depsInstDir=context.instDir,
                    config=config,
                    srcDir=srcDir,
                    osx_rpath=(osx_rpath or ""),
                    generator=(generator or ""),
                    toolset=(toolset or ""),
                    extraArgs=(" ".join(extraArgs) if extraArgs else "")))
        Run("cmake --build . --config {config} --target install -- {multiproc}"
            .format(config=config,
                    multiproc=FormatMultiProcs(context.numJobs, generator)))

def GetCMakeVersion():
    """
    Returns the CMake version as tuple of integers (major, minor) or
    (major, minor, patch) or None if an error occured while launching cmake and
    parsing its output.
    """

    output_string = GetCommandOutput("cmake --version")
    if not output_string:
        PrintWarning("Could not determine cmake version -- please install it "
                     "and adjust your PATH")
        return None

    # cmake reports, e.g., "... version 3.14.3"
    match = re.search(r"version (\d+)\.(\d+)(\.(\d+))?", output_string)
    if not match:
        PrintWarning("Could not determine cmake version")
        return None

    major, minor, patch_group, patch = match.groups()
    if patch_group is None:
        return (int(major), int(minor))
    else:
        return (int(major), int(minor), int(patch))

def PatchFile(filename, patches, multiLineMatches=False):
    """Applies patches to the specified file. patches is a list of tuples
    (old string, new string)."""
    if multiLineMatches:
        oldLines = [open(filename, 'r').read()]
    else:
        oldLines = open(filename, 'r').readlines()
    newLines = oldLines
    for (oldString, newString) in patches:
        newLines = [s.replace(oldString, newString) for s in newLines]
    if newLines != oldLines:
        PrintInfo("Patching file {filename} (original in {oldFilename})..."
                  .format(filename=filename, oldFilename=filename + ".old"))
        shutil.copy(filename, filename + ".old")
        open(filename, 'w').writelines(newLines)

def DownloadFileWithCurl(url, outputFilename):
    # Don't log command output so that curl's progress
    # meter doesn't get written to the log file.
    Run("curl {progress} -L -o {filename} {url}".format(
        progress="-#" if verbosity >= 2 else "-s",
        filename=outputFilename, url=url), 
        logCommandOutput=False)

def DownloadFileWithPowershell(url, outputFilename):
    # It's important that we specify to use TLS v1.2 at least or some
    # of the downloads will fail.
    cmd = "powershell [Net.ServicePointManager]::SecurityProtocol = \
            [Net.SecurityProtocolType]::Tls12; \"(new-object \
            System.Net.WebClient).DownloadFile('{url}', '{filename}')\""\
            .format(filename=outputFilename, url=url)

    Run(cmd,logCommandOutput=False)

def DownloadFileWithUrllib(url, outputFilename):
    r = urlopen(url)
    with open(outputFilename, "wb") as outfile:
        outfile.write(r.read())

def DownloadURL(url, context, force, extractDir = None, 
        dontExtract = None):
    """Download and extract the archive file at given URL to the
    source directory specified in the context. 

    dontExtract may be a sequence of path prefixes that will
    be excluded when extracting the archive.

    Returns the absolute path to the directory where files have 
    been extracted."""
    with CurrentWorkingDirectory(context.srcDir):
        # Extract filename from URL and see if file already exists. 
        filename = url.split("/")[-1]       
        if force and os.path.exists(filename):
            os.remove(filename)

        if os.path.exists(filename):
            PrintInfo("{0} already exists, skipping download"
                      .format(os.path.abspath(filename)))
        else:
            PrintInfo("Downloading {0} to {1}"
                      .format(url, os.path.abspath(filename)))

            # To work around occasional hiccups with downloading from websites
            # (SSL validation errors, etc.), retry a few times if we don't
            # succeed in downloading the file.
            maxRetries = 5
            lastError = None

            # Download to a temporary file and rename it to the expected
            # filename when complete. This ensures that incomplete downloads
            # will be retried if the script is run again.
            tmpFilename = filename + ".tmp"
            if os.path.exists(tmpFilename):
                os.remove(tmpFilename)

            for i in range(maxRetries):
                try:
                    context.downloader(url, tmpFilename)
                    break
                except Exception as e:
                    PrintCommandOutput("Retrying download due to error: {err}\n"
                                       .format(err=e))
                    lastError = e
            else:
                errorMsg = str(lastError)
                if "SSL: TLSV1_ALERT_PROTOCOL_VERSION" in errorMsg:
                    errorMsg += ("\n\n"
                                 "Your OS or version of Python may not support "
                                 "TLS v1.2+, which is required for downloading "
                                 "files from certain websites. This support "
                                 "was added in Python 2.7.9."
                                 "\n\n"
                                 "You can use curl to download dependencies "
                                 "by installing it in your PATH and re-running "
                                 "this script.")
                raise RuntimeError("Failed to download {url}: {err}"
                                   .format(url=url, err=errorMsg))

            shutil.move(tmpFilename, filename)

        # Open the archive and retrieve the name of the top-most directory.
        # This assumes the archive contains a single directory with all
        # of the contents beneath it, unless a specific extractDir is specified,
        # which is to be used.
        archive = None
        rootDir = None
        members = None
        try:
            if tarfile.is_tarfile(filename):
                archive = tarfile.open(filename)
                if extractDir:
                    rootDir = extractDir
                else:
                    rootDir = archive.getnames()[0].split('/')[0]
                if dontExtract != None:
                    members = (m for m in archive.getmembers() 
                               if not any((fnmatch.fnmatch(m.name, p)
                                           for p in dontExtract)))
            elif zipfile.is_zipfile(filename):
                archive = zipfile.ZipFile(filename)
                if extractDir:
                    rootDir = extractDir
                else:
                    rootDir = archive.namelist()[0].split('/')[0]
                if dontExtract != None:
                    members = (m for m in archive.getnames() 
                               if not any((fnmatch.fnmatch(m, p)
                                           for p in dontExtract)))
            else:
                raise RuntimeError("unrecognized archive file type")

            with archive:
                extractedPath = os.path.abspath(rootDir)
                if force and os.path.isdir(extractedPath):
                    shutil.rmtree(extractedPath)

                if os.path.isdir(extractedPath):
                    PrintInfo("Directory {0} already exists, skipping extract"
                              .format(extractedPath))
                else:
                    PrintInfo("Extracting archive to {0}".format(extractedPath))

                    # Extract to a temporary directory then move the contents
                    # to the expected location when complete. This ensures that
                    # incomplete extracts will be retried if the script is run
                    # again.
                    tmpExtractedPath = os.path.abspath("extract_dir")
                    if os.path.isdir(tmpExtractedPath):
                        shutil.rmtree(tmpExtractedPath)

                    archive.extractall(tmpExtractedPath, members=members)

                    shutil.move(os.path.join(tmpExtractedPath, rootDir),
                                extractedPath)
                    shutil.rmtree(tmpExtractedPath)

                return extractedPath
        except Exception as e:
            # If extraction failed for whatever reason, assume the
            # archive file was bad and move it aside so that re-running
            # the script will try downloading and extracting again.
            shutil.move(filename, filename + ".bad")
            raise RuntimeError("Failed to extract archive {filename}: {err}"
                               .format(filename=filename, err=e))

############################################################
# 3rd-Party Dependencies

AllDependencies = list()
AllDependenciesByName = dict()

class Dependency(object):
    def __init__(self, name, installer, *files):
        self.name = name
        self.installer = installer
        self.filesToCheck = files

        AllDependencies.append(self)
        AllDependenciesByName.setdefault(name.lower(), self)

    def Exists(self, context):
        return all([os.path.isfile(os.path.join(context.instDir, f))
                    for f in self.filesToCheck])

class PythonDependency(object):
    def __init__(self, name, getInstructions, moduleNames):
        self.name = name
        self.getInstructions = getInstructions
        self.moduleNames = moduleNames

    def Exists(self, context):
        # If one of the modules in our list imports successfully, we are good.
        for moduleName in self.moduleNames:
            try:
                pyModule = __import__(moduleName)
                return True
            except:
                pass

        return False

def AnyPythonDependencies(deps):
    return any([type(d) is PythonDependency for d in deps])

############################################################
# zlib

ZLIB_URL = "https://github.com/madler/zlib/archive/v1.2.11.zip"

def InstallZlib(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(ZLIB_URL, context, force)):
        RunCMake(context, force, buildArgs)

ZLIB = Dependency("zlib", InstallZlib, "include/zlib.h")
        
############################################################
# boost

if MacOS():
    BOOST_URL = "https://downloads.sourceforge.net/project/boost/boost/1.70.0/boost_1_70_0.tar.gz"
    BOOST_VERSION_FILE = "include/boost/version.hpp"
elif Linux():
    if Python3():
        BOOST_URL = "https://downloads.sourceforge.net/project/boost/boost/1.70.0/boost_1_70_0.tar.gz"
    else:
        BOOST_URL = "https://downloads.sourceforge.net/project/boost/boost/1.66.0/boost_1_66_0.tar.gz"
    BOOST_VERSION_FILE = "include/boost/version.hpp"
elif Windows():
    # The default installation of boost on Windows puts headers in a versioned 
    # subdirectory, which we have to account for here. In theory, specifying 
    # "layout=system" would make the Windows install match Linux/MacOS, but that 
    # causes problems for other dependencies that look for boost.
    #
    # boost 1.70 is required for Visual Studio 2019. For simplicity, we use
    # this version for all older Visual Studio versions as well.
    BOOST_URL = "https://downloads.sourceforge.net/project/boost/boost/1.70.0/boost_1_70_0.tar.gz"
    BOOST_VERSION_FILE = "include/boost-1_70/boost/version.hpp"

def InstallBoost_Helper(context, force, buildArgs):
    # Documentation files in the boost archive can have exceptionally
    # long paths. This can lead to errors when extracting boost on Windows,
    # since paths are limited to 260 characters by default on that platform.
    # To avoid this, we skip extracting all documentation.
    #
    # For some examples, see: https://svn.boost.org/trac10/ticket/11677
    dontExtract = ["*/doc/*", "*/libs/*/doc/*"]

    with CurrentWorkingDirectory(DownloadURL(BOOST_URL, context, force, 
                                             dontExtract=dontExtract)):
        bootstrap = "bootstrap.bat" if Windows() else "./bootstrap.sh"
        Run('{bootstrap} --prefix="{instDir}"'
            .format(bootstrap=bootstrap, instDir=context.instDir))

        # b2 supports at most -j64 and will error if given a higher value.
        num_procs = min(64, context.numJobs)

        b2_settings = [
            '--prefix="{instDir}"'.format(instDir=context.instDir),
            '--build-dir="{buildDir}"'.format(buildDir=context.buildDir),
            '-j{procs}'.format(procs=num_procs),
            'address-model=64',
            'link=shared',
            'runtime-link=shared',
            'threading=multi', 
            'variant={variant}'
                .format(variant="debug" if context.buildDebug else "release"),
            '--with-atomic',
            '--with-program_options',
            '--with-regex',
            '--ignore-site-config'
        ]

        if context.buildPython:
            b2_settings.append("--with-python")
            pythonInfo = GetPythonInfo()
            if Windows():
                # Unfortunately Boost build scripts require the Python folder 
                # that contains the executable on Windows
                pythonPath = os.path.dirname(pythonInfo[0])
            else:
                # While other platforms want the complete executable path
                pythonPath = pythonInfo[0]
            # This is the only platform-independent way to configure these
            # settings correctly and robustly for the Boost jam build system.
            # There are Python config arguments that can be passed to bootstrap 
            # but those are not available in boostrap.bat (Windows) so we must 
            # take the following approach:
            projectPath = 'python-config.jam'
            with open(projectPath, 'w') as projectFile:
                # Note that we must escape any special characters, like 
                # backslashes for jam, hence the mods below for the path 
                # arguments. Also, if the path contains spaces jam will not
                # handle them well. Surround the path parameters in quotes.
                line = 'using python : %s : "%s" : "%s" ;\n' % (pythonInfo[3], 
                       pythonPath.replace('\\', '\\\\'), 
                       pythonInfo[2].replace('\\', '\\\\'))
                projectFile.write(line)
            b2_settings.append("--user-config=python-config.jam")

        if context.buildOIIO:
            b2_settings.append("--with-date_time")

        if context.buildOIIO or context.enableOpenVDB:
            b2_settings.append("--with-system")
            b2_settings.append("--with-thread")

        if context.enableOpenVDB:
            b2_settings.append("--with-iostreams")

            # b2 with -sNO_COMPRESSION=1 fails with the following error message:
            #     error: at [...]/boost_1_61_0/tools/build/src/kernel/modules.jam:107
            #     error: Unable to find file or target named
            #     error:     '/zlib//zlib'
            #     error: referred to from project at
            #     error:     'libs/iostreams/build'
            #     error: could not resolve project reference '/zlib'

            # But to avoid an extra library dependency, we can still explicitly
            # exclude the bzip2 compression from boost_iostreams (note that
            # OpenVDB uses blosc compression).
            b2_settings.append("-sNO_BZIP2=1")

        if context.buildOIIO:
            b2_settings.append("--with-filesystem")

        if force:
            b2_settings.append("-a")

        if Windows():
            # toolset parameter for Visual Studio documented here:
            # https://github.com/boostorg/build/blob/develop/src/tools/msvc.jam
            if context.cmakeToolset == "v142":
                b2_settings.append("toolset=msvc-14.2")
            elif context.cmakeToolset == "v141":
                b2_settings.append("toolset=msvc-14.1")
            elif context.cmakeToolset == "v140":
                b2_settings.append("toolset=msvc-14.0")
            elif IsVisualStudio2019OrGreater():
                b2_settings.append("toolset=msvc-14.2")
            elif IsVisualStudio2017OrGreater():
                b2_settings.append("toolset=msvc-14.1")
            else:
                b2_settings.append("toolset=msvc-14.0")

        if MacOS():
            # Must specify toolset=clang to ensure install_name for boost
            # libraries includes @rpath
            b2_settings.append("toolset=clang")

        # Add on any user-specified extra arguments.
        b2_settings += buildArgs

        b2 = "b2" if Windows() else "./b2"
        Run('{b2} {options} install'
            .format(b2=b2, options=" ".join(b2_settings)))

def InstallBoost(context, force, buildArgs):
    # Boost's build system will install the version.hpp header before
    # building its libraries. We make sure to remove it in case of
    # any failure to ensure that the build script detects boost as a 
    # dependency to build the next time it's run.
    try:
        InstallBoost_Helper(context, force, buildArgs)
    except:
        versionHeader = os.path.join(context.instDir, BOOST_VERSION_FILE)
        if os.path.isfile(versionHeader):
            try: os.remove(versionHeader)
            except: pass
        raise

BOOST = Dependency("boost", InstallBoost, BOOST_VERSION_FILE)

############################################################
# Intel TBB

if Windows():
    TBB_URL = "https://github.com/oneapi-src/oneTBB/releases/download/2018_U6/tbb2018_20180822oss_win.zip"
elif MacOS():
    # On MacOS we experience various crashes in tests during teardown
    # starting with 2018 Update 2. Until we figure that out, we use
    # 2018 Update 1 on this platform.
    TBB_URL = "https://github.com/oneapi-src/oneTBB/archive/2018_U1.tar.gz"
else:
    TBB_URL = "https://github.com/oneapi-src/oneTBB/archive/2018_U6.tar.gz"

def InstallTBB(context, force, buildArgs):
    if Windows():
        InstallTBB_Windows(context, force, buildArgs)
    elif Linux() or MacOS():
        InstallTBB_LinuxOrMacOS(context, force, buildArgs)

def InstallTBB_Windows(context, force, buildArgs):
    TBB_ROOT_DIR_NAME = "tbb2018_20180822oss"
    with CurrentWorkingDirectory(DownloadURL(TBB_URL, context, force, 
        TBB_ROOT_DIR_NAME)):
        # On Windows, we simply copy headers and pre-built DLLs to
        # the appropriate location.

        if buildArgs:
            PrintWarning("Ignoring build arguments {}, TBB is "
                         "not built from source on this platform."
                         .format(buildArgs))

        CopyFiles(context, "bin\\intel64\\vc14\\*.*", "bin")
        CopyFiles(context, "lib\\intel64\\vc14\\*.*", "lib")
        CopyDirectory(context, "include\\serial", "include\\serial")
        CopyDirectory(context, "include\\tbb", "include\\tbb")

def InstallTBB_LinuxOrMacOS(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(TBB_URL, context, force)):
        # Note: TBB installation fails on OSX when cuda is installed, a 
        # suggested fix:
        # https://github.com/spack/spack/issues/6000#issuecomment-358817701
        if MacOS():
            PatchFile("build/macos.inc", 
                    [("shell clang -v ", "shell clang --version ")])
        # TBB does not support out-of-source builds in a custom location.
        Run('make -j{procs} {buildArgs}'
            .format(procs=context.numJobs, 
                    buildArgs=" ".join(buildArgs)))

        # Install both release and debug builds. USD requires the debug
        # libraries when building in debug mode, and installing both
        # makes it easier for users to install dependencies in some
        # location that can be shared by both release and debug USD
        # builds. Plus, the TBB build system builds both versions anyway.
        CopyFiles(context, "build/*_release/libtbb*.*", "lib")
        CopyFiles(context, "build/*_debug/libtbb*.*", "lib")
        CopyDirectory(context, "include/serial", "include/serial")
        CopyDirectory(context, "include/tbb", "include/tbb")

TBB = Dependency("TBB", InstallTBB, "include/tbb/tbb.h")

############################################################
# JPEG

if Windows():
    JPEG_URL = "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/1.5.1.zip"
else:
    JPEG_URL = "https://www.ijg.org/files/jpegsrc.v9b.tar.gz"

def InstallJPEG(context, force, buildArgs):
    if Windows():
        InstallJPEG_Turbo(context, force, buildArgs)
    else:
        InstallJPEG_Lib(context, force, buildArgs)

def InstallJPEG_Turbo(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(JPEG_URL, context, force)):
        RunCMake(context, force, buildArgs)

def InstallJPEG_Lib(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(JPEG_URL, context, force)):
        Run('./configure --prefix="{instDir}" '
            '--disable-static --enable-shared '
            '{buildArgs}'
            .format(instDir=context.instDir,
                    buildArgs=" ".join(buildArgs)))
        Run('make -j{procs} install'
            .format(procs=context.numJobs))

JPEG = Dependency("JPEG", InstallJPEG, "include/jpeglib.h")
        
############################################################
# TIFF

TIFF_URL = "https://download.osgeo.org/libtiff/tiff-4.0.7.zip"

def InstallTIFF(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(TIFF_URL, context, force)):
        # libTIFF has a build issue on Windows where tools/tiffgt.c
        # unconditionally includes unistd.h, which does not exist.
        # To avoid this, we patch the CMakeLists.txt to skip building
        # the tools entirely. We do this on Linux and MacOS as well
        # to avoid requiring some GL and X dependencies.
        #
        # We also need to skip building tests, since they rely on 
        # the tools we've just elided.
        PatchFile("CMakeLists.txt", 
                   [("add_subdirectory(tools)", "# add_subdirectory(tools)"),
                    ("add_subdirectory(test)", "# add_subdirectory(test)")])

        # The libTIFF CMakeScript says the ld-version-script 
        # functionality is only for compilers using GNU ld on 
        # ELF systems or systems which provide an emulation; therefore
        # skipping it completely on mac and windows.
        if MacOS() or Windows():
            extraArgs = ["-Dld-version-script=OFF"]
        else:
            extraArgs = []
        extraArgs += buildArgs
        RunCMake(context, force, extraArgs)

TIFF = Dependency("TIFF", InstallTIFF, "include/tiff.h")

############################################################
# PNG

PNG_URL = "https://downloads.sourceforge.net/project/libpng/libpng16/older-releases/1.6.29/libpng-1.6.29.tar.gz"

def InstallPNG(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(PNG_URL, context, force)):
        RunCMake(context, force, buildArgs)

PNG = Dependency("PNG", InstallPNG, "include/png.h")

############################################################
# IlmBase/OpenEXR

OPENEXR_URL = "https://github.com/AcademySoftwareFoundation/openexr/archive/v2.3.0.zip"

def InstallOpenEXR(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(OPENEXR_URL, context, force)):
        RunCMake(context, force, 
                 ['-DOPENEXR_BUILD_PYTHON_LIBS=OFF',
                  '-DOPENEXR_ENABLE_TESTS=OFF'] + buildArgs)

OPENEXR = Dependency("OpenEXR", InstallOpenEXR, "include/OpenEXR/ImfVersion.h")

############################################################
# Ptex

PTEX_URL = "https://github.com/wdas/ptex/archive/v2.1.33.zip"

def InstallPtex(context, force, buildArgs):
    if Windows():
        InstallPtex_Windows(context, force, buildArgs)
    else:
        InstallPtex_LinuxOrMacOS(context, force, buildArgs)

def InstallPtex_Windows(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(PTEX_URL, context, force)):
        # Ptex has a bug where the import library for the dynamic library and
        # the static library both get the same name, Ptex.lib, and as a
        # result one clobbers the other. We hack the appropriate CMake
        # file to prevent that. Since we don't need the static library we'll
        # rename that.
        #
        # In addition src\tests\CMakeLists.txt adds -DPTEX_STATIC to the 
        # compiler but links tests against the dynamic library, causing the 
        # links to fail. We patch the file to not add the -DPTEX_STATIC
        PatchFile('src\\ptex\\CMakeLists.txt', 
                  [("set_target_properties(Ptex_static PROPERTIES OUTPUT_NAME Ptex)",
                    "set_target_properties(Ptex_static PROPERTIES OUTPUT_NAME Ptexs)")])
        PatchFile('src\\tests\\CMakeLists.txt',
                  [("add_definitions(-DPTEX_STATIC)", 
                    "# add_definitions(-DPTEX_STATIC)")])

        # Patch Ptex::String to export symbol for operator<< 
        # This is required for newer versions of OIIO, which make use of the
        # this operator on Windows platform specifically.
        PatchFile('src\\ptex\\Ptexture.h',
                  [("std::ostream& operator << (std::ostream& stream, const Ptex::String& str);",
                    "PTEXAPI std::ostream& operator << (std::ostream& stream, const Ptex::String& str);")])


        RunCMake(context, force, buildArgs)

def InstallPtex_LinuxOrMacOS(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(PTEX_URL, context, force)):
        RunCMake(context, force, buildArgs)

PTEX = Dependency("Ptex", InstallPtex, "include/PtexVersion.h")

############################################################
# BLOSC (Compression used by OpenVDB)

# Using latest blosc since neither the version OpenVDB recommends
# (1.5) nor the version we test against (1.6.1) compile on Mac OS X
# Sierra (10.12) or Mojave (10.14).
BLOSC_URL = "https://github.com/Blosc/c-blosc/archive/v1.17.0.zip"

def InstallBLOSC(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(BLOSC_URL, context, force)):
        RunCMake(context, force, buildArgs)

BLOSC = Dependency("Blosc", InstallBLOSC, "include/blosc.h")

############################################################
# OpenVDB

# Using version 6.1.0 since it has reworked its CMake files so that
# there are better options to not compile the OpenVDB binaries and to
# not require additional dependencies such as GLFW. Note that version
# 6.1.0 does require CMake 3.3 though.

OPENVDB_URL = "https://github.com/AcademySoftwareFoundation/openvdb/archive/v6.1.0.zip"

def InstallOpenVDB(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(OPENVDB_URL, context, force)):
        extraArgs = [
            '-DOPENVDB_BUILD_PYTHON_MODULE=OFF',
            '-DOPENVDB_BUILD_BINARIES=OFF',
            '-DOPENVDB_BUILD_UNITTESTS=OFF'
        ]

        # Make sure to use boost installed by the build script and not any
        # system installed boost
        extraArgs.append('-DBoost_NO_BOOST_CMAKE=On')
        extraArgs.append('-DBoost_NO_SYSTEM_PATHS=True')

        extraArgs.append('-DBLOSC_ROOT="{instDir}"'
                         .format(instDir=context.instDir))
        extraArgs.append('-DTBB_ROOT="{instDir}"'
                         .format(instDir=context.instDir))
        # OpenVDB needs Half type from IlmBase
        extraArgs.append('-DILMBASE_ROOT="{instDir}"'
                         .format(instDir=context.instDir))

        # Add on any user-specified extra arguments.
        extraArgs += buildArgs

        RunCMake(context, force, extraArgs)

OPENVDB = Dependency("OpenVDB", InstallOpenVDB, "include/openvdb/openvdb.h")

############################################################
# OpenImageIO

OIIO_URL = "https://github.com/OpenImageIO/oiio/archive/Release-2.1.16.0.zip"

def InstallOpenImageIO(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(OIIO_URL, context, force)):
        extraArgs = ['-DOIIO_BUILD_TOOLS=OFF',
                     '-DOIIO_BUILD_TESTS=OFF',
                     '-DUSE_PYTHON=OFF',
                     '-DSTOP_ON_WARNING=OFF']

        # OIIO's FindOpenEXR module circumvents CMake's normal library 
        # search order, which causes versions of OpenEXR installed in
        # /usr/local or other hard-coded locations in the module to
        # take precedence over the version we've built, which would 
        # normally be picked up when we specify CMAKE_PREFIX_PATH. 
        # This may lead to undefined symbol errors at build or runtime. 
        # So, we explicitly specify the OpenEXR we want to use here.
        extraArgs.append('-DOPENEXR_ROOT="{instDir}"'
                         .format(instDir=context.instDir))

        # If Ptex support is disabled in USD, disable support in OpenImageIO
        # as well. This ensures OIIO doesn't accidentally pick up a Ptex
        # library outside of our build.
        if not context.enablePtex:
            extraArgs.append('-DUSE_PTEX=OFF')

        # Make sure to use boost installed by the build script and not any
        # system installed boost
        extraArgs.append('-DBoost_NO_BOOST_CMAKE=On')
        extraArgs.append('-DBoost_NO_SYSTEM_PATHS=True')

        # Add on any user-specified extra arguments.
        extraArgs += buildArgs

        RunCMake(context, force, extraArgs)

OPENIMAGEIO = Dependency("OpenImageIO", InstallOpenImageIO,
                         "include/OpenImageIO/oiioversion.h")

############################################################
# OpenColorIO

OCIO_URL = "https://github.com/imageworks/OpenColorIO/archive/v1.1.0.zip"

def InstallOpenColorIO(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(OCIO_URL, context, force)):
        extraArgs = ['-DOCIO_BUILD_TRUELIGHT=OFF',
                     '-DOCIO_BUILD_APPS=OFF',
                     '-DOCIO_BUILD_NUKE=OFF',
                     '-DOCIO_BUILD_DOCS=OFF',
                     '-DOCIO_BUILD_TESTS=OFF',
                     '-DOCIO_BUILD_PYGLUE=OFF',
                     '-DOCIO_BUILD_JNIGLUE=OFF',
                     '-DOCIO_STATIC_JNIGLUE=OFF']

        # The OCIO build treats all warnings as errors but several come up
        # on various platforms, including:
        # - On gcc6, v1.1.0 emits many -Wdeprecated-declaration warnings for
        #   std::auto_ptr
        # - On clang, v1.1.0 emits a -Wself-assign-field warning. This is fixed
        #   in https://github.com/AcademySoftwareFoundation/OpenColorIO/commit/0be465feb9ac2d34bd8171f30909b276c1efa996
        #
        # To avoid build failures we force all warnings off for this build.
        if GetVisualStudioCompilerAndVersion():
            # This doesn't work because CMake stores default flags for
            # MSVC in CMAKE_CXX_FLAGS and this would overwrite them.
            # However, we don't seem to get any warnings on Windows
            # (at least with VS2015 and 2017).
            # extraArgs.append('-DCMAKE_CXX_FLAGS=/w') 
            pass
        else:
            extraArgs.append('-DCMAKE_CXX_FLAGS=-w')

        # Add on any user-specified extra arguments.
        extraArgs += buildArgs

        RunCMake(context, force, extraArgs)

OPENCOLORIO = Dependency("OpenColorIO", InstallOpenColorIO,
                         "include/OpenColorIO/OpenColorABI.h")

############################################################
# OpenSubdiv

OPENSUBDIV_URL = "https://github.com/PixarAnimationStudios/OpenSubdiv/archive/v3_4_3.zip"

def InstallOpenSubdiv(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(OPENSUBDIV_URL, context, force)):
        extraArgs = [
            '-DNO_EXAMPLES=ON',
            '-DNO_TUTORIALS=ON',
            '-DNO_REGRESSION=ON',
            '-DNO_DOC=ON',
            '-DNO_OMP=ON',
            '-DNO_CUDA=ON',
            '-DNO_OPENCL=ON',
            '-DNO_DX=ON',
            '-DNO_TESTS=ON',
            '-DNO_GLEW=ON',
            '-DNO_GLFW=ON',
        ]

        # If Ptex support is disabled in USD, disable support in OpenSubdiv
        # as well. This ensures OSD doesn't accidentally pick up a Ptex
        # library outside of our build.
        if not context.enablePtex:
            extraArgs.append('-DNO_PTEX=ON')

        # NOTE: For now, we disable TBB in our OpenSubdiv build.
        # This avoids an issue where OpenSubdiv will link against
        # all TBB libraries it finds, including libtbbmalloc and
        # libtbbmalloc_proxy. On Linux and MacOS, this has the
        # unwanted effect of replacing the system allocator with
        # tbbmalloc.
        extraArgs.append('-DNO_TBB=ON')

        # Add on any user-specified extra arguments.
        extraArgs += buildArgs

        # OpenSubdiv seems to error when building on windows w/ Ninja...
        # ...so just use the default generator (ie, Visual Studio on Windows)
        # until someone can sort it out
        oldGenerator = context.cmakeGenerator
        if oldGenerator == "Ninja" and Windows():
            context.cmakeGenerator = None

        # OpenSubdiv 3.3 and later on MacOS occasionally runs into build
        # failures with multiple build jobs. Workaround this by using
        # just 1 job for now. See:
        # https://github.com/PixarAnimationStudios/OpenSubdiv/issues/1194
        oldNumJobs = context.numJobs
        if MacOS():
            context.numJobs = 1

        try:
            RunCMake(context, force, extraArgs)
        finally:
            context.cmakeGenerator = oldGenerator
            context.numJobs = oldNumJobs

OPENSUBDIV = Dependency("OpenSubdiv", InstallOpenSubdiv, 
                        "include/opensubdiv/version.h")

############################################################
# PyOpenGL

def GetPyOpenGLInstructions():
    return ('PyOpenGL is not installed. If you have pip '
            'installed, run "pip install PyOpenGL" to '
            'install it, then re-run this script.\n'
            'If PyOpenGL is already installed, you may need to '
            'update your PYTHONPATH to indicate where it is '
            'located.')

PYOPENGL = PythonDependency("PyOpenGL", GetPyOpenGLInstructions, 
                            moduleNames=["OpenGL"])

############################################################
# PySide

def GetPySideInstructions():
    # For licensing reasons, this script cannot install PySide itself.
    if Windows():
        # There is no distribution of PySide2 for Windows for Python 2.7.
        # So use PySide instead. See the following for more details:
        # https://wiki.qt.io/Qt_for_Python/Considerations#Missing_Windows_.2F_Python_2.7_release
        return ('PySide is not installed. If you have pip '
                'installed, run "pip install PySide" '
                'to install it, then re-run this script.\n'
                'If PySide is already installed, you may need to '
                'update your PYTHONPATH to indicate where it is '
                'located.')
    else:                       
        return ('PySide2 is not installed. If you have pip '
                'installed, run "pip install PySide2" '
                'to install it, then re-run this script.\n'
                'If PySide2 is already installed, you may need to '
                'update your PYTHONPATH to indicate where it is '
                'located.')

PYSIDE = PythonDependency("PySide", GetPySideInstructions,
                          moduleNames=["PySide", "PySide2"])

############################################################
# HDF5

HDF5_URL = "https://support.hdfgroup.org/ftp/HDF5/releases/hdf5-1.10/hdf5-1.10.0-patch1/src/hdf5-1.10.0-patch1.zip"

def InstallHDF5(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(HDF5_URL, context, force)):
        RunCMake(context, force,
                 ['-DBUILD_TESTING=OFF',
                  '-DHDF5_BUILD_TOOLS=OFF',
                  '-DHDF5_BUILD_EXAMPLES=OFF'] + buildArgs)
                 
HDF5 = Dependency("HDF5", InstallHDF5, "include/hdf5.h")

############################################################
# Alembic

ALEMBIC_URL = "https://github.com/alembic/alembic/archive/1.7.10.zip"

def InstallAlembic(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(ALEMBIC_URL, context, force)):
        cmakeOptions = ['-DUSE_BINARIES=OFF', '-DUSE_TESTS=OFF']
        if context.enableHDF5:
            # HDF5 requires the H5_BUILT_AS_DYNAMIC_LIB macro be defined if
            # it was built with CMake as a dynamic library.
            cmakeOptions += [
                '-DUSE_HDF5=ON',
                '-DHDF5_ROOT="{instDir}"'.format(instDir=context.instDir),
                '-DCMAKE_CXX_FLAGS="-D H5_BUILT_AS_DYNAMIC_LIB"']
        else:
           cmakeOptions += ['-DUSE_HDF5=OFF']
                 
        cmakeOptions += buildArgs

        RunCMake(context, force, cmakeOptions)

ALEMBIC = Dependency("Alembic", InstallAlembic, "include/Alembic/Abc/Base.h")

############################################################
# Draco

DRACO_URL = "https://github.com/google/draco/archive/master.zip"

def InstallDraco(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(DRACO_URL, context, force)):
        cmakeOptions = ['-DBUILD_USD_PLUGIN=ON']
        cmakeOptions += buildArgs
        RunCMake(context, force, cmakeOptions)

DRACO = Dependency("Draco", InstallDraco, "include/draco/compression/decode.h")

############################################################
# MaterialX

MATERIALX_URL = "https://github.com/materialx/MaterialX/archive/v1.38.0.zip"

def InstallMaterialX(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(MATERIALX_URL, context, force)):
        # USD requires MaterialX to be built as a shared library on Linux and MacOS
        # Currently MaterialX does not support shared builds on Windows
        cmakeOptions = []
        if Linux() or MacOS():
            cmakeOptions += ['-DMATERIALX_BUILD_SHARED_LIBS=ON']

        cmakeOptions += buildArgs;

        RunCMake(context, force, cmakeOptions)

MATERIALX = Dependency("MaterialX", InstallMaterialX, "include/MaterialXCore/Library.h")

############################################################
# Embree
# For MacOS we use version 3.7.0 to include a fix from Intel
# to build on Catalina.
if MacOS():
    EMBREE_URL = "https://github.com/embree/embree/archive/v3.7.0.tar.gz"
else:
    EMBREE_URL = "https://github.com/embree/embree/archive/v3.2.2.tar.gz"

def InstallEmbree(context, force, buildArgs):
    with CurrentWorkingDirectory(DownloadURL(EMBREE_URL, context, force)):
        extraArgs = [
            '-DTBB_ROOT={instDir}'.format(instDir=context.instDir),
            '-DEMBREE_TUTORIALS=OFF',
            '-DEMBREE_ISPC_SUPPORT=OFF'
        ]

        # By default Embree fails to build on Visual Studio 2015 due
        # to an internal compiler issue that is worked around via the
        # following flag. For more details see:
        # https://github.com/embree/embree/issues/157
        if IsVisualStudio2015OrGreater() and not IsVisualStudio2017OrGreater():
            extraArgs.append('-DCMAKE_CXX_FLAGS=/d2SSAOptimizer-')

        extraArgs += buildArgs

        RunCMake(context, force, extraArgs)

EMBREE = Dependency("Embree", InstallEmbree, "include/embree3/rtcore.h")                  

############################################################
# USD

def InstallUSD(context, force, buildArgs):
    with CurrentWorkingDirectory(context.usdSrcDir):
        extraArgs = []

        extraArgs.append('-DPXR_PREFER_SAFETY_OVER_SPEED=' + 
                         'ON' if context.safetyFirst else 'OFF')

        if context.buildPython:
            extraArgs.append('-DPXR_ENABLE_PYTHON_SUPPORT=ON')
            if Python3():
                extraArgs.append('-DPXR_USE_PYTHON_3=ON')

            # CMake has trouble finding the executable, library, and include
            # directories when there are multiple versions of Python installed.
            # This can lead to crashes due to USD being linked against one
            # version of Python but running through some other Python
            # interpreter version. This primarily shows up on macOS, as it's
            # common to have a Python install that's separate from the one
            # included with the system.
            #
            # To avoid this, we try to determine these paths from Python
            # itself rather than rely on CMake's heuristics.
            pythonInfo = GetPythonInfo()
            if pythonInfo:
                extraArgs.append('-DPYTHON_EXECUTABLE="{pyExecPath}"'
                                 .format(pyExecPath=pythonInfo[0]))
                extraArgs.append('-DPYTHON_LIBRARY="{pyLibPath}"'
                                 .format(pyLibPath=pythonInfo[1]))
                extraArgs.append('-DPYTHON_INCLUDE_DIR="{pyIncPath}"'
                                 .format(pyIncPath=pythonInfo[2]))
        else:
            extraArgs.append('-DPXR_ENABLE_PYTHON_SUPPORT=OFF')

        if context.buildShared:
            extraArgs.append('-DBUILD_SHARED_LIBS=ON')
        elif context.buildMonolithic:
            extraArgs.append('-DPXR_BUILD_MONOLITHIC=ON')

        if context.buildDebug:
            extraArgs.append('-DTBB_USE_DEBUG_BUILD=ON')
        else:
            extraArgs.append('-DTBB_USE_DEBUG_BUILD=OFF')
        
        if context.buildDocs:
            extraArgs.append('-DPXR_BUILD_DOCUMENTATION=ON')
        else:
            extraArgs.append('-DPXR_BUILD_DOCUMENTATION=OFF')
    
        if context.buildTests:
            extraArgs.append('-DPXR_BUILD_TESTS=ON')
        else:
            extraArgs.append('-DPXR_BUILD_TESTS=OFF')

        if context.buildExamples:
            extraArgs.append('-DPXR_BUILD_EXAMPLES=ON')
        else:
            extraArgs.append('-DPXR_BUILD_EXAMPLES=OFF')

        if context.buildTutorials:
            extraArgs.append('-DPXR_BUILD_TUTORIALS=ON')
        else:
            extraArgs.append('-DPXR_BUILD_TUTORIALS=OFF')

        if context.buildTools:
            extraArgs.append('-DPXR_BUILD_USD_TOOLS=ON')
        else:
            extraArgs.append('-DPXR_BUILD_USD_TOOLS=OFF')
            
        if context.buildImaging:
            extraArgs.append('-DPXR_BUILD_IMAGING=ON')
            if context.enablePtex:
                extraArgs.append('-DPXR_ENABLE_PTEX_SUPPORT=ON')
            else:
                extraArgs.append('-DPXR_ENABLE_PTEX_SUPPORT=OFF')

            if context.enableOpenVDB:
                extraArgs.append('-DPXR_ENABLE_OPENVDB_SUPPORT=ON')
            else:
                extraArgs.append('-DPXR_ENABLE_OPENVDB_SUPPORT=OFF')

            if context.buildEmbree:
                extraArgs.append('-DPXR_BUILD_EMBREE_PLUGIN=ON')
            else:
                extraArgs.append('-DPXR_BUILD_EMBREE_PLUGIN=OFF')

            if context.buildPrman:
                if context.prmanLocation:
                    extraArgs.append('-DRENDERMAN_LOCATION="{location}"'
                                     .format(location=context.prmanLocation))
                extraArgs.append('-DPXR_BUILD_PRMAN_PLUGIN=ON')
            else:
                extraArgs.append('-DPXR_BUILD_PRMAN_PLUGIN=OFF')                
            
            if context.buildOIIO:
                extraArgs.append('-DPXR_BUILD_OPENIMAGEIO_PLUGIN=ON')
            else:
                extraArgs.append('-DPXR_BUILD_OPENIMAGEIO_PLUGIN=OFF')
                
            if context.buildOCIO:
                extraArgs.append('-DPXR_BUILD_OPENCOLORIO_PLUGIN=ON')
            else:
                extraArgs.append('-DPXR_BUILD_OPENCOLORIO_PLUGIN=OFF')

        else:
            extraArgs.append('-DPXR_BUILD_IMAGING=OFF')

        if context.buildUsdImaging:
            extraArgs.append('-DPXR_BUILD_USD_IMAGING=ON')
        else:
            extraArgs.append('-DPXR_BUILD_USD_IMAGING=OFF')

        if context.buildUsdview:
            extraArgs.append('-DPXR_BUILD_USDVIEW=ON')
        else:
            extraArgs.append('-DPXR_BUILD_USDVIEW=OFF')

        if context.buildAlembic:
            extraArgs.append('-DPXR_BUILD_ALEMBIC_PLUGIN=ON')
            if context.enableHDF5:
                extraArgs.append('-DPXR_ENABLE_HDF5_SUPPORT=ON')

                # CMAKE_PREFIX_PATH isn't sufficient for the FindHDF5 module 
                # to find the HDF5 we've built, so provide an extra hint.
                extraArgs.append('-DHDF5_ROOT="{instDir}"'
                                 .format(instDir=context.instDir))
            else:
                extraArgs.append('-DPXR_ENABLE_HDF5_SUPPORT=OFF')
        else:
            extraArgs.append('-DPXR_BUILD_ALEMBIC_PLUGIN=OFF')

        if context.buildDraco:
            extraArgs.append('-DPXR_BUILD_DRACO_PLUGIN=ON')
            draco_root = (context.dracoLocation
                          if context.dracoLocation else context.instDir)
            extraArgs.append('-DDRACO_ROOT="{}"'.format(draco_root))
        else:
            extraArgs.append('-DPXR_BUILD_DRACO_PLUGIN=OFF')

        if context.buildMaterialX:
            extraArgs.append('-DPXR_ENABLE_MATERIALX_SUPPORT=ON')
        else:
            extraArgs.append('-DPXR_ENABLE_MATERIALX_SUPPORT=OFF')

        if Windows():
            # Increase the precompiled header buffer limit.
            extraArgs.append('-DCMAKE_CXX_FLAGS="/Zm150"')

        # Make sure to use boost installed by the build script and not any
        # system installed boost
        extraArgs.append('-DBoost_NO_BOOST_CMAKE=On')
        extraArgs.append('-DBoost_NO_SYSTEM_PATHS=True')
        extraArgs += buildArgs

        RunCMake(context, force, extraArgs)

USD = Dependency("USD", InstallUSD, "include/pxr/pxr.h")

############################################################
# Install script

programDescription = """\
Installation Script for USD

Builds and installs USD and 3rd-party dependencies to specified location.

- Libraries:
The following is a list of libraries that this script will download and build
as needed. These names can be used to identify libraries for various script
options, like --force or --build-args.

{libraryList}

- Downloading Libraries:
If curl or powershell (on Windows) are installed and located in PATH, they
will be used to download dependencies. Otherwise, a built-in downloader will 
be used.

- Specifying Custom Build Arguments:
Users may specify custom build arguments for libraries using the --build-args
option. This values for this option must take the form <library name>,<option>. 
For example:

%(prog)s --build-args boost,cxxflags=... USD,-DPXR_STRICT_BUILD_MODE=ON ...
%(prog)s --build-args USD,"-DPXR_STRICT_BUILD_MODE=ON -DPXR_HEADLESS_TEST_MODE=ON" ...

These arguments will be passed directly to the build system for the specified 
library. Multiple quotes may be needed to ensure arguments are passed on 
exactly as desired. Users must ensure these arguments are suitable for the
specified library and do not conflict with other options, otherwise build 
errors may occur.

- Python Versions and DCC Plugins:
Some DCCs (most notably, Maya) may ship with and run using their own version of
Python. In that case, it is important that USD and the plugins for that DCC are
built using the DCC's version of Python and not the system version. This can be
done by running %(prog)s using the DCC's version of Python.

For example, to build USD on macOS for use in Maya 2019, run:

/Applications/Autodesk/maya2019/Maya.app/Contents/bin/mayapy %(prog)s --no-usdview ...

Note that this is primarily an issue on macOS, where a DCC's version of Python
is likely to conflict with the version provided by the system. On other
platforms, %(prog)s *should* be run using the system Python and *should not*
be run using the DCC's Python.
""".format(
    libraryList=" ".join(sorted([d.name for d in AllDependencies])))

parser = argparse.ArgumentParser(
    formatter_class=argparse.RawDescriptionHelpFormatter,
    description=programDescription)

parser.add_argument("install_dir", type=str, 
                    help="Directory where USD will be installed")
parser.add_argument("-n", "--dry_run", dest="dry_run", action="store_true",
                    help="Only summarize what would happen")
                    
group = parser.add_mutually_exclusive_group()
group.add_argument("-v", "--verbose", action="count", default=1,
                   dest="verbosity",
                   help="Increase verbosity level (1-3)")
group.add_argument("-q", "--quiet", action="store_const", const=0,
                   dest="verbosity",
                   help="Suppress all output except for error messages")

group = parser.add_argument_group(title="Build Options")
group.add_argument("-j", "--jobs", type=int, default=GetCPUCount(),
                   help=("Number of build jobs to run in parallel. "
                         "(default: # of processors [{0}])"
                         .format(GetCPUCount())))
group.add_argument("--build", type=str,
                   help=("Build directory for USD and 3rd-party dependencies " 
                         "(default: <install_dir>/build)"))
group.add_argument("--build-args", type=str, nargs="*", default=[],
                   help=("Custom arguments to pass to build system when "
                         "building libraries (see docs above)"))
group.add_argument("--force", type=str, action="append", dest="force_build",
                   default=[],
                   help=("Force download and build of specified library "
                         "(see docs above)"))
group.add_argument("--force-all", action="store_true",
                   help="Force download and build of all libraries")
group.add_argument("--generator", type=str,
                   help=("CMake generator to use when building libraries with "
                         "cmake"))
group.add_argument("--toolset", type=str,
                   help=("CMake toolset to use when building libraries with "
                         "cmake"))

group = parser.add_argument_group(title="3rd Party Dependency Build Options")
group.add_argument("--src", type=str,
                   help=("Directory where dependencies will be downloaded "
                         "(default: <install_dir>/src)"))
group.add_argument("--inst", type=str,
                   help=("Directory where dependencies will be installed "
                         "(default: <install_dir>)"))

group = parser.add_argument_group(title="USD Options")

(SHARED_LIBS, MONOLITHIC_LIB) = (0, 1)
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--build-shared", dest="build_type",
                      action="store_const", const=SHARED_LIBS, 
                      default=SHARED_LIBS,
                      help="Build individual shared libraries (default)")
subgroup.add_argument("--build-monolithic", dest="build_type",
                      action="store_const", const=MONOLITHIC_LIB,
                      help="Build a single monolithic shared library")

group.add_argument("--debug", dest="build_debug", action="store_true",
                    help="Build with debugging information")

subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--tests", dest="build_tests", action="store_true",
                      default=False, help="Build unit tests")
subgroup.add_argument("--no-tests", dest="build_tests", action="store_false",
                      help="Do not build unit tests (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--examples", dest="build_examples", action="store_true",
                      default=True, help="Build examples (default)")
subgroup.add_argument("--no-examples", dest="build_examples", action="store_false",
                      help="Do not build examples")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--tutorials", dest="build_tutorials", action="store_true",
                      default=True, help="Build tutorials (default)")
subgroup.add_argument("--no-tutorials", dest="build_tutorials", action="store_false",
                      help="Do not build tutorials")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--tools", dest="build_tools", action="store_true",
                     default=True, help="Build USD tools (default)")
subgroup.add_argument("--no-tools", dest="build_tools", action="store_false",
                      help="Do not build USD tools")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--docs", dest="build_docs", action="store_true",
                      default=False, help="Build documentation")
subgroup.add_argument("--no-docs", dest="build_docs", action="store_false",
                      help="Do not build documentation (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--python", dest="build_python", action="store_true",
                      default=True, help="Build python based components "
                                         "(default)")
subgroup.add_argument("--no-python", dest="build_python", action="store_false",
                      help="Do not build python based components")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--prefer-safety-over-speed", dest="safety_first",
                      action="store_true", default=True, help=
                      "Enable extra safety checks (which may negatively "
                      "impact performance) against malformed input files "
                      "(default)")
subgroup.add_argument("--prefer-speed-over-safety", dest="safety_first",
                      action="store_false", help=
                      "Disable performance-impacting safety checks against "
                      "malformed input files")

(NO_IMAGING, IMAGING, USD_IMAGING) = (0, 1, 2)

group = parser.add_argument_group(title="Imaging and USD Imaging Options")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--imaging", dest="build_imaging", 
                      action="store_const", const=IMAGING, default=USD_IMAGING,
                      help="Build imaging component")
subgroup.add_argument("--usd-imaging", dest="build_imaging", 
                      action="store_const", const=USD_IMAGING,
                      help="Build imaging and USD imaging components (default)")
subgroup.add_argument("--no-imaging", dest="build_imaging", 
                      action="store_const", const=NO_IMAGING,
                      help="Do not build imaging or USD imaging components")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--ptex", dest="enable_ptex", action="store_true", 
                      default=False, 
                      help="Enable Ptex support in imaging")
subgroup.add_argument("--no-ptex", dest="enable_ptex", 
                      action="store_false",
                      help="Disable Ptex support in imaging (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--openvdb", dest="enable_openvdb", action="store_true", 
                      default=False, 
                      help="Enable OpenVDB support in imaging")
subgroup.add_argument("--no-openvdb", dest="enable_openvdb", 
                      action="store_false",
                      help="Disable OpenVDB support in imaging (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--usdview", dest="build_usdview",
                      action="store_true", default=True,
                      help="Build usdview (default)")
subgroup.add_argument("--no-usdview", dest="build_usdview",
                      action="store_false", 
                      help="Do not build usdview")

group = parser.add_argument_group(title="Imaging Plugin Options")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--embree", dest="build_embree", action="store_true",
                      default=False,
                      help="Build Embree sample imaging plugin")
subgroup.add_argument("--no-embree", dest="build_embree", action="store_false",
                      help="Do not build Embree sample imaging plugin (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--prman", dest="build_prman", action="store_true",
                      default=False,
                      help="Build Pixar's RenderMan imaging plugin")
subgroup.add_argument("--no-prman", dest="build_prman", action="store_false",
                      help="Do not build Pixar's RenderMan imaging plugin (default)")
group.add_argument("--prman-location", type=str,
                   help="Directory where Pixar's RenderMan is installed.")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--openimageio", dest="build_oiio", action="store_true", 
                      default=False,
                      help="Build OpenImageIO plugin for USD")
subgroup.add_argument("--no-openimageio", dest="build_oiio", action="store_false",
                      help="Do not build OpenImageIO plugin for USD (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--opencolorio", dest="build_ocio", action="store_true", 
                      default=False,
                      help="Build OpenColorIO plugin for USD")
subgroup.add_argument("--no-opencolorio", dest="build_ocio", action="store_false",
                      help="Do not build OpenColorIO plugin for USD (default)")

group = parser.add_argument_group(title="Alembic Plugin Options")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--alembic", dest="build_alembic", action="store_true", 
                      default=False,
                      help="Build Alembic plugin for USD")
subgroup.add_argument("--no-alembic", dest="build_alembic", action="store_false",
                      help="Do not build Alembic plugin for USD (default)")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--hdf5", dest="enable_hdf5", action="store_true", 
                      default=False,
                      help="Enable HDF5 support in the Alembic plugin")
subgroup.add_argument("--no-hdf5", dest="enable_hdf5", action="store_false",
                      help="Disable HDF5 support in the Alembic plugin (default)")

group = parser.add_argument_group(title="Draco Plugin Options")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--draco", dest="build_draco", action="store_true", 
                      default=False,
                      help="Build Draco plugin for USD")
subgroup.add_argument("--no-draco", dest="build_draco", action="store_false",
                      help="Do not build Draco plugin for USD (default)")
group.add_argument("--draco-location", type=str,
                   help="Directory where Draco is installed.")

group = parser.add_argument_group(title="MaterialX Plugin Options")
subgroup = group.add_mutually_exclusive_group()
subgroup.add_argument("--materialx", dest="build_materialx", action="store_true", 
                      default=False,
                      help="Build MaterialX plugin for USD")
subgroup.add_argument("--no-materialx", dest="build_materialx", action="store_false",
                      help="Do not build MaterialX plugin for USD (default)")

args = parser.parse_args()

class InstallContext:
    def __init__(self, args):
        # Assume the USD source directory is in the parent directory
        self.usdSrcDir = os.path.normpath(
            os.path.join(os.path.abspath(os.path.dirname(__file__)), ".."))

        # Directory where USD will be installed
        self.usdInstDir = os.path.abspath(args.install_dir)

        # Directory where dependencies will be installed
        self.instDir = (os.path.abspath(args.inst) if args.inst 
                        else self.usdInstDir)

        # Directory where dependencies will be downloaded and extracted
        self.srcDir = (os.path.abspath(args.src) if args.src
                       else os.path.join(self.usdInstDir, "src"))
        
        # Directory where USD and dependencies will be built
        self.buildDir = (os.path.abspath(args.build) if args.build
                         else os.path.join(self.usdInstDir, "build"))

        # Determine which downloader to use.  The reason we don't simply
        # use urllib2 all the time is that some older versions of Python
        # don't support TLS v1.2, which is required for downloading some
        # dependencies.
        if which("curl"):
            self.downloader = DownloadFileWithCurl
            self.downloaderName = "curl"
        elif Windows() and which("powershell"):
            self.downloader = DownloadFileWithPowershell
            self.downloaderName = "powershell"
        else:
            self.downloader = DownloadFileWithUrllib
            self.downloaderName = "built-in"

        # CMake generator and toolset
        self.cmakeGenerator = args.generator
        self.cmakeToolset = args.toolset

        # Number of jobs
        self.numJobs = args.jobs
        if self.numJobs <= 0:
            raise ValueError("Number of jobs must be greater than 0")

        # Build arguments
        self.buildArgs = dict()
        for a in args.build_args:
            (depName, _, arg) = a.partition(",")
            if not depName or not arg:
                raise ValueError("Invalid argument for --build-args: {}"
                                 .format(a))
            if depName.lower() not in AllDependenciesByName:
                raise ValueError("Invalid library for --build-args: {}"
                                 .format(depName))

            self.buildArgs.setdefault(depName.lower(), []).append(arg)

        # Build type
        self.buildDebug = args.build_debug;
        self.buildShared = (args.build_type == SHARED_LIBS)
        self.buildMonolithic = (args.build_type == MONOLITHIC_LIB)

        # Build options
        self.safetyFirst = args.safety_first

        # Dependencies that are forced to be built
        self.forceBuildAll = args.force_all
        self.forceBuild = [dep.lower() for dep in args.force_build]

        # Optional components
        self.buildTests = args.build_tests
        self.buildDocs = args.build_docs
        self.buildPython = args.build_python
        self.buildExamples = args.build_examples
        self.buildTutorials = args.build_tutorials
        self.buildTools = args.build_tools

        # - Imaging
        self.buildImaging = (args.build_imaging == IMAGING or
                             args.build_imaging == USD_IMAGING)
        self.enablePtex = self.buildImaging and args.enable_ptex
        self.enableOpenVDB = self.buildImaging and args.enable_openvdb

        # - USD Imaging
        self.buildUsdImaging = (args.build_imaging == USD_IMAGING)

        # - usdview
        self.buildUsdview = (self.buildUsdImaging and 
                             self.buildPython and 
                             args.build_usdview)

        # - Imaging plugins
        self.buildEmbree = self.buildImaging and args.build_embree
        self.buildPrman = self.buildImaging and args.build_prman
        self.prmanLocation = (os.path.abspath(args.prman_location)
                               if args.prman_location else None)                               
        self.buildOIIO = args.build_oiio
        self.buildOCIO = args.build_ocio

        # - Alembic Plugin
        self.buildAlembic = args.build_alembic
        self.enableHDF5 = self.buildAlembic and args.enable_hdf5

        # - Draco Plugin
        self.buildDraco = args.build_draco
        self.dracoLocation = (os.path.abspath(args.draco_location)
                                if args.draco_location else None)

        # - MaterialX Plugin
        self.buildMaterialX = args.build_materialx

    def GetBuildArguments(self, dep):
        return self.buildArgs.get(dep.name.lower(), [])
       
    def ForceBuildDependency(self, dep):
        # Never force building a Python dependency, since users are required
        # to build these dependencies themselves.
        if type(dep) is PythonDependency:
            return False
        return self.forceBuildAll or dep.name.lower() in self.forceBuild

try:
    context = InstallContext(args)
except Exception as e:
    PrintError(str(e))
    sys.exit(1)

verbosity = args.verbosity

# Augment PATH on Windows so that 3rd-party dependencies can find libraries
# they depend on. In particular, this is needed for building IlmBase/OpenEXR.
extraPaths = []
extraPythonPaths = []
if Windows():
    extraPaths.append(os.path.join(context.instDir, "lib"))
    extraPaths.append(os.path.join(context.instDir, "bin"))

if extraPaths:
    paths = os.environ.get('PATH', '').split(os.pathsep) + extraPaths
    os.environ['PATH'] = os.pathsep.join(paths)

if extraPythonPaths:
    paths = os.environ.get('PYTHONPATH', '').split(os.pathsep) + extraPythonPaths
    os.environ['PYTHONPATH'] = os.pathsep.join(paths)

# Determine list of dependencies that are required based on options
# user has selected.
requiredDependencies = [ZLIB, BOOST, TBB]

if context.buildAlembic:
    if context.enableHDF5:
        requiredDependencies += [HDF5]
    requiredDependencies += [OPENEXR, ALEMBIC]

if context.buildDraco:
    requiredDependencies += [DRACO]

if context.buildMaterialX:
    requiredDependencies += [MATERIALX]

if context.buildImaging:
    if context.enablePtex:
        requiredDependencies += [PTEX]

    requiredDependencies += [OPENSUBDIV]

    if context.enableOpenVDB:
        requiredDependencies += [BLOSC, BOOST, OPENEXR, OPENVDB, TBB]
    
    if context.buildOIIO:
        requiredDependencies += [BOOST, JPEG, TIFF, PNG, OPENEXR, OPENIMAGEIO]

    if context.buildOCIO:
        requiredDependencies += [OPENCOLORIO]

    if context.buildEmbree:
        requiredDependencies += [TBB, EMBREE]
                             
if context.buildUsdview:
    requiredDependencies += [PYOPENGL, PYSIDE]

# Assume zlib already exists on Linux platforms and don't build
# our own. This avoids potential issues where a host application
# loads an older version of zlib than the one we'd build and link
# our libraries against.
if Linux():
    requiredDependencies.remove(ZLIB)

# Error out if user is building monolithic library on windows with draco plugin
# enabled. This currently results in missing symbols.
if context.buildDraco and context.buildMonolithic and Windows():
    PrintError("Draco plugin can not be enabled for monolithic build on Windows")
    sys.exit(1)

# Error out if user explicitly specified building usdview without required
# components. Otherwise, usdview will be silently disabled. This lets users
# specify "--no-python" without explicitly having to specify "--no-usdview",
# for instance.
if "--usdview" in sys.argv:
    if not context.buildUsdImaging:
        PrintError("Cannot build usdview when usdImaging is disabled.")
        sys.exit(1)
    if not context.buildPython:
        PrintError("Cannot build usdview when Python support is disabled.")
        sys.exit(1)

# Error out if running Maya's version of Python and attempting to build
# usdview.
if IsMayaPython():
    if context.buildUsdview:
        PrintError("Cannot build usdview when building against Maya's version "
                   "of Python. Maya does not provide access to the 'OpenGL' "
                   "Python module. Use '--no-usdview' to disable building "
                   "usdview.")
        sys.exit(1)

dependenciesToBuild = []
for dep in requiredDependencies:
    if context.ForceBuildDependency(dep) or not dep.Exists(context):
        if dep not in dependenciesToBuild:
            dependenciesToBuild.append(dep)

# Verify toolchain needed to build required dependencies
if (not which("g++") and
    not which("clang") and
    not GetXcodeDeveloperDirectory() and
    not GetVisualStudioCompilerAndVersion()):
    PrintError("C++ compiler not found -- please install a compiler")
    sys.exit(1)

# Error out if a 64bit version of python interpreter is not being used
isPython64Bit = (ctypes.sizeof(ctypes.c_voidp) == 8)
if not isPython64Bit:
    PrintError("64bit python not found -- please install it and adjust your"
               "PATH")
    sys.exit(1)

if which("cmake"):
    # Check cmake requirements
    if Windows():
        # Windows build depend on boost 1.70, which is not supported before
        # cmake version 3.14
        cmake_required_version = (3, 14)
    else:
        cmake_required_version = (3, 12)
    cmake_version = GetCMakeVersion()
    if not cmake_version:
        PrintError("Failed to determine CMake version")
        sys.exit(1)

    if cmake_version < cmake_required_version:
        def _JoinVersion(v):
            return ".".join(str(n) for n in v)
        PrintError("CMake version {req} or later required to build USD, "
                   "but version found was {found}".format(
                       req=_JoinVersion(cmake_required_version),
                       found=_JoinVersion(cmake_version)))
        sys.exit(1)
else:
    PrintError("CMake not found -- please install it and adjust your PATH")
    sys.exit(1)

if context.buildDocs:
    if not which("doxygen"):
        PrintError("doxygen not found -- please install it and adjust your PATH")
        sys.exit(1)
        
    if not which("dot"):
        PrintError("dot not found -- please install graphviz and adjust your "
                   "PATH")
        sys.exit(1)

if PYSIDE in requiredDependencies:
    # The USD build will skip building usdview if pyside2-uic or pyside-uic is
    # not found, so check for it here to avoid confusing users. This list of 
    # PySide executable names comes from cmake/modules/FindPySide.cmake
    pyside2Uic = ["pyside2-uic", "python2-pyside2-uic", "pyside2-uic-2.7"]
    found_pyside2Uic = any([which(p) for p in pyside2Uic])
    pysideUic = ["pyside-uic", "python2-pyside-uic", "pyside-uic-2.7"]
    found_pysideUic = any([which(p) for p in pysideUic])
    if not found_pyside2Uic and not found_pysideUic:
        if Windows():
            # Windows does not support PySide2 with Python2.7
            PrintError("pyside-uic not found -- please install PySide and"
                       " adjust your PATH. (Note that this program may be named"
                       " {0} depending on your platform)"
                   .format(" or ".join(pysideUic)))
        else:
            PrintError("pyside2-uic not found -- please install PySide2 and"
                       " adjust your PATH. (Note that this program may be"
                       " named {0} depending on your platform)"
                       .format(" or ".join(pyside2Uic)))
        sys.exit(1)

if JPEG in requiredDependencies:
    # NASM is required to build libjpeg-turbo
    if (Windows() and not which("nasm")):
        PrintError("nasm not found -- please install it and adjust your PATH")
        sys.exit(1)

# Summarize
summaryMsg = """
Building with settings:
  USD source directory          {usdSrcDir}
  USD install directory         {usdInstDir}
  3rd-party source directory    {srcDir}
  3rd-party install directory   {instDir}
  Build directory               {buildDir}
  CMake generator               {cmakeGenerator}
  CMake toolset                 {cmakeToolset}
  Downloader                    {downloader}

  Building                      {buildType}
    Config                      {buildConfig}
    Imaging                     {buildImaging}
      Ptex support:             {enablePtex}
      OpenVDB support:          {enableOpenVDB}
      OpenImageIO support:      {buildOIIO} 
      OpenColorIO support:      {buildOCIO} 
      PRMan support:            {buildPrman}
    UsdImaging                  {buildUsdImaging}
      usdview:                  {buildUsdview}
    Python support              {buildPython}
      Python 3:                 {enablePython3}
    Documentation               {buildDocs}
    Tests                       {buildTests}
    Examples                    {buildExamples}
    Tutorials                   {buildTutorials}
    Tools                       {buildTools}
    Alembic Plugin              {buildAlembic}
      HDF5 support:             {enableHDF5}
    Draco Plugin                {buildDraco}
    MaterialX Plugin            {buildMaterialX}

  Dependencies                  {dependencies}"""

if context.buildArgs:
    summaryMsg += """
  Build arguments               {buildArgs}"""

def FormatBuildArguments(buildArgs):
    s = ""
    for depName in sorted(buildArgs.keys()):
        args = buildArgs[depName]
        s += """
                                {name}: {args}""".format(
            name=AllDependenciesByName[depName].name,
            args=" ".join(args))
    return s.lstrip()

summaryMsg = summaryMsg.format(
    usdSrcDir=context.usdSrcDir,
    usdInstDir=context.usdInstDir,
    srcDir=context.srcDir,
    buildDir=context.buildDir,
    instDir=context.instDir,
    cmakeGenerator=("Default" if not context.cmakeGenerator
                    else context.cmakeGenerator),
    cmakeToolset=("Default" if not context.cmakeToolset
                  else context.cmakeToolset),
    downloader=(context.downloaderName),
    dependencies=("None" if not dependenciesToBuild else 
                  ", ".join([d.name for d in dependenciesToBuild])),
    buildArgs=FormatBuildArguments(context.buildArgs),
    buildType=("Shared libraries" if context.buildShared
               else "Monolithic shared library" if context.buildMonolithic
               else ""),
    buildConfig=("Debug" if context.buildDebug else "Release"),
    buildImaging=("On" if context.buildImaging else "Off"),
    enablePtex=("On" if context.enablePtex else "Off"),
    enableOpenVDB=("On" if context.enableOpenVDB else "Off"),
    buildOIIO=("On" if context.buildOIIO else "Off"),
    buildOCIO=("On" if context.buildOCIO else "Off"),
    buildPrman=("On" if context.buildPrman else "Off"),
    buildUsdImaging=("On" if context.buildUsdImaging else "Off"),
    buildUsdview=("On" if context.buildUsdview else "Off"),
    buildPython=("On" if context.buildPython else "Off"),
    enablePython3=("On" if Python3() else "Off"),
    buildDocs=("On" if context.buildDocs else "Off"),
    buildTests=("On" if context.buildTests else "Off"),
    buildExamples=("On" if context.buildExamples else "Off"),
    buildTutorials=("On" if context.buildTutorials else "Off"),
    buildTools=("On" if context.buildTools else "Off"),
    buildAlembic=("On" if context.buildAlembic else "Off"),
    buildDraco=("On" if context.buildDraco else "Off"),
    buildMaterialX=("On" if context.buildMaterialX else "Off"),
    enableHDF5=("On" if context.enableHDF5 else "Off"))

Print(summaryMsg)

if args.dry_run:
    sys.exit(0)

# Scan for any dependencies that the user is required to install themselves
# and print those instructions first.
pythonDependencies = \
    [dep for dep in dependenciesToBuild if type(dep) is PythonDependency]
if pythonDependencies:
    for dep in pythonDependencies:
        Print(dep.getInstructions())
    sys.exit(1)

# Ensure directory structure is created and is writable.
for dir in [context.usdInstDir, context.instDir, context.srcDir, 
            context.buildDir]:
    try:
        if os.path.isdir(dir):
            testFile = os.path.join(dir, "canwrite")
            open(testFile, "w").close()
            os.remove(testFile)
        else:
            os.makedirs(dir)
    except Exception as e:
        PrintError("Could not write to directory {dir}. Change permissions "
                   "or choose a different location to install to."
                   .format(dir=dir))
        sys.exit(1)

try:
    # Download and install 3rd-party dependencies, followed by USD.
    for dep in dependenciesToBuild + [USD]:
        PrintStatus("Installing {dep}...".format(dep=dep.name))
        dep.installer(context, 
                      buildArgs=context.GetBuildArguments(dep),
                      force=context.ForceBuildDependency(dep))
except Exception as e:
    PrintError(str(e))
    sys.exit(1)

# Done. Print out a final status message.
requiredInPythonPath = set([
    os.path.join(context.usdInstDir, "lib", "python")
])
requiredInPythonPath.update(extraPythonPaths)

requiredInPath = set([
    os.path.join(context.usdInstDir, "bin")
])
requiredInPath.update(extraPaths)

if Windows():
    requiredInPath.update([
        os.path.join(context.usdInstDir, "lib"),
        os.path.join(context.instDir, "bin"),
        os.path.join(context.instDir, "lib")
    ])

Print("""
Success! To use USD, please ensure that you have:""")

if context.buildPython:
    Print("""
    The following in your PYTHONPATH environment variable:
    {requiredInPythonPath}""".format(
        requiredInPythonPath="\n    ".join(sorted(requiredInPythonPath))))

Print("""
    The following in your PATH environment variable:
    {requiredInPath}
""".format(requiredInPath="\n    ".join(sorted(requiredInPath))))
    
if context.buildPrman:
    Print("See documentation at http://openusd.org/docs/RenderMan-USD-Imaging-Plugin.html "
          "for setting up the RenderMan plugin.\n")
