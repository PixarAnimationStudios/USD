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
#
# Usage:
#   shebang shebang-str source.py dest.py
#   shebang file output.cmd
#
# The former substitutes '/pxrpythonsubst' in <source.py> with <shebang-str>
# and writes the result to <dest.py>.  The latter generates a file named
# <output.cmd> with the contents '@python "%~dp0<file>"';  this is a
# Windows command script to execute "python <file>" where <file> is in
# the same directory as the command script.

from __future__ import print_function
import sys


if len(sys.argv) < 3 or len(sys.argv) > 4:
    print("Usage: %s {shebang-str source.py dest|file output.cmd}" % sys.argv[0])
    sys.exit(1)

if len(sys.argv) == 3:
    with open(sys.argv[2], 'w') as f:
        if sys.version_info.major == 3:
            cmdName = 'python3'
        else:
            cmdName = 'python'
        print('@%s "%%~dp0%s" %%*' % (cmdName, sys.argv[1], ), file=f)

else:
    with open(sys.argv[2], 'r') as s:
        with open(sys.argv[3], 'w') as d:
            for line in s:
                d.write(line.replace('/pxrpythonsubst', sys.argv[1]))
