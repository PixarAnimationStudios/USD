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
'''
Module that provides the StageView class.
'''
from __future__ import print_function

from math import tan, floor, ceil, radians as rad, isinf
import os, sys
from time import time

from .qt import QtCore, QtGui, QtWidgets, QtOpenGL

from pxr import Tf
from pxr import Gf
from pxr import Glf
from pxr import Sdf, Usd, UsdGeom
from pxr import UsdImagingGL
from pxr import CameraUtil

from .common import (RenderModes, ColorCorrectionModes, ShadedRenderModes, Timer,
                     ReportMetricSize, SelectionHighlightModes, DEBUG_CLIPPING,
                     DefaultFontFamily)
from .rootDataModel import RootDataModel
from .selectionDataModel import ALL_INSTANCES, SelectionDataModel
from .viewSettingsDataModel import ViewSettingsDataModel
from .freeCamera import FreeCamera

# A viewport rectangle to be used for GL must be integer values.
# In order to loose the least amount of precision the viewport
# is centered and adjusted to initially contain entirely the
# given viewport.
# If it turns out that doing so gives more than a pixel width
# or height of error the viewport is instead inset.
# This does mean that the returned viewport may have a slightly
# different aspect ratio to the given viewport.
def ViewportMakeCenteredIntegral(viewport):

    # The values are initially integral and containing the
    # the given rect
    left = int(floor(viewport[0]))
    bottom = int(floor(viewport[1]))
    right = int(ceil(viewport[0] + viewport[2]))
    top = int(ceil(viewport[1] + viewport[3]))

    width = right - left
    height = top - bottom

    # Compare the integral height to the original height
    # and do a centered 1 pixel adjustment if more than
    # a pixel off.
    if (height - viewport[3]) > 1.0:
        bottom += 1
        height -= 2
    # Compare the integral width to the original width
    # and do a centered 1 pixel adjustment if more than
    # a pixel off.
    if (width - viewport[2]) > 1.0:
        left += 1
        width -= 2
    return (left, bottom, width, height)

class GLSLProgram():
    def __init__(self, VS3, FS3, VS2, FS2, uniformDict):
        from OpenGL import GL
        # versionString = <version_number><space><vendor_specific_information>
        versionString = GL.glGetString(GL.GL_VERSION).decode()
        # <version_number> = <major_number>.<minor_number>[.<release_number>]
        versionNumberString = versionString.split()[0]
        self._glMajorVersion = int(versionNumberString.split('.')[0])

        # requires PyOpenGL 3.0.2 or later for glGenVertexArrays.
        self.useVAO = (self._glMajorVersion >= 3 and
                        hasattr(GL, 'glGenVertexArrays'))
        self.useSampleAlphaToCoverage = (self._glMajorVersion >= 4)

        self.program   = GL.glCreateProgram()
        vertexShader   = GL.glCreateShader(GL.GL_VERTEX_SHADER)
        fragmentShader = GL.glCreateShader(GL.GL_FRAGMENT_SHADER)

        if (self._glMajorVersion >= 3):
            vsSource = VS3
            fsSource = FS3
        else:
            vsSource = VS2
            fsSource = FS2

        GL.glShaderSource(vertexShader, vsSource)
        GL.glCompileShader(vertexShader)
        GL.glShaderSource(fragmentShader, fsSource)
        GL.glCompileShader(fragmentShader)
        GL.glAttachShader(self.program, vertexShader)
        GL.glAttachShader(self.program, fragmentShader)
        GL.glLinkProgram(self.program)

        if GL.glGetProgramiv(self.program, GL.GL_LINK_STATUS) == GL.GL_FALSE:
            print(GL.glGetShaderInfoLog(vertexShader))
            print(GL.glGetShaderInfoLog(fragmentShader))
            print(GL.glGetProgramInfoLog(self.program))
            GL.glDeleteShader(vertexShader)
            GL.glDeleteShader(fragmentShader)
            GL.glDeleteProgram(self.program)
            self.program = 0

        GL.glDeleteShader(vertexShader)
        GL.glDeleteShader(fragmentShader)

        self.uniformLocations = {}
        for param in uniformDict:
            self.uniformLocations[param] = GL.glGetUniformLocation(self.program, param)

    def uniform4f(self, param, x, y, z, w):
        from OpenGL import GL
        GL.glUniform4f(self.uniformLocations[param], x, y, z, w)

class Rect():
    def __init__(self):
        self.xywh = [0.0] * 4

    @classmethod
    def fromXYWH(cls, xywh):
        self = cls()
        self.xywh[:] = list(map(float, xywh[:4]))
        return self

    @classmethod
    def fromCorners(cls, c0, c1):
        self = cls()
        self.xywh[0] = float(min(c0[0], c1[0]))
        self.xywh[1] = float(min(c0[1], c1[1]))
        self.xywh[2] = float(max(c0[0], c1[0])) - self.xywh[0]
        self.xywh[3] = float(max(c0[1], c1[1])) - self.xywh[1]
        return self

    def scaledAndBiased(self, sxy, txy):
        ret = self.__class__()
        for c in range(2):
            ret.xywh[c] = sxy[c] * self.xywh[c] + txy[c]
            ret.xywh[c + 2] = sxy[c] * self.xywh[c + 2]
        return ret

    def _splitAlongY(self, y):
        bottom = self.__class__()
        top = self.__class__()
        bottom.xywh[:] = self.xywh
        top.xywh[:] = self.xywh
        top.xywh[1] = y
        bottom.xywh[3] = top.xywh[1] - bottom.xywh[1]
        top.xywh[3] = top.xywh[3] - bottom.xywh[3]
        return bottom, top

    def _splitAlongX(self, x):
        left = self.__class__()
        right = self.__class__()
        left.xywh[:] = self.xywh
        right.xywh[:] = self.xywh
        right.xywh[0] = x
        left.xywh[2] = right.xywh[0] - left.xywh[0]
        right.xywh[2] = right.xywh[2] - left.xywh[2]
        return left, right

    def difference(self, xywh):
        #check x
        if xywh[0] > self.xywh[0]:
            #keep left, check right
            left, right = self._splitAlongX(xywh[0])
            return [left] + right.difference(xywh)
        if (xywh[0] + xywh[2]) < (self.xywh[0] + self.xywh[2]):
            #keep right
            left, right = self._splitAlongX(xywh[0] + xywh[2])
            return [right]
        #check y
        if xywh[1] > self.xywh[1]:
            #keep bottom, check top
            bottom, top = self._splitAlongY(xywh[1])
            return [bottom] + top.difference(xywh)
        if (xywh[1] + xywh[3]) < (self.xywh[1] + self.xywh[3]):
            #keep top
            bottom, top = self._splitAlongY(xywh[1] + xywh[3])
            return [top]
        return []


class OutlineRect(Rect):
    _glslProgram = None
    _vbo = 0
    _vao = 0
    def __init__(self):
        Rect.__init__(self)

    @classmethod
    def compileProgram(self):
        if self._glslProgram:
            return self._glslProgram
        from OpenGL import GL
        import ctypes

        # prep a quad line vbo
        self._vbo = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        st = [0, 0, 1, 0, 1, 1, 0, 1]
        GL.glBufferData(GL.GL_ARRAY_BUFFER, len(st)*4,
                        (ctypes.c_float*len(st))(*st), GL.GL_STATIC_DRAW)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)

        self._glslProgram = GLSLProgram(
            # for OpenGL 3.1 or later
            """#version 140
               uniform vec4 rect;
               in vec2 st;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1); }""",
            """#version 140
               out vec4 fragColor;
               uniform vec4 color;
              void main() { fragColor = color; }""",
            # for OpenGL 2.1 (osx compatibility profile)
            """#version 120
               uniform vec4 rect;
               attribute vec2 st;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1); }""",
            """#version 120
               uniform vec4 color;
               void main() { gl_FragColor = color; }""",
            ["rect", "color"])

        return self._glslProgram

    def glDraw(self, color):
        from OpenGL import GL

        cls = self.__class__

        program = cls.compileProgram()
        if program.program == 0:
            return

        GL.glUseProgram(program.program)

        if program.useSampleAlphaToCoverage:
            GL.glDisable(GL.GL_SAMPLE_ALPHA_TO_COVERAGE)

        if program.useVAO:
            if (cls._vao == 0):
                cls._vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(cls._vao)

        # for some reason, we need to bind at least 1 vertex attrib (is OSX)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, cls._vbo)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, False, 0, None)

        program.uniform4f("color", *color)
        program.uniform4f("rect", *self.xywh)
        GL.glDrawArrays(GL.GL_LINE_LOOP, 0, 4)

        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        GL.glDisableVertexAttribArray(0)
        if program.useVAO:
            GL.glBindVertexArray(0)

        GL.glUseProgram(0)

class FilledRect(Rect):
    _glslProgram = None
    _vbo = 0
    _vao = 0
    def __init__(self):
        Rect.__init__(self)

    @classmethod
    def compileProgram(self):
        if self._glslProgram:
            return self._glslProgram
        from OpenGL import GL
        import ctypes

        # prep a quad line vbo
        self._vbo = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        st = [0, 0, 1, 0, 0, 1, 1, 1]
        GL.glBufferData(GL.GL_ARRAY_BUFFER, len(st)*4,
                        (ctypes.c_float*len(st))(*st), GL.GL_STATIC_DRAW)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)

        self._glslProgram = GLSLProgram(
            # for OpenGL 3.1 or later
            """#version 140
               uniform vec4 rect;
               in vec2 st;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1); }""",
            """#version 140
               out vec4 fragColor;
               uniform vec4 color;
              void main() { fragColor = color; }""",
            # for OpenGL 2.1 (osx compatibility profile)
            """#version 120
               uniform vec4 rect;
               attribute vec2 st;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1); }""",
            """#version 120
               uniform vec4 color;
               void main() { gl_FragColor = color; }""",
            ["rect", "color"])

        return self._glslProgram

    def glDraw(self, color):
        #don't draw if too small
        if self.xywh[2] < 0.001 or self.xywh[3] < 0.001:
            return

        from OpenGL import GL

        cls = self.__class__

        program = cls.compileProgram()
        if program.program == 0:
            return

        GL.glUseProgram(program.program)

        if program.useSampleAlphaToCoverage:
            GL.glDisable(GL.GL_SAMPLE_ALPHA_TO_COVERAGE)

        if program.useVAO:
            if (cls._vao == 0):
                cls._vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(cls._vao)

        # for some reason, we need to bind at least 1 vertex attrib (is OSX)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, cls._vbo)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, False, 0, None)

        program.uniform4f("color", *color)
        program.uniform4f("rect", *self.xywh)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)

        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        GL.glDisableVertexAttribArray(0)
        if program.useVAO:
            GL.glBindVertexArray(0)

        GL.glUseProgram(0)

