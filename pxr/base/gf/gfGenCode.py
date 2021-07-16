#!/usr/bin/env python
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

########################################################################
# Code generation script for GfVec, GfRange, GfQuat (and in future, GfMatrix)
# classes.
#
# Run this script manually to update the source code that's checked in.  Run
# with --validate to compare what would be generated with the existing code.  If
# it differs, this script will print a diff and error out.
#

from __future__ import print_function

import os, sys, itertools
from argparse import ArgumentParser

from jinja2 import Environment, FileSystemLoader
from jinja2.exceptions import TemplateError, TemplateSyntaxError

# Write filePath if its content differs from \p content.  If unchanged, print a
# message indicating that.  If filePath is not writable, print a diff.
def _WriteFile(filePath, content, verbose=True):
    import difflib
    # If file currently exists and content is unchanged, do nothing.
    existingContent = '\n'
    content = (content + '\n'
               if content and not content.endswith('\n') else content)
    if os.path.exists(filePath):
        with open(filePath, 'r') as fp:
            existingContent = fp.read()
        if existingContent == content:
            if verbose:
                print('\tunchanged %s' % filePath)
            return
    # Otherwise attempt to write to file.
    try:
        with open(filePath, 'w') as curfile:
            curfile.write(content)
            if verbose:
                print('\t    wrote %s' % filePath)
    except IOError as ioe:
        print('\t', ioe)
        print('Diff:')
        print('\n'.join(difflib.unified_diff(existingContent.split('\n'),
                                             content.split('\n'))))

def IsFloatingPoint(t):
    return t in ['double', 'float', 'GfHalf']

def RankScalar(s):
    # Return a numeric rank for a scalar type.  We allow implicit conversions to
    # scalars of greater rank.
    return dict([(t, n) for n, t in
                 enumerate(['int', 'GfHalf', 'float', 'double'])])[s]

def AllowImplicitConversion(src, dst):
    # Somewhat questionably, we always allow ints to implicitly convert to
    # floating point types.
    return RankScalar(src) <= RankScalar(dst)

def MakeListFn(defaultN):
    def List(fmt, sep=', ', num=None):
        num = num if num is not None else defaultN
        return sep.join([fmt % {'i': i} for i in range(num)])
    return List

def MakeMatrixFn(defaultN):
    def Matrix(fmt, sep=', ', indent=0, diagFmt=None, num=None):
        num = num if num is not None else defaultN
        diagFmt = diagFmt if diagFmt is not None else fmt

        def GetFmt(row, col):
            return diagFmt if row == col else fmt

        strs = [(sep.join([GetFmt(i,j) % {'i':i,'j':j} for j in range(num)])) \
                    for i in range(num)]
        indentStr = (' ' * indent)
        lineSep = sep + ('\n' + indentStr if '\n' not in sep else '')
        result = lineSep.join(strs)
        return result
    return Matrix

def GenerateFromTemplates(env, templates, suffix, outputPath, verbose=True):
    for tmpl in templates:
        tmplName = tmpl % '.template'

        try:
            _WriteFile(os.path.join(outputPath, tmpl % suffix),
                env.get_template(tmplName).render(), verbose)
        except TemplateSyntaxError as err:
            print('Syntax Error: {0.name}:{0.lineno}: {0.message}'.format(err),
                  file=sys.stderr)
        except TemplateError as err:
            print('Template Error: {}: {}'.format(err, tmplName),
                  file=sys.stderr)

def ScalarSuffix(scl):
    if scl == 'GfHalf':
        return 'h'
    else:
        return scl[0]

def VecName(dim, scl):
    return 'GfVec%s%s' % (dim, ScalarSuffix(scl))

def Eps(scl):
    return '0.001' if scl == 'GfHalf' else 'GF_MIN_VECTOR_LENGTH'

########################################################################
# GfVec
def GetVecSpecs():
    scalarTypes = ['double', 'float', 'GfHalf', 'int']
    dimensions = [2, 3, 4]
    vecSpecs = sorted(
        [dict(SCL=scl,
              DIM=dim,
              SUFFIX=str(dim) + ScalarSuffix(scl),
              VEC=VecName(dim, scl),
              EPS=Eps(scl),
              LIST=MakeListFn(dim),
              VECNAME=VecName,
              SCALAR_SUFFIX=ScalarSuffix,
              SCALARS=scalarTypes)
         for scl, dim in itertools.product(scalarTypes, dimensions)],
        key=lambda d: RankScalar(d['SCL']))

    return dict(templates=['vec%s.h', 'vec%s.cpp', 'wrapVec%s.cpp'],
                specs=vecSpecs)

########################################################################
# GfRange
def GetRangeSpecs():
    def RngName(dim, scl):
        return 'GfRange%s%s' % (dim, ScalarSuffix(scl))

    def MinMaxType(dim, scl):
        return scl if dim == 1 else VecName(dim, scl)

    def MinMaxParm(dim, scl):
        t = MinMaxType(dim, scl)
        return t + ' ' if dim == 1 else 'const %s &' % t

    scalarTypes = ['double', 'float']
    dimensions = [1, 2, 3]
    rngSpecs = sorted(
        [dict(SCL=scl,
              MINMAX=MinMaxType(dim, scl),
              MINMAXPARM=MinMaxParm(dim, scl),
              DIM=dim,
              SUFFIX=str(dim) + ScalarSuffix(scl),
              RNG=RngName(dim, scl),
              RNGNAME=RngName,
              SCALARS=scalarTypes,
              LIST=MakeListFn(dim))
         for scl, dim in itertools.product(scalarTypes, dimensions)],
        key=lambda d: RankScalar(d['SCL']))

    return dict(templates=['range%s.h', 'range%s.cpp', 'wrapRange%s.cpp'],
                specs=rngSpecs)
    
