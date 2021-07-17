#!/pxrpythonsubst
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

from __future__ import print_function

import os, sys

import platform
isWindows = (platform.system() == 'Windows')

def __findExe(name):
    from distutils.spawn import find_executable
    cmd = find_executable(name)
    if cmd:
        return cmd
    else:
        cmd = find_executable(name, path=os.path.abspath(os.path.dirname(sys.argv[0])))
        if cmd:
            return cmd
    return None

def __findExe_Win(name):
    cmd = __findExe(name)
    if cmd and os.access(cmd + '.cmd', os.X_OK):
        return cmd + '.cmd'

    # find_executable under Windows only returns *.EXE files
    # so we need to traverse PATH.
    for path in os.environ['PATH'].split(os.pathsep):
        base = os.path.join(path, name)
        # We need to test for name.cmd first because on Windows, the USD
        # executables are wrapped due to lack of N*IX style shebang support
        # on Windows.
        for ext in ['.cmd', '']:
            cmd = base + ext
            if os.access(cmd, os.X_OK):
                return cmd
    return None

if isWindows:
    _findExe = __findExe_Win
else:
    _findExe = __findExe

# lookup usdcat and a suitable text editor. if none are available, 
# this will cause the program to abort with a suitable error message.
def _findEditorTools(usdFileName, readOnly):
    # Ensure the usdcat executable has been installed
    usdcatCmd = _findExe("usdcat")
    if not usdcatCmd:
        sys.exit("Error: Couldn't find 'usdcat'. Expected it to be in PATH.")

    # Ensure we have a suitable editor available
    editorCmd = (os.getenv("USD_EDITOR") or
                 os.getenv("EDITOR") or 
                 _findExe("emacs") or
                 _findExe("vim") or
                 _findExe("notepad"))
    
    if not editorCmd:
        sys.exit("Error: Couldn't find a suitable text editor to use. Expected " 
                 "$USD_EDITOR or $EDITOR to be set, or emacs/vim/notepad to "
                 "be installed and available in PATH.")

    # special handling for emacs users
    if 'emacs' in editorCmd:
        title = '"usdedit %s%s"' % ("--noeffect " if readOnly else "",
                                    usdFileName)
        editorCmd += " -name %s" % title

    return (usdcatCmd, editorCmd)

# this generates a temporary usd file which the user will edit.
def _generateTemporaryFile(usdcatCmd, usdFileName, readOnly, prefix):
    # gets the base name of the USD file opened
    usdFileNameBasename = os.path.splitext(os.path.basename(usdFileName))[0]

    fullPrefix = prefix or usdFileNameBasename + "_tmp"
    import tempfile
    (usdaFile, usdaFileName) = tempfile.mkstemp(
        prefix=fullPrefix, suffix='.usda', dir=os.getcwd())

    # No need for an open file descriptor, as it locks the file in Windows.
    os.close(usdaFile)
 
    os.system(usdcatCmd + ' ' + usdFileName + '> ' + usdaFileName)

    if readOnly:
        os.chmod(usdaFileName, 0o444)
     
    # Thrown if failed to open temp file Could be caused by 
    # failure to read USD file
    if os.stat(usdaFileName).st_size == 0:
        sys.exit("Error: Failed to open file %s, exiting." % usdFileName)

    return usdaFileName

# allow the user to edit the temporary file, and return whether or
# not they made any changes.
def _editTemporaryFile(editorCmd, usdaFileName):
    # check the timestamp before updating a file's mtime
    initialTimeStamp = os.path.getmtime(usdaFileName)
    os.system(editorCmd + ' ' + usdaFileName)
    newTimeStamp = os.path.getmtime(usdaFileName)
    
    # indicate whether the file was changed
    return initialTimeStamp != newTimeStamp