class Prim2DSetupTask():
    def __init__(self, viewport):
        self._viewport = viewport[:]

    def Sync(self, ctx):
        pass

    def Execute(self, ctx):
        from OpenGL import GL
        GL.glViewport(*self._viewport)
        GL.glDisable(GL.GL_DEPTH_TEST)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        GL.glEnable(GL.GL_BLEND)

class Prim2DDrawTask():
    def __init__(self):
        self._prims = []
        self._colors = []
        self._pixelRatio = QtWidgets.QApplication.instance().devicePixelRatio()


    def Sync(self, ctx):
        for prim in self._prims:
            prim.__class__.compileProgram()

    def Execute(self, ctx):
        for prim, color in zip(self._prims, self._colors):
            prim.glDraw(color)

class Outline(Prim2DDrawTask):
    def __init__(self):
        Prim2DDrawTask.__init__(self)
        self._outlineColor = Gf.ConvertDisplayToLinear(Gf.Vec4f(0.0, 0.0, 0.0, 1.0))

    def updatePrims(self, croppedViewport, qglwidget):
        width = float(qglwidget.width()) * self._pixelRatio
        height = float(qglwidget.height()) * self._pixelRatio
        prims = [ OutlineRect.fromXYWH(croppedViewport) ]
        self._prims = [p.scaledAndBiased((2.0 / width, 2.0 / height), (-1, -1))
                for p in prims]
        self._colors = [ self._outlineColor ]

