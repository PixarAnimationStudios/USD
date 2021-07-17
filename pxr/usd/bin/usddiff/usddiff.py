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
import difflib, os, sys
from subprocess import call

import platform
isWindows = (platform.system() == 'Windows')

NO_DIFF_FOUND_EXIT_CODE = 0
DIFF_FOUND_EXIT_CODE = 1
ERROR_EXIT_CODE = 2

def _exit(msg, exitCode):
    if msg is not None:
        sys.stderr.write(msg + "\n")
    sys.exit(exitCode)

# generates a command list representing a call which will generate
# a temporary ascii file used during diffing. 
def _generateCatCommand(usdcatCmd, inPath, outPath, flatten=None, fmt=None):
    command = [usdcatCmd, inPath, '--out', outPath]
    if flatten:
        command.append('--flatten')

    if fmt and os.path.splitext(outPath)[1] == '.usd':
        command.append('--usdFormat')
        command.append(fmt)

    return command

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

# looks up a suitable diff tool, and locates usdcat
def _findDiffTools():
    usdcatCmd = _findExe("usdcat")
    if not usdcatCmd:
        _exit("Error: Could not find 'usdcat'. Expected it to be in PATH", 
              ERROR_EXIT_CODE)

    # prefer USD_DIFF, then DIFF, else use the internal unified diff.
    diffCmd = (os.environ.get('USD_DIFF') or os.environ.get('DIFF'))
    diffCmdArgs = list()
    if diffCmd:
        diffCmdList = diffCmd.split()
        diffCmd = diffCmdList[0]
        if diffCmdList[1:]:
            diffCmdArgs = diffCmdList[1:]
    if diffCmd and not _findExe(diffCmd):
        _exit("Error: Failed to find diff tool %s." % (diffCmd, ),
              ERROR_EXIT_CODE)

    return (usdcatCmd, diffCmd, diffCmdArgs)

def _getFileFormat(path):
    from pxr import Sdf, Ar

    # Note that python's os.path.splitext retains the '.' portion
    # when obtaining an extension, but Sdf's Fileformat API doesn't 
    # expect one. We also make sure to prune out any version specifiers.
    _, ext = os.path.splitext(path)
    path = Ar.GetResolver().Resolve(path)
    if path is None:
        return None

    if len(ext) <= 1:
        fileFormat = Sdf.FileFormat.FindByExtension('usd')
    else:
        prunedExtension = ext[1:]
        versionSpecifierPos = prunedExtension.rfind('#')
        if versionSpecifierPos != -1:
            prunedExtension = prunedExtension[:versionSpecifierPos]
         
        fileFormat = Sdf.FileFormat.FindByExtension(prunedExtension)

    # Don't check if file exists - this should be handled by resolver (and
    # path may not exist / have been fetched yet)
    if fileFormat:
        return fileFormat.formatId

    return None

def _convertTo(inPath, outPath, usdcatCmd, flatten=None, fmt=None):
    # Just copy empty files -- we want something to diff against but
    # the file isn't valid usd.
    try:
        if os.stat(inPath).st_size == 0:
            import shutil
            try:
                shutil.copy(inPath, outPath)
                return 0
            except:
                return 1
    except (IOError, OSError):
        # assume it's because file doesn't exist yet, because it's an unresolved
        # path...
        pass
    return call(_generateCatCommand(usdcatCmd, inPath, outPath, flatten, fmt))

def _tryEdit(fileName, tempFileName, usdcatCmd, fileType, flattened):
    if flattened:
        _exit('Error: Cannot write out flattened result.', ERROR_EXIT_CODE)

    if not os.access(fileName, os.W_OK):
        _exit('Error: Cannot write to %s, insufficient permissions' % fileName,
              ERROR_EXIT_CODE)
    
    return _convertTo(tempFileName, fileName, usdcatCmd, flatten=None, fmt=fileType)