# attempt to write out our changes to the actual usd file
def _writeOutChanges(temporaryFileName, permanentFileName):
    from pxr import Sdf, Tf

    try:
        temporaryLayer = Sdf.Layer.FindOrOpen(temporaryFileName)
    except Tf.ErrorException as err:
        sys.exit("Error: Failed to open temporary layer %s, and therefore cannot save your edits back to original file %s"
                 ". An error occurred trying to parse the file: %s" % (temporaryFileName, permanentFileName, str(err)))

    # Note that we attempt to overwrite the permanent file's contents
    # rather than explicitly creating a new layer. This avoids aligning
    # file format paremeters from the original to the new.
    outLayer = Sdf.Layer.FindOrOpen(permanentFileName)
    if not outLayer:
        sys.exit("Error: Unable to save edits back to the original file %s"
                 ". Your edits can be found in %s." \
                 %(permanentFileName, temporaryFileName))
    outLayer.TransferContent(temporaryLayer)
    return outLayer.Save()

def main():
    import argparse
    parser = argparse.ArgumentParser(prog=os.path.basename(sys.argv[0]),
        description= 'Convert a usd-readable file to the usd ascii format in \n'
               'a temporary location and invoke an editor on it.  After \n'
               'saving and quitting the editor, the edited file will be \n'
               'converted back to the original format and OVERWRITE the \n'
               'original file, unless you supply the "-n" (--noeffect) flag, \n'
               'in which case no changes will be saved back to the original '
               'file. \n'
               'The editor to use will be looked up as follows: \n'
               '    - USD_EDITOR environment variable \n'
               '    - EDITOR environment variable \n'
               '    - emacs in PATH \n'
               '    - vim in PATH \n'
               '    - notepad in PATH \n'
               '\n\n')
    parser.add_argument('-n', '--noeffect',
                        dest='readOnly', action='store_true',
                        help='Do not edit the file.')
    parser.add_argument('-f', '--forcewrite', 
                        dest='forceWrite', action='store_true',
                        help='Override file permissions to allow writing.')
    parser.add_argument('-p', '--prefix', 
                        dest='prefix', action='store', type=str, default=None,
                        help='Provide a prefix for the temporary file name.')
    parser.add_argument('usdFileName', help='The usd file to edit.')
    results = parser.parse_args()

    # pull args from result map so we don't need to write result. for each
    readOnly, forceWrite, usdFileName, prefix = (
        results.readOnly,
        results.forceWrite,
        results.usdFileName,
        results.prefix)
    
    # verify our usd file exists, and permissions args are sane
    if readOnly and forceWrite:
        sys.exit("Error: Cannot set read only(-n) and force " 
                 " write(-f) together.")

    from pxr import Ar
    resolvedPath = Ar.GetResolver().Resolve(usdFileName)
    if not resolvedPath:
        sys.exit("Error: Cannot find file %s" % usdFileName)

    # Layers in packages cannot be written using the Sdf API.
    from pxr import Ar, Sdf
    (package, packaged) = Ar.SplitPackageRelativePathOuter(resolvedPath)

    extension = Sdf.FileFormat.GetFileExtension(package)
    fileFormat = Sdf.FileFormat.FindByExtension(extension)
    if not fileFormat:
        sys.exit("Error: Unknown file format")
        
    if fileFormat.IsPackage():
        print("Warning: Edits cannot be saved to layers in %s files. "
              "Starting in no-effect mode." % extension)
        readOnly = True
        forceWrite = False

    writable = os.path.isfile(usdFileName) and os.access(usdFileName, os.W_OK)
    if not (writable or readOnly or forceWrite):
        sys.exit("Error: File isn't writable, and "
                 "readOnly(-n)/forceWrite(-f) haven't been marked.")

    # ensure we have both a text editor and usdcat available
    usdcatCmd, editorCmd = _findEditorTools(usdFileName, readOnly)
    
    # generate our temporary file with proper permissions and edit.
    usdaFileName = _generateTemporaryFile(usdcatCmd, usdFileName,
                                          readOnly, prefix)
    tempFileChanged = _editTemporaryFile(editorCmd, usdaFileName)
    

    if (not readOnly or forceWrite) and tempFileChanged:
        # note that we need not overwrite usdFileName's write permissions
        # because we will be creating a new layer at that path.
        if not _writeOutChanges(temporaryFileName=usdaFileName, 
                                permanentFileName=usdFileName):
            sys.exit("Error: Unable to save edits back to the original file %s"
                     ". Your edits can be found in %s. " \
                     %(usdFileName, usdaFileName))

    if readOnly:
        os.chmod(usdaFileName, 0o644)
    os.remove(usdaFileName)

if __name__ == "__main__":
    main()