class Reticles(Prim2DDrawTask):
    def __init__(self):
        Prim2DDrawTask.__init__(self)
        self._outlineColor = Gf.ConvertDisplayToLinear(Gf.Vec4f(0.0, 0.7, 1.0, 0.9))

    def updateColor(self, color):
        self._outlineColor = Gf.ConvertDisplayToLinear(Gf.Vec4f(*color))

    def updatePrims(self, croppedViewport, qglwidget, inside, outside):
        width = float(qglwidget.width()) * self._pixelRatio
        height = float(qglwidget.height()) * self._pixelRatio
        prims = [ ]
        ascenders = [0, 0]
        descenders = [0, 0]
        if inside:
            descenders = [7, 15]
        if outside:
            ascenders = [7, 15]
        # vertical reticles on the top and bottom
        for i in range(5):
            w = 2.6
            h = ascenders[i & 1] + descenders[i & 1]
            x = croppedViewport[0] - (w // 2) + ((i + 1) * croppedViewport[2]) // 6
            bottomY = croppedViewport[1] - ascenders[i & 1]
            topY = croppedViewport[1] + croppedViewport[3] - descenders[i & 1]
            prims.append(FilledRect.fromXYWH((x, bottomY, w, h)))
            prims.append(FilledRect.fromXYWH((x, topY, w, h)))
        # horizontal reticles on the left and right
        for i in range(5):
            w = ascenders[i & 1] + descenders[i & 1]
            h = 2.6
            leftX = croppedViewport[0] - ascenders[i & 1]
            rightX = croppedViewport[0] + croppedViewport[2] - descenders[i & 1]
            y = croppedViewport[1] - (h // 2) + ((i + 1) * croppedViewport[3]) // 6
            prims.append(FilledRect.fromXYWH((leftX, y, w, h)))
            prims.append(FilledRect.fromXYWH((rightX, y, w, h)))

        self._prims = [p.scaledAndBiased((2.0 / width, 2.0 / height), (-1, -1))
                for p in prims]
        self._colors = [ self._outlineColor ] * len(self._prims)

class Mask(Prim2DDrawTask):
    def __init__(self):
        Prim2DDrawTask.__init__(self)
        self._maskColor = Gf.ConvertDisplayToLinear(Gf.Vec4f(0.0, 0.0, 0.0, 1.0))

    def updateColor(self, color):
        self._maskColor = Gf.ConvertDisplayToLinear(Gf.Vec4f(*color))

    def updatePrims(self, croppedViewport, qglwidget):
        width = float(qglwidget.width()) * self._pixelRatio
        height = float(qglwidget.height()) * self._pixelRatio
        rect = FilledRect.fromXYWH((0, 0, width, height))
        prims = rect.difference(croppedViewport)
        self._prims = [p.scaledAndBiased((2.0 / width, 2.0 / height), (-1, -1))
                for p in prims]
        self._colors = [ self._maskColor ] * 2

class HUD():
    class Group():
        def __init__(self, name, w, h):
            self.x = 0
            self.y = 0
            self.w = w
            self.h = h
            pixelRatio = QtWidgets.QApplication.instance().devicePixelRatio()
            imageW = w * pixelRatio
            imageH = h * pixelRatio
            self.qimage = QtGui.QImage(imageW, imageH, QtGui.QImage.Format_ARGB32)
            self.qimage.fill(QtGui.QColor(0, 0, 0, 0))
            self.painter = QtGui.QPainter()

    def __init__(self):
        self._pixelRatio = QtWidgets.QApplication.instance().devicePixelRatio()
        self._HUDLineSpacing = 15
        self._HUDFont = QtGui.QFont(DefaultFontFamily.MONOSPACE_FONT_FAMILY, 
                9*self._pixelRatio)
        self._groups = {}
        self._glslProgram = None
        self._vao = 0

    def compileProgram(self):
        from OpenGL import GL
        import ctypes

        # prep a quad vbo
        self._vbo = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        st = [0, 0, 1, 0, 0, 1, 1, 1]
        GL.glBufferData(GL.GL_ARRAY_BUFFER, len(st)*4,
                        (ctypes.c_float*len(st))(*st), GL.GL_STATIC_DRAW)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)

        self._glslProgram = GLSLProgram(
            # for OpenGL 3.1 or later
            """#version 140
               uniform vec4 rect;
               in vec2 st;
               out vec2 uv;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1);
                 uv          = vec2(st.x, 1 - st.y); }""",
            """#version 140
               in vec2 uv;
               out vec4 color;
               uniform sampler2D tex;
              void main() { color = texture(tex, uv); }""",
            # for OpenGL 2.1 (osx compatibility profile)
            """#version 120
               uniform vec4 rect;
               attribute vec2 st;
               varying vec2 uv;
               void main() {
                 gl_Position = vec4(rect.x + rect.z*st.x,
                                    rect.y + rect.w*st.y, 0, 1);
                 uv          = vec2(st.x, 1 - st.y); }""",
            """#version 120
               varying vec2 uv;
               uniform sampler2D tex;
               void main() { gl_FragColor = texture2D(tex, uv); }""",
            ["rect", "tex"])

        return True

    def addGroup(self, name, w, h):
        self._groups[name] = self.Group(name, w, h)

    def updateGroup(self, name, x, y, col, dic, keys = None):
        group = self._groups[name]
        group.qimage.fill(QtGui.QColor(0, 0, 0, 0))
        group.x = x
        group.y = y
        painter = group.painter
        painter.begin(group.qimage)

        from .prettyPrint import prettyPrint
        if keys is None:
            keys = sorted(dic.keys())

        # find the longest key so we know how far from the edge to print
        # add [0] at the end so that max() never gets an empty sequence
        longestKeyLen = max([len(k) for k in dic.keys()]+[0])
        margin = int(longestKeyLen*1.4)

        painter.setFont(self._HUDFont)
        color = QtGui.QColor()
        yy = 10 * self._pixelRatio
        lineSpacing = self._HUDLineSpacing * self._pixelRatio
        for key in keys:
            if key not in dic:
                continue
            line = key.rjust(margin) + ": " + str(prettyPrint(dic[key]))
            # Shadow of text
            shadow = Gf.ConvertDisplayToLinear(Gf.Vec3f(.2, .2, .2))
            color.setRgbF(shadow[0], shadow[1], shadow[2])
            painter.setPen(color)
            painter.drawText(1, yy+1, line)

            # Colored text
            color.setRgbF(col[0], col[1], col[2])
            painter.setPen(color)
            painter.drawText(0, yy, line)

            yy += lineSpacing

        painter.end()
        return y + lineSpacing

    def draw(self, qglwidget):
        from OpenGL import GL

        if (self._glslProgram == None):
            self.compileProgram()

        if (self._glslProgram.program == 0):
            return

        GL.glUseProgram(self._glslProgram.program)

        width = float(qglwidget.width())
        height = float(qglwidget.height())

        if self._glslProgram.useSampleAlphaToCoverage:
            GL.glDisable(GL.GL_SAMPLE_ALPHA_TO_COVERAGE)

        if self._glslProgram.useVAO:
            if (self._vao == 0):
                self._vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(self._vao)

        # for some reason, we need to bind at least 1 vertex attrib (is OSX)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, False, 0, None)

        # seems like a bug in Qt4.8/CoreProfile on OSX that GL_UNPACK_ROW_LENGTH has changed.
        GL.glPixelStorei(GL.GL_UNPACK_ROW_LENGTH, 0)

        for name in self._groups:
            group = self._groups[name]

            tex = qglwidget.bindTexture(group.qimage, GL.GL_TEXTURE_2D, GL.GL_RGBA,
                                        QtOpenGL.QGLContext.NoBindOption)
            GL.glUniform4f(self._glslProgram.uniformLocations["rect"],
                           2*group.x/width - 1,
                           1 - 2*group.y/height - 2*group.h/height,
                           2*group.w/width,
                           2*group.h/height)
            GL.glUniform1i(self._glslProgram.uniformLocations["tex"], 0)
            GL.glActiveTexture(GL.GL_TEXTURE0)
            GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
            GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)

            GL.glDeleteTextures(tex)

        GL.glBindTexture(GL.GL_TEXTURE_2D, 0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        GL.glDisableVertexAttribArray(0)

        if self._glslProgram.useVAO:
            GL.glBindVertexArray(0)

        GL.glUseProgram(0)

def _ComputeCameraFraming(viewport, renderBufferSize):
    x, y, w, h = viewport
    renderBufferWidth  = renderBufferSize[0]
    renderBufferHeight = renderBufferSize[1]

    # Set display window equal to viewport - but flipped
    # since viewport is in y-Up coordinate system but
    # display window is y-Down.
    displayWindow = Gf.Range2f(
        Gf.Vec2f(x,     renderBufferHeight - y - h),
        Gf.Vec2f(x + w, renderBufferHeight - y))

    # Intersect the display window with render buffer rect for
    # data window.
    renderBufferRect = Gf.Rect2i(
        Gf.Vec2i(0, 0), renderBufferWidth, renderBufferHeight)
    dataWindow = renderBufferRect.GetIntersection(
        Gf.Rect2i(
            Gf.Vec2i(x, renderBufferHeight - y - h),
            w, h))

    return CameraUtil.Framing(displayWindow, dataWindow)

class StageView(QtOpenGL.QGLWidget):
    '''
    QGLWidget that displays a USD Stage.  A StageView requires a dataModel
    object from which it will query state it needs to properly image its
    given UsdStage.  See the nested DefaultDataModel class for the expected
    API.
    '''

    # TODO: most, if not all of the state StageView requires (except possibly
    # the stage?), should be migrated to come from the dataModel, and redrawing
    # should be triggered by signals the dataModel emits.
    class DefaultDataModel(RootDataModel):

        def __init__(self):
            super(StageView.DefaultDataModel, self).__init__()

            self._selectionDataModel = SelectionDataModel(self)
            self._viewSettingsDataModel = ViewSettingsDataModel(self, None)

        @property
        def selection(self):
            return self._selectionDataModel

        @property
        def viewSettings(self):
            return self._viewSettingsDataModel

    ###########
    # Signals #
    ###########

    signalBboxUpdateTimeChanged = QtCore.Signal(int)

    # First arg is primPath, (which could be empty Path)
    # Second arg is instanceIndex (or UsdImagingGL.ALL_INSTANCES for all
    #  instances)
    # Third and fourth arg are primPath, instanceIndex, of root level
    #  boundable (if applicable).
    # Fifth arg is selectedPoint
    # Sixth and seventh args represent state at time of the pick
    signalPrimSelected = QtCore.Signal(Sdf.Path, int, Sdf.Path, int, Gf.Vec3f,
                                       QtCore.Qt.MouseButton,
                                       QtCore.Qt.KeyboardModifiers)

    # Only raised when StageView has been told to do so, setting
    # rolloverPicking to True
    signalPrimRollover = QtCore.Signal(Sdf.Path, int, Sdf.Path, int,
                                       Gf.Vec3f, QtCore.Qt.KeyboardModifiers)
    signalMouseDrag = QtCore.Signal()
    signalErrorMessage = QtCore.Signal(str)

    signalSwitchedToFreeCam = QtCore.Signal()

    signalFrustumChanged = QtCore.Signal()

    @property
    def renderParams(self):
        return self._renderParams

    @renderParams.setter
    def renderParams(self, params):
        self._renderParams = params

    @property
    def autoClip(self):
        return self._dataModel.viewSettings.autoComputeClippingPlanes

    @property
    def showReticles(self):
        return ((self._dataModel.viewSettings.showReticles_Inside or self._dataModel.viewSettings.showReticles_Outside)
                and self._dataModel.viewSettings.cameraPrim != None)

    @property
    def _fitCameraInViewport(self):
       return ((self._dataModel.viewSettings.showMask or self._dataModel.viewSettings.showMask_Outline or self.showReticles)
               and self._dataModel.viewSettings.cameraPrim != None)

    @property
    def _cropImageToCameraViewport(self):
       return ((self._dataModel.viewSettings.showMask and self._dataModel.viewSettings.showMask_Opaque)
               and self._dataModel.viewSettings.cameraPrim != None)

    @property
    def cameraPrim(self):
        return self._dataModel.viewSettings.cameraPrim

    @cameraPrim.setter
    def cameraPrim(self, prim):
        self._dataModel.viewSettings.cameraPrim = prim

    @property
    def rolloverPicking(self):
        return self._rolloverPicking

    @rolloverPicking.setter
    def rolloverPicking(self, enabled):
        self._rolloverPicking = enabled
        self.setMouseTracking(enabled)

    @property
    def fpsHUDInfo(self):
        return self._fpsHUDInfo

    @fpsHUDInfo.setter
    def fpsHUDInfo(self, info):
        self._fpsHUDInfo = info

    @property
    def fpsHUDKeys(self):
        return self._fpsHUDKeys

    @fpsHUDKeys.setter
    def fpsHUDKeys(self, keys):
        self._fpsHUDKeys = keys

    @property
    def upperHUDInfo(self):
        return self._upperHUDInfo

    @upperHUDInfo.setter
    def upperHUDInfo(self, info):
        self._upperHUDInfo = info

    @property
    def HUDStatKeys(self):
        return self._HUDStatKeys

    @HUDStatKeys.setter
    def HUDStatKeys(self, keys):
        self._HUDStatKeys = keys

    @property
    def overrideNear(self):
        return self._overrideNear

    @overrideNear.setter
    def overrideNear(self, value):
        """To remove the override, set to None.  Causes FreeCamera to become
        active."""
        self._overrideNear = value
        self.switchToFreeCamera()
        self._dataModel.viewSettings.freeCamera.overrideNear = value
        self.updateGL()

    @property
    def overrideFar(self):
        return self._overrideFar

    @overrideFar.setter
    def overrideFar(self, value):
        """To remove the override, set to None.  Causes FreeCamera to become
        active."""
        self._overrideFar = value
        self.switchToFreeCamera()
        self._dataModel.viewSettings.freeCamera.overrideFar = value
        self.updateGL()

    @property
    def allSceneCameras(self):
        return self._allSceneCameras

    @allSceneCameras.setter
    def allSceneCameras(self, value):
        self._allSceneCameras = value

    @property
    def gfCamera(self):
        """Return the last computed Gf Camera"""
        return self._lastComputedGfCamera

    @property
    def cameraFrustum(self):
        """Unlike the StageView.freeCamera property, which is invalid/None
        whenever we are viewing from a scene/stage camera, the 'cameraFrustum'
        property will always return the last-computed camera frustum, regardless
        of source."""
        return self._lastComputedGfCamera.frustum

    @property
    def rendererDisplayName(self):
        return self._rendererDisplayName

    @property
    def rendererAovName(self):
        return self._rendererAovName

    def __init__(self, parent=None, dataModel=None, printTiming=False):
        # Note: The default format *disables* the alpha component and so the
        # default backbuffer uses GL_RGB.
        glFormat = QtOpenGL.QGLFormat()
        msaa = os.getenv("USDVIEW_ENABLE_MSAA", "1")
        if msaa == "1":
            glFormat.setSampleBuffers(True)
            glFormat.setSamples(4)
        # XXX: for OSX (QT5 required)
        # glFormat.setProfile(QtOpenGL.QGLFormat.CoreProfile)
        super(StageView, self).__init__(glFormat, parent)

        self._dataModel = dataModel or StageView.DefaultDataModel()
        self._printTiming = printTiming

        self._isFirstImage = True

        # update() whenever a visible view setting (one which affects the view)
        # is changed.
        self._dataModel.viewSettings.signalVisibleSettingChanged.connect(
            self.update)

        self._dataModel.viewSettings.signalAutoComputeClippingPlanesChanged\
                                    .connect(self._onAutoComputeClippingChanged)

        self._dataModel.signalStageReplaced.connect(self._stageReplaced)
        self._dataModel.selection.signalPrimSelectionChanged.connect(
            self._primSelectionChanged)

        self._dataModel.viewSettings.freeCamera = FreeCamera(True,
                                    self._dataModel.viewSettings.freeCameraFOV)
        self._lastComputedGfCamera = None

        # prep Mask regions
        self._mask = Mask()
        self._maskOutline = Outline()

        self._reticles = Reticles()

        # prep HUD regions
        self._hud = HUD()
        self._hud.addGroup("TopLeft",     250, 160)  # subtree
        self._hud.addGroup("TopRight",    140, 32)   # Hydra: Enabled
        self._hud.addGroup("BottomLeft",  250, 160)  # GPU stats
        self._hud.addGroup("BottomRight", 210, 32)   # Camera, Complexity

        self._stageIsZup = True
        self._cameraMode = "none"
        self._rolloverPicking = False
        self._dragActive = False
        self._lastX = 0
        self._lastY = 0

        self._renderer = None
        self._renderPauseState = False
        self._renderStopState = False
        self._reportedContextError = False
        self._renderModeDict = {
            RenderModes.WIREFRAME: UsdImagingGL.DrawMode.DRAW_WIREFRAME,
            RenderModes.WIREFRAME_ON_SURFACE: 
                UsdImagingGL.DrawMode.DRAW_WIREFRAME_ON_SURFACE,
            RenderModes.SMOOTH_SHADED: UsdImagingGL.DrawMode.DRAW_SHADED_SMOOTH,
            RenderModes.POINTS: UsdImagingGL.DrawMode.DRAW_POINTS,
            RenderModes.FLAT_SHADED: UsdImagingGL.DrawMode.DRAW_SHADED_FLAT,
            RenderModes.GEOM_ONLY: UsdImagingGL.DrawMode.DRAW_GEOM_ONLY,
            RenderModes.GEOM_SMOOTH: UsdImagingGL.DrawMode.DRAW_GEOM_SMOOTH,
            RenderModes.GEOM_FLAT: UsdImagingGL.DrawMode.DRAW_GEOM_FLAT,
            RenderModes.HIDDEN_SURFACE_WIREFRAME:
                UsdImagingGL.DrawMode.DRAW_WIREFRAME
        }

        self._renderParams = UsdImagingGL.RenderParams()

        # Optionally override OCIO lut size. Similar env var available for
        # other apps: ***_OCIO_LUT3D_EDGE_SIZE
        ocioLutSize = os.getenv("USDVIEW_OCIO_LUT3D_EDGE_SIZE", 0)
        if ocioLutSize > 0:
            self._renderParams.lut3dSizeOCIO = ocioLutSize

        self._dist = 50 
        self._bbox = Gf.BBox3d()
        self._selectionBBox = Gf.BBox3d()
        self._selectionBrange = Gf.Range3d()
        self._selectionOrientedRange = Gf.Range3d()
        self._bbcenterForBoxDraw = (0, 0, 0)

        self._overrideNear = None
        self._overrideFar = None

        self._forceRefresh = False
        self._renderTime = 0

        self._allSceneCameras = None

        # HUD properties
        self._fpsHUDInfo = dict()
        self._fpsHUDKeys = []
        self._upperHUDInfo = dict()
        self._HUDStatKeys = list()

        self._glPrimitiveGeneratedQuery = None
        self._glTimeElapsedQuery = None

        self._simpleGLSLProgram = None
        self._axisVBO = None
        self._bboxVBO = None
        self._cameraGuidesVBO = None
        self._vao = 0

        # Update all properties for the current stage.
        self._stageReplaced()

    def _getRenderer(self):
        # Unfortunately, we cannot assume that initializeGL() was called
        # before attempts to use the renderer (e.g. pick()), so we must
        # create the renderer lazily, when we try to do real work with it.
        if not self._renderer:
            if self.context().isValid():
                if self.context().initialized():
                    self._renderer = UsdImagingGL.Engine()
                    self._handleRendererChanged(self.GetCurrentRendererId())
            elif not self._reportedContextError:
                self._reportedContextError = True
                raise RuntimeError("StageView could not initialize renderer without a valid GL context")
        return self._renderer

    def _handleRendererChanged(self, rendererId):
        self._rendererDisplayName = self.GetRendererDisplayName(rendererId)
        self._rendererAovName = "color"
        self._renderPauseState = False
        self._renderStopState = False
        # XXX For HdSt we explicitely enable AOV via SetRendererAov
        # This is because ImagingGL / TaskController are spawned via prims in
        # Presto, so we default AOVs OFF until everything is AOV ready.
        self.SetRendererAov(self.rendererAovName)

    def _scaleMouseCoords(self, point):
        return point * QtWidgets.QApplication.instance().devicePixelRatio()

    def closeRenderer(self):
        '''Close the current renderer.'''
        with Timer() as t:
            self._renderer = None
        if self._printTiming:
            t.PrintTime('shut down Hydra')

    def GetRendererPlugins(self):
        if self._renderer:
            return self._renderer.GetRendererPlugins()
        else:
            return []

    def GetRendererDisplayName(self, plugId):
        if self._renderer:
            return self._renderer.GetRendererDisplayName(plugId)
        else:
            return ""

    def GetCurrentRendererId(self):
        if self._renderer:
            return self._renderer.GetCurrentRendererId()
        else:
            return ""

    def SetRendererPlugin(self, plugId):
        if self._renderer:
            if self._renderer.SetRendererPlugin(plugId):
                self._handleRendererChanged(plugId)
                self.updateGL()
                return True
            else:
                return False
        return True

    def GetRendererAovs(self):
        if self._renderer:
            return self._renderer.GetRendererAovs()
        else:
            return []

    def SetRendererAov(self, aov):
        if self._renderer:
            if self._renderer.SetRendererAov(aov):
                self._rendererAovName = aov
                self.updateGL()
                return True
            else:
                return False
        return True

    def GetRendererSettingsList(self):
        if self._renderer:
            return self._renderer.GetRendererSettingsList()
        else:
            return []

    def GetRendererSetting(self, name):
        if self._renderer:
            return self._renderer.GetRendererSetting(name)
        else:
            return None

    def SetRendererSetting(self, name, value):
        if self._renderer:
            self._renderer.SetRendererSetting(name, value)
            self.updateGL()

    def GetRendererCommands(self):
        if self._renderer:
            return self._renderer.GetRendererCommandDescriptors()
        else:
            return []

    def InvokeRendererCommand(self, command):
        if self._renderer:
            return self._renderer.InvokeRendererCommand(command.commandName)
        else:
            return False

    def SetRendererPaused(self, paused):
        if self._renderer and (not self._renderer.IsConverged()):
            if paused:
                self._renderPauseState = self._renderer.PauseRenderer()
            else:
                self._renderPauseState = not self._renderer.ResumeRenderer()
            self.updateGL()

    def IsPauseRendererSupported(self):
        if self._renderer:
            if self._renderer.IsPauseRendererSupported():
                return True

        return False

    def IsRendererConverged(self):
        return self._renderer and self._renderer.IsConverged()

    def SetRendererStopped(self, stopped):
        if self._renderer:
            if stopped:
                self._renderStopState = self._renderer.StopRenderer()
            else:
                self._renderStopState = not self._renderer.RestartRenderer()
            self.updateGL()

    def IsStopRendererSupported(self):
        if self._renderer:
            if self._renderer.IsStopRendererSupported():
                return True

        return False

    def _stageReplaced(self):
        '''Set the USD Stage this widget will be displaying. To decommission
        (even temporarily) this widget, supply None as 'stage'.'''

        self.allSceneCameras = None

        if self._dataModel.stage:
            self._stageIsZup = (
                UsdGeom.GetStageUpAxis(self._dataModel.stage) == UsdGeom.Tokens.z)
            self._dataModel.viewSettings.freeCamera = \
                    FreeCamera(self._stageIsZup,
                               self._dataModel.viewSettings.freeCameraFOV)

    # simple GLSL program for axis/bbox drawings
    def GetSimpleGLSLProgram(self):
        if self._simpleGLSLProgram == None:
            self._simpleGLSLProgram = GLSLProgram(
            """#version 140
               uniform mat4 mvpMatrix;
               in vec3 position;
               void main() { gl_Position = vec4(position, 1)*mvpMatrix; }""",
            """#version 140
               out vec4 outColor;
               uniform vec4 color;
               void main() { outColor = color; }""",
            """#version 120
               uniform mat4 mvpMatrix;
               attribute vec3 position;
               void main() { gl_Position = vec4(position, 1)*mvpMatrix; }""",
            """#version 120
               uniform vec4 color;
               void main() { gl_FragColor = color; }""",
            ["mvpMatrix", "color"])
        return self._simpleGLSLProgram

    def DrawAxis(self, viewProjectionMatrix):
        from OpenGL import GL
        import ctypes

        # grab the simple shader
        glslProgram = self.GetSimpleGLSLProgram()
        if glslProgram.program == 0:
            return

        # vao
        if glslProgram.useVAO:
            if (self._vao == 0):
                self._vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(self._vao)

        # prep a vbo for axis
        if (self._axisVBO is None):
            self._axisVBO = GL.glGenBuffers(1)
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._axisVBO)
            data = [1, 0, 0, 0, 0, 0,
                    0, 1, 0, 0, 0, 0,
                    0, 0, 1, 0, 0, 0]
            GL.glBufferData(GL.GL_ARRAY_BUFFER, len(data)*4,
                            (ctypes.c_float*len(data))(*data), GL.GL_STATIC_DRAW)

        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._axisVBO)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))

        GL.glUseProgram(glslProgram.program)
        # i *think* this actually wants the camera dist so that the axis stays
        # somewhat fixed in screen-space size.
        mvpMatrix = Gf.Matrix4f().SetScale(self._dist/20.0) * viewProjectionMatrix
        matrix = (ctypes.c_float*16).from_buffer_copy(mvpMatrix)
        GL.glUniformMatrix4fv(glslProgram.uniformLocations["mvpMatrix"],
                              1, GL.GL_TRUE, matrix)

        GL.glUniform4f(glslProgram.uniformLocations["color"], 1, 0, 0, 1)
        GL.glDrawArrays(GL.GL_LINES, 0, 2)
        GL.glUniform4f(glslProgram.uniformLocations["color"], 0, 1, 0, 1)
        GL.glDrawArrays(GL.GL_LINES, 2, 2)
        GL.glUniform4f(glslProgram.uniformLocations["color"], 0, 0, 1, 1)
        GL.glDrawArrays(GL.GL_LINES, 4, 2)

        GL.glDisableVertexAttribArray(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        GL.glUseProgram(0)

        if glslProgram.useVAO:
            GL.glBindVertexArray(0)

    def DrawBBox(self, viewProjectionMatrix):
        col = self._dataModel.viewSettings.clearColor
        color = Gf.Vec3f(col[0]-.6 if col[0]>0.5 else col[0]+.6,
                         col[1]-.6 if col[1]>0.5 else col[1]+.6,
                         col[2]-.6 if col[2]>0.5 else col[2]+.6)
        color[0] = Gf.Clamp(color[0], 0, 1); 
        color[1] = Gf.Clamp(color[1], 0, 1); 
        color[2] = Gf.Clamp(color[2], 0, 1);                 

        # Draw axis-aligned bounding box
        if self._dataModel.viewSettings.showAABBox:
            bsize = self._selectionBrange.max - self._selectionBrange.min

            trans = Gf.Transform()
            trans.SetScale(0.5*bsize)
            trans.SetTranslation(self._bbcenterForBoxDraw)

            self.drawWireframeCube(color,
                                   Gf.Matrix4f(trans.GetMatrix()) * viewProjectionMatrix)

        # Draw oriented bounding box
        if self._dataModel.viewSettings.showOBBox:
            bsize = self._selectionOrientedRange.max - self._selectionOrientedRange.min
            center = bsize / 2. + self._selectionOrientedRange.min
            trans = Gf.Transform()
            trans.SetScale(0.5*bsize)
            trans.SetTranslation(center)

            self.drawWireframeCube(color,
                                   Gf.Matrix4f(trans.GetMatrix()) *
                                   Gf.Matrix4f(self._selectionBBox.matrix) *
                                   viewProjectionMatrix)

    # XXX:
    # First pass at visualizing cameras in usdview-- just oracles for
    # now. Eventually the logic should live in usdImaging, where the delegate
    # would add the camera guide geometry to the GL buffers over the course over
    # its stage traversal, and get time samples accordingly.
    def DrawCameraGuides(self, mvpMatrix):
        from OpenGL import GL
        import ctypes

        # prep a vbo for camera guides
        if (self._cameraGuidesVBO is None):
            self._cameraGuidesVBO = GL.glGenBuffers(1)

        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._cameraGuidesVBO)
        data = []
        for camera in self._allSceneCameras:
            # Don't draw guides for the active camera.
            if camera == self._dataModel.viewSettings.cameraPrim or not (camera and camera.IsActive()):
                continue

            gfCamera = UsdGeom.Camera(camera).GetCamera(
                self._dataModel.currentFrame)
            frustum = gfCamera.frustum

            # (Gf documentation seems to be wrong)-- Ordered as
            # 0: left bottom near
            # 1: right bottom near
            # 2: left top near
            # 3: right top near
            # 4: left bottom far
            # 5: right bottom far
            # 6: left top far
            # 7: right top far
            oraclePoints = frustum.ComputeCorners()

            # Near plane
            indices = [0,1,1,3,3,2,2,0, # Near plane
                       4,5,5,7,7,6,6,4, # Far plane
                       3,7,0,4,1,5,2,6] # Lines between near and far planes.
            data.extend([oraclePoints[i][j] for i in indices for j in range(3)])

        GL.glBufferData(GL.GL_ARRAY_BUFFER, len(data)*4,
                        (ctypes.c_float*len(data))(*data), GL.GL_STATIC_DRAW)

        # grab the simple shader
        glslProgram = self.GetSimpleGLSLProgram()
        if (glslProgram.program == 0):
            return

        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))

        GL.glUseProgram(glslProgram.program)
        matrix = (ctypes.c_float*16).from_buffer_copy(mvpMatrix)
        GL.glUniformMatrix4fv(glslProgram.uniformLocations["mvpMatrix"],
                              1, GL.GL_TRUE, matrix)
        # Grabbed fallback oracleColor from CamCamera.
        GL.glUniform4f(glslProgram.uniformLocations["color"],
                       0.82745, 0.39608, 0.1647, 1)

        GL.glDrawArrays(GL.GL_LINES, 0, len(data)//3)

        GL.glDisableVertexAttribArray(0)
        GL.glUseProgram(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)

    def updateBboxPurposes(self):
        includedPurposes = self._dataModel.includedPurposes

        if self._dataModel.viewSettings.displayGuide:
            includedPurposes.add(UsdGeom.Tokens.guide)
        elif UsdGeom.Tokens.guide in includedPurposes:
            includedPurposes.remove(UsdGeom.Tokens.guide)

        if self._dataModel.viewSettings.displayProxy:
            includedPurposes.add(UsdGeom.Tokens.proxy)
        elif UsdGeom.Tokens.proxy in includedPurposes:
            includedPurposes.remove(UsdGeom.Tokens.proxy)

        if self._dataModel.viewSettings.displayRender:
            includedPurposes.add(UsdGeom.Tokens.render)
        elif UsdGeom.Tokens.render in includedPurposes:
            includedPurposes.remove(UsdGeom.Tokens.render)

        self._dataModel.includedPurposes = includedPurposes
        # force the bbox to refresh
        self._bbox = Gf.BBox3d()

    def recomputeBBox(self):
        selectedPrims = self._dataModel.selection.getLCDPrims()
        try:
            startTime = time()
            self._bbox = self.getStageBBox()
            if len(selectedPrims) == 1 and selectedPrims[0].GetPath() == '/':
                if self._bbox.GetRange().IsEmpty():
                    self._selectionBBox = self._getDefaultBBox()
                else:
                    self._selectionBBox = self._bbox
            else:
                self._selectionBBox = self.getSelectionBBox()

            # BBox computation time for HUD
            endTime = time()
            ms = (endTime - startTime) * 1000.
            self.signalBboxUpdateTimeChanged.emit(ms)

        except RuntimeError:
            # This may fail, but we want to keep the UI available,
            # so print the error and attempt to continue loading
            self.signalErrorMessage.emit("unable to get bounding box on "
               "stage at frame {0}".format(self._dataModel.currentFrame))
            import traceback
            traceback.print_exc()
            self._bbox = self._getEmptyBBox()
            self._selectionBBox = self._getDefaultBBox()

        self._selectionBrange = self._selectionBBox.ComputeAlignedRange()
        self._selectionOrientedRange = self._selectionBBox.box
        self._bbcenterForBoxDraw = self._selectionBBox.ComputeCentroid()

    def resetCam(self, frameFit=1.1):
        validFrameRange = (not self._selectionBrange.IsEmpty() and
            self._selectionBrange.GetMax() != self._selectionBrange.GetMin())
        if validFrameRange:
            self.switchToFreeCamera(False)
            self._dataModel.viewSettings.freeCamera.frameSelection(self._selectionBBox,
                frameFit)
            if self._dataModel.viewSettings.autoComputeClippingPlanes:
                self.computeAndSetClosestDistance()

    def updateView(self, resetCam=False, forceComputeBBox=False, frameFit=1.1):
        '''Updates bounding boxes and camera. resetCam = True causes the camera to reframe
        the specified prims. frameFit sets the ratio of the camera's frustum's
        relevant dimension to the object's bounding box. 1.1, the default,
        fits the prim's bounding box in the frame with a roughly 10% margin.
        '''

        # Only compute BBox if forced, if needed for drawing,
        # or if this is the first time running.
        computeBBox = forceComputeBBox or \
                     (self._dataModel.viewSettings.showBBoxes and
                      (self._dataModel.viewSettings.showAABBox or self._dataModel.viewSettings.showOBBox))\
                     or self._bbox.GetRange().IsEmpty()
        if computeBBox:
            self.recomputeBBox()
        if resetCam:
            self.resetCam(frameFit)

        self.updateGL()

    def updateSelection(self):
        try:
            renderer = self._getRenderer()
            if not renderer:
                # error has already been issued
                return

            renderer.ClearSelected()

            psuRoot = self._dataModel.stage.GetPseudoRoot()
            allInstances = self._dataModel.selection.getPrimInstances()
            for prim in self._dataModel.selection.getLCDPrims():
                if prim == psuRoot:
                    continue
                primInstances = allInstances[prim]
                if primInstances != ALL_INSTANCES:
                    for instanceIndex in primInstances:
                        renderer.AddSelected(prim.GetPath(), instanceIndex)
                else:
                    renderer.AddSelected(
                        prim.GetPath(), UsdImagingGL.ALL_INSTANCES)
        except Tf.ErrorException as e:
            # If we encounter an error, we want to continue running. Just log 
            # the error and continue.
            sys.stderr.write(
                "ERROR: Usdview encountered an error while updating selection."
                "{}\n".format(e))
        finally:
            # Make sure not to leak a reference to the renderer
            renderer = None

    def _getEmptyBBox(self):
        # This returns the default empty bbox [FLT_MAX,-FLT_MAX]
        return Gf.BBox3d()

    def _getDefaultBBox(self):
        return Gf.BBox3d(Gf.Range3d((-10,-10,-10), (10,10,10)))

    def _isInfiniteBBox(self, bbox):
        return isinf(bbox.GetRange().GetMin().GetLength()) or \
               isinf(bbox.GetRange().GetMax().GetLength())

    def getStageBBox(self):
        bbox = self._dataModel.computeWorldBound(
            self._dataModel.stage.GetPseudoRoot())
        if bbox.GetRange().IsEmpty() or self._isInfiniteBBox(bbox):
            bbox = self._getEmptyBBox()
        return bbox

    def getSelectionBBox(self):
        bbox = Gf.BBox3d()
        for n in self._dataModel.selection.getLCDPrims():
            if n.IsActive() and not n.IsInPrototype():
                primBBox = self._dataModel.computeWorldBound(n)
                bbox = Gf.BBox3d.Combine(bbox, primBBox)
        return bbox

    def renderSinglePass(self, renderMode, renderSelHighlights):
        if not self._dataModel.stage:
            return
        renderer = self._getRenderer()
        if not renderer:
            # error has already been issued
            return

        # update rendering parameters
        self._renderParams.frame = self._dataModel.currentFrame
        self._renderParams.complexity = self._dataModel.viewSettings.complexity.value
        self._renderParams.drawMode = renderMode
        self._renderParams.showGuides = self._dataModel.viewSettings.displayGuide
        self._renderParams.showProxy = self._dataModel.viewSettings.displayProxy
        self._renderParams.showRender = self._dataModel.viewSettings.displayRender
        self._renderParams.forceRefresh = self._forceRefresh
        self._renderParams.cullStyle = \
            (UsdImagingGL.CullStyle.CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED
               if self._dataModel.viewSettings.cullBackfaces
               else UsdImagingGL.CullStyle.CULL_STYLE_NOTHING)
        self._renderParams.gammaCorrectColors = False
        self._renderParams.enableIdRender = self._dataModel.viewSettings.displayPrimId
        self._renderParams.enableSampleAlphaToCoverage = not self._dataModel.viewSettings.displayPrimId
        self._renderParams.highlight = renderSelHighlights
        self._renderParams.enableSceneMaterials = self._dataModel.viewSettings.enableSceneMaterials
        self._renderParams.enableSceneLights = self._dataModel.viewSettings.enableSceneLights
        self._renderParams.clearColor = Gf.Vec4f(self._dataModel.viewSettings.clearColor)

        ccMode = self._dataModel.viewSettings.colorCorrectionMode
        self._renderParams.colorCorrectionMode = ccMode
        self._renderParams.ocioDisplay, self._renderParams.ocioView, self._renderParams.ocioColorSpace = \
            (self._dataModel.viewSettings.ocioConfig.display,
               self._dataModel.viewSettings.ocioConfig.view,
               self._dataModel.viewSettings.ocioConfig.colorSpace) if ccMode == ColorCorrectionModes.OPENCOLORIO else \
            ('','','')

        pseudoRoot = self._dataModel.stage.GetPseudoRoot()

        renderer.SetSelectionColor(self._dataModel.viewSettings.highlightColor)

        try:
            renderer.Render(pseudoRoot, self._renderParams)
        except Tf.ErrorException as e:
            # If we encounter an error during a render, we want to continue
            # running. Just log the error and continue.
            sys.stderr.write(
                "ERROR: Usdview encountered an error while rendering.{}\n".format(e))
        finally:
            # Make sure not to leak a reference to the renderer
            renderer = None
        self._forceRefresh = False


    def initializeGL(self):
        if not self.isValid():
            return
        from pxr import Glf
        Glf.RegisterDefaultDebugOutputMessageCallback()

    def updateGL(self):
        """We override this virtual so that we can make it a no-op during
        playback.  The client driving playback at a particular rate should
        instead call updateForPlayback() to image the next frame."""
        if not self._dataModel.playing:
            super(StageView, self).updateGL()

    def updateForPlayback(self):
        """If playing, update the GL canvas.  Otherwise a no-op"""
        if self._dataModel.playing:
            super(StageView, self).updateGL()

    def getActiveSceneCamera(self):
        cameraPrim = self._dataModel.viewSettings.cameraPrim
        if cameraPrim and cameraPrim.IsActive():
            return cameraPrim
        return None
    
    # XXX: Consolidate window/frustum conformance code that is littered in
    # several places.
    def computeWindowPolicy(self, cameraAspectRatio):
        # The freeCam always uses 'MatchVertically'.
        # When using a scene cam, we factor in the masking setting and window
        # size to compute it.
        windowPolicy = CameraUtil.MatchVertically
        
        if self.getActiveSceneCamera():
            if self._cropImageToCameraViewport:
                targetAspect = (
                    float(self.size().width()) / max(1.0, self.size().height()))

                if targetAspect < cameraAspectRatio:
                    windowPolicy =  CameraUtil.MatchHorizontally
            else:
                if self._fitCameraInViewport:
                    windowPolicy =  CameraUtil.Fit
        
        return windowPolicy
    
    def computeWindowSize(self):
        size = self.size() * self.devicePixelRatioF()
        return (int(size.width()), int(size.height()))

    def computeWindowViewport(self):
        return (0, 0) + self.computeWindowSize()

    def resolveCamera(self):
        """Returns a tuple of the camera to use for rendering (either a scene
        camera or a free camera) and that camera's original aspect ratio.
        Depending on camera guide settings, the camera frustum may be conformed
        to fit the window viewport. Emits a signalFrustumChanged if the
        camera frustum has changed since the last time resolveCamera was called."""

        # If 'camera' is None, make sure we have a valid freeCamera
        sceneCam = self.getActiveSceneCamera()
        if sceneCam:
            gfCam = UsdGeom.Camera(sceneCam).GetCamera(
                                                self._dataModel.currentFrame)
        else:
            self.switchToFreeCamera()
            gfCam = self._dataModel.viewSettings.freeCamera.computeGfCamera(
                            self._bbox, autoClip=self.autoClip)

        cameraAspectRatio = gfCam.aspectRatio

        # Conform the camera's frustum to the window viewport, if necessary.
        if not self._cropImageToCameraViewport:
            targetAspect = float(self.size().width()) / max(1.0, self.size().height())
            if self._fitCameraInViewport:
                CameraUtil.ConformWindow(gfCam, CameraUtil.Fit, targetAspect)
            else:
                CameraUtil.ConformWindow(gfCam, CameraUtil.MatchVertically, targetAspect)

        frustumChanged = ((not self._lastComputedGfCamera) or
                          self._lastComputedGfCamera.frustum != gfCam.frustum)
        # We need to COPY the camera, not assign it...
        self._lastComputedGfCamera = Gf.Camera(gfCam)
        if frustumChanged:
            self.signalFrustumChanged.emit()
        return (gfCam, cameraAspectRatio)

    def computeCameraViewport(self, cameraAspectRatio):
        # Conform the camera viewport to the camera's aspect ratio,
        # and center the camera viewport in the window viewport.
        windowPolicy = CameraUtil.MatchVertically
        targetAspect = (
          float(self.size().width()) / max(1.0, self.size().height()))
        if targetAspect < cameraAspectRatio:
            windowPolicy = CameraUtil.MatchHorizontally

        viewport = Gf.Range2d(Gf.Vec2d(0, 0),
                              Gf.Vec2d(self.computeWindowSize()))
        viewport = CameraUtil.ConformedWindow(viewport, windowPolicy, cameraAspectRatio)

        viewport = (viewport.GetMin()[0], viewport.GetMin()[1],
                    viewport.GetSize()[0], viewport.GetSize()[1])
        viewport = ViewportMakeCenteredIntegral(viewport)

        return viewport

    def copyViewState(self):
        """Returns a copy of this StageView's view-affecting state,
        which can be used later to restore the view via restoreViewState().
        Take note that we do NOT include the StageView's notion of the
        current time (used by prim-based cameras to extract their data),
        since we do not want a restore operation to put us out of sync
        with respect to our owner's time.
        """
        viewState = {}
        viewState["_cameraPrim"] = self._dataModel.viewSettings.cameraPrim
        viewState["_stageIsZup"] = self._stageIsZup
        viewState["_overrideNear"] = self._overrideNear
        viewState["_overrideFar"] = self._overrideFar
        # Since FreeCamera is a compound/class object, we must copy
        # it more deeply
        viewState["_freeCamera"] = self._dataModel.viewSettings.freeCamera.clone() if self._dataModel.viewSettings.freeCamera else None
        return viewState

    def restoreViewState(self, viewState):
        """Restore view parameters from 'viewState', and redraw"""
        self._dataModel.viewSettings.cameraPrim = viewState["_cameraPrim"]
        self._stageIsZup = viewState["_stageIsZup"]
        self._overrideNear = viewState["_overrideNear"]
        self._overrideFar = viewState["_overrideFar"]

        restoredCamera = viewState["_freeCamera"]
        # Detach our freeCamera from the given viewState, to
        # insulate against changes to viewState by caller
        self._dataModel.viewSettings.freeCamera = restoredCamera.clone() if restoredCamera else None

        self.update()

    def drawWireframeCube(self, col, mvpMatrix):
        from OpenGL import GL
        import ctypes, itertools

        # grab the simple shader
        glslProgram = self.GetSimpleGLSLProgram()
        if (glslProgram.program == 0):
            return
        # vao
        if glslProgram.useVAO:
            if (self._vao == 0):
                self._vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(self._vao)

        # prep a vbo for bbox
        if (self._bboxVBO is None):
            self._bboxVBO = GL.glGenBuffers(1)
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._bboxVBO)
            # create 12 edges
            data = []
            p = list(itertools.product([-1,1],[-1,1],[-1,1]))
            for i in p:
                data.extend([i[0], i[1], i[2]])
            for i in p:
                data.extend([i[1], i[2], i[0]])
            for i in p:
                data.extend([i[2], i[0], i[1]])

            GL.glBufferData(GL.GL_ARRAY_BUFFER, len(data)*4,
                            (ctypes.c_float*len(data))(*data), GL.GL_STATIC_DRAW)

        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._bboxVBO)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))

        GL.glEnable(GL.GL_LINE_STIPPLE)
        GL.glLineStipple(2,0xAAAA)

        GL.glUseProgram(glslProgram.program)
        matrix = (ctypes.c_float*16).from_buffer_copy(mvpMatrix)
        GL.glUniformMatrix4fv(glslProgram.uniformLocations["mvpMatrix"],
                              1, GL.GL_TRUE, matrix)
        GL.glUniform4f(glslProgram.uniformLocations["color"],
                       col[0], col[1], col[2], 1)

        GL.glDrawArrays(GL.GL_LINES, 0, 24)

        GL.glDisableVertexAttribArray(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        GL.glUseProgram(0)

        GL.glDisable(GL.GL_LINE_STIPPLE)
        if glslProgram.useVAO:
            GL.glBindVertexArray(0)

    def paintGL(self):
        if not self._dataModel.stage:
            return
        renderer = self._getRenderer()
        if not renderer:
            # error has already been issued
            return

        try:
            from OpenGL import GL

            if self._dataModel.viewSettings.showHUD_GPUstats:
                if self._glPrimitiveGeneratedQuery is None:
                    self._glPrimitiveGeneratedQuery = Glf.GLQueryObject()
                if self._glTimeElapsedQuery is None:
                    self._glTimeElapsedQuery = Glf.GLQueryObject()
                self._glPrimitiveGeneratedQuery.BeginPrimitivesGenerated()
                self._glTimeElapsedQuery.BeginTimeElapsed()

            if not UsdImagingGL.Engine.IsColorCorrectionCapable():
                from OpenGL.GL.EXT.framebuffer_sRGB import GL_FRAMEBUFFER_SRGB_EXT
                GL.glEnable(GL_FRAMEBUFFER_SRGB_EXT)

            # Clear the default FBO associated with the widget/context to
            # fully transparent and *not* the bg color.
            # The bg color is used as the clear color for the aov, and the
            # results of rendering are composited over the FBO (and not blit).
            GL.glClearColor(*Gf.Vec4f(0,0,0,0))

            GL.glEnable(GL.GL_DEPTH_TEST)
            GL.glDepthFunc(GL.GL_LESS)

            GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
            GL.glEnable(GL.GL_BLEND)

            # Note: camera lights and camera guides require the
            # resolved (adjusted) camera viewProjection matrix, which is
            # why we resolve the camera above always.
            (gfCamera, cameraAspect) = self.resolveCamera()
            frustum = gfCamera.frustum
            cameraViewport = self.computeCameraViewport(cameraAspect)

            viewport = self.computeWindowViewport()
            windowViewport = viewport
            if self._cropImageToCameraViewport:
                viewport = cameraViewport

            # For legacy implementation (--renderer HydraDisabled)
            if not renderer.IsHydraEnabled():
                renderer.SetRenderViewport(viewport)
                renderer.SetWindowPolicy(self.computeWindowPolicy(cameraAspect))

            renderBufferSize = Gf.Vec2i(self.computeWindowSize())

            renderer.SetRenderBufferSize(
                renderBufferSize)
            renderer.SetFraming(
                _ComputeCameraFraming(viewport, renderBufferSize))
            renderer.SetOverrideWindowPolicy(
                self.computeWindowPolicy(cameraAspect))

            sceneCam = self.getActiveSceneCamera()
            if sceneCam:
                # When using a USD camera, simply set it as the active camera.
                # Window policy conformance is handled in the engine/hydra.
                renderer.SetCameraPath(sceneCam.GetPath())
            else:
            # When using the free cam (which isn't currently backed on the
            # USD stage), we send the camera matrices to the engine.
                renderer.SetCameraState(frustum.ComputeViewMatrix(),
                                        frustum.ComputeProjectionMatrix())

            viewProjectionMatrix = Gf.Matrix4f(frustum.ComputeViewMatrix()
                                            * frustum.ComputeProjectionMatrix())

            GL.glClear(GL.GL_COLOR_BUFFER_BIT|GL.GL_DEPTH_BUFFER_BIT)

            # ensure viewport is right for the camera framing
            GL.glViewport(*viewport)

            # Set the clipping planes.
            self._renderParams.clipPlanes = [Gf.Vec4d(i) for i in
                                            gfCamera.clippingPlanes]

            if len(self._dataModel.selection.getLCDPrims()) > 0:
                cam_pos = frustum.position
                sceneAmbient = (0.01, 0.01, 0.01, 1.0)
                material = Glf.SimpleMaterial()
                lights = []
                # for renderModes that need lights
                if self._dataModel.viewSettings.renderMode in ShadedRenderModes:

                    # ambient light located at the camera
                    if self._dataModel.viewSettings.ambientLightOnly:
                        l = Glf.SimpleLight()
                        l.ambient = (0, 0, 0, 0)
                        l.position = (cam_pos[0], cam_pos[1], cam_pos[2], 1)
                        lights.append(l)

                    # Default Dome Light
                    if self._dataModel.viewSettings.domeLightEnabled:
                        l = Glf.SimpleLight()
                        l.isDomeLight = True
                        if self._stageIsZup:
                            l.transform = Gf.Matrix4d().SetRotate(
                                    Gf.Rotation(Gf.Vec3d.XAxis(), 90))
                        lights.append(l)

                    kA = self._dataModel.viewSettings.defaultMaterialAmbient
                    kS = self._dataModel.viewSettings.defaultMaterialSpecular
                    material.ambient = (kA, kA, kA, 1.0)
                    material.specular = (kS, kS, kS, 1.0)
                    material.shininess = 32.0

                # modes that want no lighting simply leave lights as an empty list
                renderer.SetLightingState(lights, material, sceneAmbient)

                if self._dataModel.viewSettings.renderMode == RenderModes.HIDDEN_SURFACE_WIREFRAME:
                    GL.glEnable( GL.GL_POLYGON_OFFSET_FILL )
                    GL.glPolygonOffset( 1.0, 1.0 )
                    GL.glPolygonMode( GL.GL_FRONT_AND_BACK, GL.GL_FILL )

                    self.renderSinglePass( 
                        UsdImagingGL.DrawMode.DRAW_GEOM_ONLY, False)

                    GL.glDisable( GL.GL_POLYGON_OFFSET_FILL )

                    # Use display space for the second clear when color 
                    # correction is performed by the engine because we
                    # composite the framebuffer contents with the
                    # color-corrected (i.e., display space) aov contents.
                    clearColor = Gf.ConvertLinearToDisplay(Gf.Vec4f(
                        self._dataModel.viewSettings.clearColor))

                    if not UsdImagingGL.Engine.IsColorCorrectionCapable():
                        # Use linear color when using the sRGB extension
                        clearColor = Gf.Vec4f(
                            self._dataModel.viewSettings.clearColor)

                    GL.glClearColor(*clearColor)
                    GL.glClear(GL.GL_COLOR_BUFFER_BIT)

                highlightMode = self._dataModel.viewSettings.selHighlightMode
                if self._dataModel.playing:
                    # Highlight mode must be ALWAYS to draw highlights during playback.
                    drawSelHighlights = (
                        highlightMode == SelectionHighlightModes.ALWAYS)
                else:
                    # Highlight mode can be ONLY_WHEN_PAUSED or ALWAYS to draw
                    # highlights when paused.
                    drawSelHighlights = (
                        highlightMode != SelectionHighlightModes.NEVER)

                self.renderSinglePass(
                    self._renderModeDict[self._dataModel.viewSettings.renderMode],
                    drawSelHighlights)

                if not UsdImagingGL.Engine.IsColorCorrectionCapable():
                    GL.glDisable(GL_FRAMEBUFFER_SRGB_EXT)

                self.DrawAxis(viewProjectionMatrix)

                # XXX:
                # Draw camera guides-- no support for toggling guide visibility on
                # individual cameras until we move this logic directly into
                # usdImaging.
                if self._dataModel.viewSettings.displayCameraOracles:
                    self.DrawCameraGuides(viewProjectionMatrix)

                if self._dataModel.viewSettings.showBBoxes and\
                        (self._dataModel.viewSettings.showBBoxPlayback or not self._dataModel.playing):
                    self.DrawBBox(viewProjectionMatrix)
            else:
                GL.glClear(GL.GL_COLOR_BUFFER_BIT)

            if self._dataModel.viewSettings.showHUD_GPUstats:
                self._glPrimitiveGeneratedQuery.End()
                self._glTimeElapsedQuery.End()

            # reset the viewport for 2D and HUD drawing
            uiTasks = [ Prim2DSetupTask(self.computeWindowViewport()) ]
            if self._dataModel.viewSettings.showMask:
                color = self._dataModel.viewSettings.cameraMaskColor
                if self._dataModel.viewSettings.showMask_Opaque:
                    color = color[0:3] + (1.0,)
                else:
                    color = color[0:3] + (color[3] * 0.45,)
                self._mask.updateColor(color)
                self._mask.updatePrims(cameraViewport, self)
                uiTasks.append(self._mask)
            if self._dataModel.viewSettings.showMask_Outline:
                self._maskOutline.updatePrims(cameraViewport, self)
                uiTasks.append(self._maskOutline)
            if self.showReticles:
                color = self._dataModel.viewSettings.cameraReticlesColor
                color = color[0:3] + (color[3] * 0.85,)
                self._reticles.updateColor(color)
                self._reticles.updatePrims(cameraViewport, self,
                        self._dataModel.viewSettings.showReticles_Inside, self._dataModel.viewSettings.showReticles_Outside)
                uiTasks.append(self._reticles)

            for task in uiTasks:
                task.Sync(None)
            for task in uiTasks:
                task.Execute(None)

            # check current state of renderer -- (not IsConverged()) means renderer is running
            if self._renderStopState and (not renderer.IsConverged()):
                self._renderStopState = False

            # ### DRAW HUD ### #
            if self._dataModel.viewSettings.showHUD:
                self.drawHUD(renderer)

            if (not self._dataModel.playing) & (not renderer.IsConverged()):
                QtCore.QTimer.singleShot(5, self.update)
        
        except Exception as e:
            # If we encounter an error during a render, we want to continue 
            # running. Just log the error and continue.
            sys.stderr.write(
                "ERROR: Usdview encountered an error while rendering."
                "{}\n".format(e))

        finally:
            # Make sure not to leak a reference to the renderer
            renderer = None

    def drawHUD(self, renderer):
        # compute the time it took to render this frame,
        # so we can display it in the HUD
        ms = self._renderTime * 1000.
        fps = float("inf")
        if not self._renderTime == 0:
            fps = 1./self._renderTime
        # put the result in the HUD string
        self.fpsHUDInfo['Render'] = "%.2f ms (%.2f FPS)" % (ms, fps)

        col = Gf.Vec3f(.733,.604,.333)

        # the subtree info does not update while animating, grey it out
        if not self._dataModel.playing:
            subtreeCol = col
        else:
            subtreeCol = Gf.Vec3f(.6,.6,.6)

        # Subtree Info
        if self._dataModel.viewSettings.showHUD_Info:
            self._hud.updateGroup("TopLeft", 0, 14, subtreeCol,
                                 self.upperHUDInfo,
                                 self.HUDStatKeys)
        else:
            self._hud.updateGroup("TopLeft", 0, 0, subtreeCol, {})

        # Complexity
        if self._dataModel.viewSettings.showHUD_Complexity:
            # Camera name
            camName = "Free%s" % (" AutoClip" if self.autoClip else "")
            if self._dataModel.viewSettings.cameraPrim:
                camName = self._dataModel.viewSettings.cameraPrim.GetName()

            toPrint = {"Complexity" : self._dataModel.viewSettings.complexity.name,
                       "Camera" : camName}
            self._hud.updateGroup("BottomRight",
                                  self.width()-210, self.height()-self._hud._HUDLineSpacing*2,
                                  col, toPrint)
        else:
            self._hud.updateGroup("BottomRight", 0, 0, col, {})

        # Hydra Enabled (Top Right)
        hydraMode = "Disabled"

        if UsdImagingGL.Engine.IsHydraEnabled():
            hydraMode = self._rendererDisplayName
            if not hydraMode:
                hydraMode = "Enabled"

        if self._renderPauseState:
            toPrint = {"Hydra": "(paused)"}
        elif self._renderStopState:
            toPrint = {"Hydra": "(stopped)"}
        else:
            toPrint = {"Hydra": hydraMode}
            
        if self._rendererAovName != "color":
            toPrint["  AOV"] = self._rendererAovName
        self._hud.updateGroup("TopRight", self.width()-160, 14, col,
                              toPrint, toPrint.keys())

        # bottom left
        from collections import OrderedDict
        toPrint = OrderedDict()

        # GPU stats (TimeElapsed is in nano seconds)
        if self._dataModel.viewSettings.showHUD_GPUstats:

            def _addSizeMetric(toPrint, stats, label, key):
                if key in stats:
                    toPrint[label] = ReportMetricSize(stats[key])

            rStats = renderer.GetRenderStats()

            toPrint["GL prims "] = self._glPrimitiveGeneratedQuery.GetResult()
            if not (self._renderPauseState or self._renderStopState):
                toPrint["GPU time "] = "%.2f ms " % (self._glTimeElapsedQuery.GetResult() / 1000000.0)
            _addSizeMetric(toPrint, rStats, "GPU mem  ", "gpuMemoryUsed")
            _addSizeMetric(toPrint, rStats, " primvar ", "primvar")
            _addSizeMetric(toPrint, rStats, " topology", "topology")
            _addSizeMetric(toPrint, rStats, " shader  ", "drawingShader")
            _addSizeMetric(toPrint, rStats, " texture ", "textureMemory")
            
            if "numCompletedSamples" in rStats:
                toPrint["Samples done "] = rStats["numCompletedSamples"]

        # Playback Rate
        if (not (self._renderPauseState or self._renderStopState)) and \
                            self._dataModel.viewSettings.showHUD_Performance:
            for key in self.fpsHUDKeys:
                toPrint[key] = self.fpsHUDInfo[key]
        self._hud.updateGroup("BottomLeft",
                              0, self.height()-len(toPrint)*self._hud._HUDLineSpacing,
                              col, toPrint, toPrint.keys())

        # draw HUD
        self._hud.draw(self)

    def sizeHint(self):
        return QtCore.QSize(460, 460)

    def switchToFreeCamera(self, computeAndSetClosestDistance=True):
        """
        If our current camera corresponds to a prim, create a FreeCamera
        that has the same view and use it.
        """
        if self._dataModel.viewSettings.cameraPrim != None:
            # cameraPrim may no longer be valid, so use the last-computed
            # gf camera
            if self._lastComputedGfCamera:
                self._dataModel.viewSettings.freeCamera = FreeCamera.FromGfCamera(
                    self._lastComputedGfCamera, self._stageIsZup)
            else:
                self._dataModel.viewSettings.freeCamera = FreeCamera(
                    self._stageIsZup,
                    self._dataModel.viewSettings.freeCameraFOV)

            # override clipping plane state is managed by StageView,
            # so that it can be persistent.  Therefore we must restore it
            # now
            self._dataModel.viewSettings.freeCamera.overrideNear = self._overrideNear
            self._dataModel.viewSettings.freeCamera.overrideFar = self._overrideFar
            self._dataModel.viewSettings.cameraPrim = None
            if computeAndSetClosestDistance:
                self.computeAndSetClosestDistance()
            # let the controller know we've done this!
            self.signalSwitchedToFreeCam.emit()

    # It WBN to support marquee selection in the viewer also, at some point...
    def mousePressEvent(self, event):
        """This widget claims the Alt modifier key as the enabler for camera
        manipulation, and will consume mousePressEvents when Alt is present.
        In any other modifier state, a mousePressEvent will result in a
        pick operation, and the pressed button and active modifiers will be
        made available to clients via a signalPrimSelected()."""

        # It's important to set this first, since pickObject(), called below
        # may produce the mouse-up event that will terminate the drag
        # initiated by this mouse-press
        self._dragActive = True

        # Note: multiplying by devicePixelRatio is only necessary because this
        # is a QGLWidget.
        x = event.x() * self.devicePixelRatioF()
        y = event.y() * self.devicePixelRatioF()

        # Allow for either meta or alt key, since meta maps to Windows and Apple
        # keys on various hardware/os combos, and some windowing systems consume
        # one or the other by default, but hopefully not both.
        if (event.modifiers() & (QtCore.Qt.AltModifier | QtCore.Qt.MetaModifier)):
            if event.button() == QtCore.Qt.LeftButton:
                self.switchToFreeCamera()
                ctrlModifier = event.modifiers() & QtCore.Qt.ControlModifier
                self._cameraMode = "truck" if ctrlModifier else "tumble"
            if event.button() == QtCore.Qt.MidButton:
                self.switchToFreeCamera()
                self._cameraMode = "truck"
            if event.button() == QtCore.Qt.RightButton:
                self.switchToFreeCamera()
                self._cameraMode = "zoom"
        else:
            self._cameraMode = "pick"
            self.pickObject(x, y, event.button(), event.modifiers())

        self._lastX = x
        self._lastY = y

    def mouseReleaseEvent(self, event):
        self._cameraMode = "none"
        self._dragActive = False

    def mouseMoveEvent(self, event):
        # Note: multiplying by devicePixelRatio is only necessary because this
        # is a QGLWidget.
        x = event.x() * self.devicePixelRatioF()
        y = event.y() * self.devicePixelRatioF()

        if self._dragActive:
            dx = x - self._lastX
            dy = y - self._lastY
            if dx == 0 and dy == 0:
                return

            freeCam = self._dataModel.viewSettings.freeCamera
            if self._cameraMode == "tumble":
                freeCam.Tumble(0.25 * dx, 0.25*dy)

            elif self._cameraMode == "zoom":
                zoomDelta = -.002 * (dx + dy)
                if freeCam.orthographic:
                    # orthographic cameras zoom by scaling fov
                    # fov is the height of the view frustum in world units
                    freeCam.fov *= (1 + zoomDelta)
                else:
                    # perspective cameras dolly forward or back
                    freeCam.AdjustDistance(1 + zoomDelta)

            elif self._cameraMode == "truck":
                height = float(self.size().height())
                pixelsToWorld = freeCam.ComputePixelsToWorldFactor(height)

                self._dataModel.viewSettings.freeCamera.Truck(
                        -dx * pixelsToWorld, 
                         dy * pixelsToWorld)

            self._lastX = x
            self._lastY = y
            self.updateGL()

            self.signalMouseDrag.emit()
        elif self._cameraMode == "none":
            # Mouse tracking is only enabled when rolloverPicking is enabled,
            # and this function only gets called elsewise when mouse-tracking
            # is enabled
            self.pickObject(event.x(), event.y(), None, event.modifiers())
        else:
            event.ignore()

    def wheelEvent(self, event):
        self.switchToFreeCamera()
        self._dataModel.viewSettings.freeCamera.AdjustDistance(
                1-max(-0.5,min(0.5,(event.angleDelta().y()/1000.))))
        self.updateGL()

    def _onAutoComputeClippingChanged(self):
        """If we are currently rendering from a prim camera, switch to the
        FreeCamera.  Then reset the near/far clipping planes based on
        distance to closest geometry.  But only when autoClip has turned on!"""
        if self._dataModel.viewSettings.autoComputeClippingPlanes:
            if not self._dataModel.viewSettings.freeCamera:
                self.switchToFreeCamera()
            else:
                self.computeAndSetClosestDistance()

    def computeAndSetClosestDistance(self):
        '''Using the current FreeCamera's frustum, determine the world-space
        closest rendered point to the camera.  Use that point
        to set our FreeCamera's closest visible distance.'''
        # pick() operates at very low screen resolution, but that's OK for
        # our purposes.  Ironically, the same limited Z-buffer resolution for
        # which we are trying to compensate may cause us to completely lose
        # ALL of our geometry if we set the near-clip really small (which we
        # want to do so we don't miss anything) when geometry is clustered
        # closer to far-clip.  So in the worst case, we may need to perform
        # two picks, with the first pick() using a small near and far, and the
        # second pick() using a near that keeps far within the safe precision
        # range.  We don't expect the worst-case to happen often.
        if not self._dataModel.viewSettings.freeCamera:
            return
        cameraFrustum = self.resolveCamera()[0].frustum
        trueFar = cameraFrustum.nearFar.max
        smallNear = min(FreeCamera.defaultNear,
                        self._dataModel.viewSettings.freeCamera._selSize / 10.0)
        cameraFrustum.nearFar = \
            Gf.Range1d(smallNear, smallNear*FreeCamera.maxSafeZResolution)
        pickResults = self.pick(cameraFrustum)
        if pickResults[0] is None or pickResults[2] == Sdf.Path.emptyPath:
            cameraFrustum.nearFar = \
                Gf.Range1d(trueFar/FreeCamera.maxSafeZResolution, trueFar)
            pickResults = self.pick(cameraFrustum)
            if Tf.Debug.IsDebugSymbolNameEnabled(DEBUG_CLIPPING):
                print("computeAndSetClosestDistance: Needed to call pick() a second time")

        if pickResults[0] is not None and pickResults[2] != Sdf.Path.emptyPath:
            self._dataModel.viewSettings.freeCamera.setClosestVisibleDistFromPoint(pickResults[0])
            self.updateView()

    def pick(self, pickFrustum):
        '''
        Find closest point in scene rendered through 'pickFrustum'.
        Returns a sextuple:
          selectedPoint, selectedNormal, selectedPrimPath,
          selectedInstancerPath, selectedInstanceIndex, selectedInstancerContext
        '''
        renderer = self._getRenderer()
        if not self._dataModel.stage or not renderer:
            # error has already been issued
            return None, None, Sdf.Path.emptyPath, None, None, None

        # this import is here to make sure the create_first_image stat doesn't
        # regress..
        from OpenGL import GL

        # Need a correct OpenGL Rendering context for FBOs
        self.makeCurrent()

        # update rendering parameters
        self._renderParams.frame = self._dataModel.currentFrame
        self._renderParams.complexity = self._dataModel.viewSettings.complexity.value
        self._renderParams.drawMode = self._renderModeDict[self._dataModel.viewSettings.renderMode]
        self._renderParams.showGuides = self._dataModel.viewSettings.displayGuide
        self._renderParams.showProxy = self._dataModel.viewSettings.displayProxy
        self._renderParams.showRender = self._dataModel.viewSettings.displayRender
        self._renderParams.forceRefresh = self._forceRefresh
        self._renderParams.cullStyle = \
            (UsdImagingGL.CullStyle.CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED
                   if self._dataModel.viewSettings.cullBackfaces
                   else UsdImagingGL.CullStyle.CULL_STYLE_NOTHING)
        self._renderParams.gammaCorrectColors = False
        self._renderParams.enableIdRender = True
        self._renderParams.enableSampleAlphaToCoverage = False
        self._renderParams.enableSceneMaterials = self._dataModel.viewSettings.enableSceneMaterials
        self._renderParams.enableSceneLights = self._dataModel.viewSettings.enableSceneLights

        results = renderer.TestIntersection(
                pickFrustum.ComputeViewMatrix(),
                pickFrustum.ComputeProjectionMatrix(),
                self._dataModel.stage.GetPseudoRoot(), self._renderParams)
        if Tf.Debug.IsDebugSymbolNameEnabled(DEBUG_CLIPPING):
            print("Pick results = {}".format(results))

        return results

    def computePickFrustum(self, x, y):

        # compute pick frustum
        (gfCamera, cameraAspect) = self.resolveCamera()
        cameraFrustum = gfCamera.frustum

        viewport = self.computeWindowViewport()
        if self._cropImageToCameraViewport:
            viewport = self.computeCameraViewport(cameraAspect)

        # normalize position and pick size by the viewport size
        point = Gf.Vec2d((x - viewport[0]) / float(viewport[2]),
                         (y - viewport[1]) / float(viewport[3]))
        point[0] = (point[0] * 2.0 - 1.0)
        point[1] = -1.0 * (point[1] * 2.0 - 1.0)

        size = Gf.Vec2d(1.0 / viewport[2], 1.0 / viewport[3])

        # "point" is normalized to the image viewport size, but if the image
        # is cropped to the camera viewport, the image viewport won't fill the
        # whole window viewport.  Clicking outside the image will produce
        # normalized coordinates > 1 or < -1; in this case, we should skip
        # picking.
        inImageBounds = (abs(point[0]) <= 1.0 and abs(point[1]) <= 1.0)

        return (inImageBounds, cameraFrustum.ComputeNarrowedFrustum(point, size))

    def pickObject(self, x, y, button, modifiers):
        '''
        Render stage into fbo with each piece as a different color.
        Emits a signalPrimSelected or signalRollover depending on
        whether 'button' is None.
        '''
        if not self._dataModel.stage:
            return
        renderer = self._getRenderer()
        if not renderer:
            # error has already been issued
            return
        try:
            (inImageBounds, pickFrustum) = self.computePickFrustum(x,y)

            if inImageBounds:
                selectedPoint, selectedNormal, selectedPrimPath, \
                selectedInstanceIndex, selectedTLPath, selectedTLIndex = \
                self.pick(pickFrustum)
            else:
                # If we're picking outside the image viewport (maybe because
                # camera guides are on), treat that as a de-select.
                selectedPoint, selectedNormal, selectedPrimPath, \
                selectedInstanceIndex, selectedTLPath, selectedTLIndex = \
                    [-1,-1], None, Sdf.Path.emptyPath, -1, Sdf.Path.emptyPath, -1
        
            # Correct for high DPI displays
            # Cast to int explicitly as some versions of PySide/Shiboken throw
            # when converting extremely small doubles held in selectedPoint
            coord = self._scaleMouseCoords(QtCore.QPoint(
                int(selectedPoint[0]), int(selectedPoint[1])))
            selectedPoint[0] = coord.x()
            selectedPoint[1] = coord.y()

            if button:
                self.signalPrimSelected.emit(
                    selectedPrimPath, selectedInstanceIndex, selectedTLPath,
                    selectedTLIndex, selectedPoint, button, modifiers)
            else:
                self.signalPrimRollover.emit(
                    selectedPrimPath, selectedInstanceIndex, selectedTLPath,
                    selectedTLIndex, selectedPoint, modifiers)
        except Tf.ErrorException as e:
            # If we encounter an error, we want to continue running. Just log 
            # the error and continue.
            sys.stderr.write(
                "ERROR: Usdview encountered an error while picking."
                "{}\n".format(e))
        finally:
            renderer = None

    def glDraw(self):
        # override glDraw so we can time it.
        with Timer() as t:
            QtOpenGL.QGLWidget.glDraw(self)

        # Render creation is a deferred operation, so the render may not
        # be initialized on entry to the function.
        #
        # This function itself can not create the render, as to create the
        # renderer we need a valid GL context, which QT has not made current
        # yet.
        #
        # So instead check that the render has been created after the fact.
        # The point is to avoid reporting an invalid first image time.
        
        if not self._renderer:
            # error has already been issued
            return


        self._renderTime = t.interval

        # If timings are being printed and this is the first time an image is
        # being drawn, report how long it took to do so.
        if self._printTiming and self._isFirstImage:
            self._isFirstImage = False
            t.PrintTime("create first image")

    def SetForceRefresh(self, val):
        self._forceRefresh = val or self._forceRefresh

    def ExportFreeCameraToStage(self, stage, defcamName='usdviewCam',
                                imgWidth=None, imgHeight=None):
        '''
        Export the free camera to the specified USD stage, if it is
        currently defined. If it is not active (i.e. we are viewing through
        a stage camera), raise a ValueError.
        '''
        if not self._dataModel.viewSettings.freeCamera:
            raise ValueError("StageView's Free Camera is not defined, so cannot"
                             " be exported")

        imgWidth = imgWidth if imgWidth is not None else self.width()
        imgHeight = imgHeight if imgHeight is not None else self.height()

        defcam = UsdGeom.Camera.Define(stage, '/'+defcamName)

        # Map free camera params to usd camera.  We do **not** want to burn
        # auto-clipping near/far into our exported camera
        gfCamera = self._dataModel.viewSettings.freeCamera.computeGfCamera(self._bbox, autoClip=False)

        targetAspect = float(imgWidth) / max(1.0, imgHeight)
        CameraUtil.ConformWindow(
            gfCamera, CameraUtil.MatchVertically, targetAspect)

        when = (self._dataModel.currentFrame
            if stage.HasAuthoredTimeCodeRange() else Usd.TimeCode.Default())

        defcam.SetFromCamera(gfCamera, when)

    def ExportSession(self, stagePath, defcamName='usdviewCam',
                      imgWidth=None, imgHeight=None):
        '''
        Export the free camera (if currently active) and session layer to a
        USD file at the specified stagePath that references the current-viewed
        stage.
        '''

        tmpStage = Usd.Stage.CreateNew(stagePath)
        if self._dataModel.stage:
            tmpStage.GetRootLayer().TransferContent(
                self._dataModel.stage.GetSessionLayer())

        if not self.cameraPrim:
            # Export the free camera if it's the currently-visible camera
            self.ExportFreeCameraToStage(tmpStage, defcamName, imgWidth,
                imgHeight)

        tmpStage.GetRootLayer().Save()
        del tmpStage

        # Reopen just the tmp layer, to sublayer in the pose cache without
        # incurring Usd composition cost.
        if self._dataModel.stage:
            from pxr import Sdf
            sdfLayer = Sdf.Layer.FindOrOpen(stagePath)
            sdfLayer.subLayerPaths.append(
                os.path.abspath(
                    self._dataModel.stage.GetRootLayer().realPath))
            sdfLayer.Save()

    def _primSelectionChanged(self):

        # set highlighted paths to renderer
        self.updateSelection()
        self.update()