########################################################################
# GfQuat
def GetQuatSpecs():
    def QuatName(scl):
        return 'GfQuat%s' % ScalarSuffix(scl)

    scalarTypes = ['double', 'float', 'GfHalf']
    quatSpecs = sorted(
        [dict(SCL=scl,
              SUFFIX=ScalarSuffix(scl),
              QUAT=QuatName(scl),
              QUATNAME=QuatName,
              SCALAR_SUFFIX=ScalarSuffix,
              SCALARS=scalarTypes,
              LIST=MakeListFn(4))
         for scl in scalarTypes],
        key=lambda d: RankScalar(d['SCL']))

    return dict(templates=['quat%s.h', 'quat%s.cpp', 'wrapQuat%s.cpp'],
                specs=quatSpecs)

########################################################################
# GfMatrix
def GetMatrixSpecs(dim):
    def MatrixName(dim, scl):
        return 'GfMatrix%s%s' % (dim, ScalarSuffix(scl))

    scalarTypes = ['double', 'float']
    dimensions = [dim]

    matrixSpecs = sorted(
        [dict(SCL=scl,
              DIM=i,
              FILESUFFIX=ScalarSuffix(scl),
              SUFFIX=str(i) + ScalarSuffix(scl),
              MAT=MatrixName(i, scl),
              LIST=MakeListFn(i),
              MATRIX=MakeMatrixFn(i),
              MATNAME=MatrixName,
              SCALARS=scalarTypes)
         for scl, i in itertools.product(scalarTypes, dimensions)],
        key=lambda d: RankScalar(d['SCL']))

    return dict(templates=['matrix%s%%s.h' % dim,
                           'matrix%s%%s.cpp' % dim,
                           'wrapMatrix%s%%s.cpp' % dim ], specs=matrixSpecs)

def GetMatrix2Specs():
    return GetMatrixSpecs(dim = 2)

def GetMatrix3Specs():
    return GetMatrixSpecs(dim = 3)

def GetMatrix4Specs():
    return GetMatrixSpecs(dim = 4)

# Check that each file in dstDir matches the corresponding file in srcDir.
def ValidateFiles(srcDir, dstDir):
    import difflib
    missing = []
    diffs = []
    for dstFile in [os.path.join(dstDir, f) for f in os.listdir(dstDir)
                    if os.path.isfile(os.path.join(dstDir, f))]:
        srcFile = os.path.join(srcDir, os.path.basename(dstFile))
        if not os.path.isfile(srcFile):
            missing.append(srcFile)
            continue
        dstContent, srcContent = open(dstFile).read(), open(srcFile).read()
        if dstContent != srcContent:
            diff = '\n'.join(difflib.unified_diff(
                srcContent.split('\n'),
                dstContent.split('\n'),
                'Source ' + os.path.basename(srcFile),
                'Generated ' + os.path.basename(dstFile)))
            diffs.append(diff)
            continue

    if missing or diffs:
        msg = []
        if missing:
            msg.append('*** Missing Generated Files: ' + ', '.join(missing))
        if diffs:
            msg.append('*** Differing Generated Files:\n' + '\n'.join(diffs))
        raise RuntimeError('\n' + '\n'.join(msg))

if __name__ == '__main__':
    ap = ArgumentParser(
        description='Generate source code for GfVec, GfRange, GfQuat.')
    ap.add_argument('--validate', action='store_true')
    ap.add_argument('--dstDir', default=os.curdir)
    ap.add_argument('--srcDir', default=os.curdir)
    args = ap.parse_args()

    stdEnv = dict(UPPER=str.upper, LOWER=str.lower,
                  ALLOW_IMPLICIT_CONVERSION=AllowImplicitConversion,
                  IS_FLOATING_POINT=IsFloatingPoint)

    if args.validate:
        # Make a temporary directory for results.
        import tempfile
        args.dstDir = tempfile.mkdtemp()

    try:
        for s in [GetVecSpecs(), GetRangeSpecs(), GetQuatSpecs(),
                  GetMatrix2Specs(), GetMatrix3Specs(), GetMatrix4Specs()]:
            env = Environment(loader=FileSystemLoader(args.srcDir),
                              trim_blocks=True)
            env.globals.update(**stdEnv)
            templates, specs = s['templates'], s['specs']
            for spec in specs:
                env.globals.update(**spec)
                GenerateFromTemplates(
                    env, templates, spec.get('FILESUFFIX', spec['SUFFIX']),
                    args.dstDir, verbose=not args.validate)

        if args.validate:
            ValidateFiles(args.srcDir, args.dstDir)

    finally:
        # Remove the temporary directory.
        if args.validate:
            import shutil
            shutil.rmtree(args.dstDir)