def _runDiff(baseline, comparison, flatten, noeffect, brief):
    from pxr import Tf

    diffResult = 0

    usdcatCmd, diffCmd, diffCmdArgs = _findDiffTools()
    baselineFileType = _getFileFormat(baseline)
    comparisonFileType = _getFileFormat(comparison)

    pluginError = 'Error: Cannot find supported file format plugin for %s'
    if baselineFileType is None:
        _exit(pluginError % baseline, ERROR_EXIT_CODE)

    if comparisonFileType is None:
        _exit(pluginError % comparison, ERROR_EXIT_CODE)

    # Generate recognizable suffixes for our files in the temp dir
    # location of the form /temp/string__originalFileName.usda 
    # where originalFileName is the basename(no extension) of the original file.
    # This allows users to tell which file is which when diffing.
    tempBaselineFileName = ("__" + 
        os.path.splitext(os.path.basename(baseline))[0] + '.usda') 
    tempComparisonFileName = ("__" +     
        os.path.splitext(os.path.basename(comparison))[0] + '.usda')

    with Tf.NamedTemporaryFile(suffix=tempBaselineFileName) as tempBaseline, \
         Tf.NamedTemporaryFile(suffix=tempComparisonFileName) as tempComparison:

        # Dump the contents of our files into the temporaries
        convertError = 'Error: failed to convert from %s to %s.'
        if _convertTo(baseline, tempBaseline.name, usdcatCmd, 
                      flatten, fmt=None) != 0:
            _exit(convertError % (baseline, tempBaseline.name),
                  ERROR_EXIT_CODE)
        if _convertTo(comparison, tempComparison.name, usdcatCmd,
                      flatten, fmt=None) != 0:
            _exit(convertError % (comparison, tempComparison.name),
                  ERROR_EXIT_CODE) 

        tempBaselineTimestamp = os.path.getmtime(tempBaseline.name)
        tempComparisonTimestamp = os.path.getmtime(tempComparison.name)

        if diffCmd:
            # Run the external diff tool.
            if brief:
                diffCmdArgs.append("--brief")
            diffResult = call([diffCmd] + diffCmdArgs + [tempBaseline.name, tempComparison.name])

        else:
            # Read the files.
            with open(tempBaseline.name, "r") as f:
                baselineData = f.readlines()
            with open(tempComparison.name, "r") as f:
                comparisonData = f.readlines()

            if baselineData != comparisonData:
                if brief:
                    print("Files %s and %s differ" % (baseline, comparison))
                else:
                    # Generate unified diff and output if there are any differences.
                    diff = list(difflib.unified_diff(
                        baselineData, comparisonData,
                        tempBaseline.name, tempComparison.name, n=0))
                    # Skip the file names.
                    for line in diff[2:]:
                        print(line, end='')
                diffResult = 1

        tempBaselineChanged = ( 
            os.path.getmtime(tempBaseline.name) != tempBaselineTimestamp)
        tempComparisonChanged = (
            os.path.getmtime(tempComparison.name) != tempComparisonTimestamp)

        # If we intend to edit either of the files
        if not noeffect:
            if tempBaselineChanged:
                if _tryEdit(baseline, tempBaseline.name, 
                            usdcatCmd, baselineFileType, flatten) != 0:
                    _exit(convertError % (baseline, tempBaseline.name),
                          ERROR_EXIT_CODE)
            if tempComparisonChanged:
                if _tryEdit(comparison, tempComparison.name,
                            usdcatCmd, comparisonFileType, flatten) != 0:
                    _exit(convertError % (comparison, tempComparison.name),
                          ERROR_EXIT_CODE)
    return diffResult

def _findFiles(args):
    '''Return a 3-tuple of lists: (baseline-only, matching, comparison-only).
    baseline-only and comparison-only are lists of individual files, while
    matching is a list of corresponding pairs of files.'''
    import os
    import stat
    from pxr import Ar

    join = os.path.join
    basename = os.path.basename
    exists = os.path.exists

    def listFiles(dirpath):
        ret = []
        for root, _, files in os.walk(dirpath):
            ret += [os.path.relpath(join(root, file), dirpath)
                    for file in files]
        return set(ret)

    # Must have FILE FILE, DIR DIR, DIR FILES... or FILES... DIR.
    err = ValueError("Error: File arguments must be one of: "
                     "FILE FILE, DIR DIR, DIR FILES..., or FILES... DIR.")
    if len(args) < 2:
        raise err

    # For speed, since filestats can be slow, stat all args once, then reuse that
    # to determine isdir/isfile
    resolver = Ar.GetResolver()
    stats = []
    for arg in args:
        try:
            st = os.stat(arg)
        except (OSError, IOError):
            if not resolver.Resolve(arg):
                raise ValueError("Error: %s does not exist, and cannot be "
                                 "resolved" % arg)
            st = None
        stats.append(st)

    def isdir(st):
        return st and stat.S_ISDIR(st.st_mode)

    # if any of the directory forms are used, no paths may be unresolved assets
    def validateFiles():
        for i, st in enumerate(stats):
            if st is None:
                raise ValueError("Error: %s did not exist on disk, and using a "
                                 "directory comparison form" % args[i])

    # DIR FILES...
    if isdir(stats[0]) and not any(map(isdir, stats[1:])):
        validateFiles()
        dirpath = args[0]
        files = set(map(os.path.relpath, args[1:]))
        dirfiles = listFiles(dirpath)
        return ([], 
                [(join(dirpath, p), p) for p in files & dirfiles],
                [p for p in files - dirfiles])
    # FILES... DIR
    elif not any(map(isdir, stats[:-1])) and isdir(stats[-1]):
        validateFiles()
        dirpath = args[-1]
        files = set(map(os.path.relpath, args[:-1]))
        dirfiles = listFiles(dirpath)
        return ([p for p in files - dirfiles],
                [(p, join(dirpath, p)) for p in files & dirfiles],
                [])
    # FILE FILE or DIR DIR
    elif len(args) == 2:
        # DIR DIR
        if all(map(isdir, stats)):
            ldir, rdir = args[0], args[1]
            lhs, rhs = map(listFiles, args)
            return (
                # baseline only
                sorted([join(ldir, p) for p in lhs - rhs]),
                # corresponding
                sorted([(join(ldir, p), join(rdir, p)) for p in lhs & rhs]),
                # comparison only
                sorted([join(rdir, p) for p in rhs - lhs]))
        # FILE FILE
        elif not any(map(isdir, stats)):
            return ([], [(args[0], args[1])], [])

    raise err

def main():
    import argparse

    parser = argparse.ArgumentParser(prog=os.path.basename(sys.argv[0]),
                description="Compares two usd-readable files using a selected"
                            " diff program. This is chosen by looking at the" 
                            " $USD_DIFF environment variable. If this is unset,"
                            " it will consult the $DIFF environment variable. "
                            " Lastly, if neither of these is set, it will try" 
                            " to use the canonical unix program, diff."
                            " This will relay the exit code of the selected"
                            " diff program.")
    parser.add_argument('files', nargs='+',
                        help='The files to compare. These must be of the form '
                             'DIR DIR, FILE... DIR, DIR FILE... or FILE FILE. ')
    parser.add_argument('-n', '--noeffect', action='store_true',
                        help='Do not edit either file.') 
    parser.add_argument('-f', '--flatten', action='store_true',
                        help='Fully compose both layers as Usd Stages and '
                             'flatten into single layers.')
    parser.add_argument('-q', '--brief', action='store_true',
                        help='Do not return full results of diffs. Passes --brief to the diff command.')

    results = parser.parse_args()
    diffResult = NO_DIFF_FOUND_EXIT_CODE 

    try:
        baselineOnly, common, comparisonOnly = _findFiles(results.files)

        for (baseline, comparison) in common:
            if _runDiff(baseline, comparison, 
                        results.flatten, results.noeffect, results.brief):
                diffResult = DIFF_FOUND_EXIT_CODE

        mismatchMsg = 'No corresponding file found for %s, skipping.'
        for b in baselineOnly:
            print(mismatchMsg % b)

        for c in comparisonOnly:
            print(mismatchMsg % c)

    except ValueError as err:
        _exit(str(err), ERROR_EXIT_CODE)
    
    _exit(None, diffResult)

if __name__ == "__main__":
    main()
