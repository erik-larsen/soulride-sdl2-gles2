/*
    srgl.hpp  -- Soul Ride GL: fixed-function OpenGL 1.x subset over GLES2.

    This file is part of The Soul Ride Engine, see http://soulride.com

    The Soul Ride Engine is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.
*/
// Game code includes this header (via ogl.hpp) instead of the system
// GL headers.  Every GL 1.x entry point the game uses is renamed via
// macro to an srgl* function implemented in srgl.cpp on top of the
// GLES2 API subset (desktop OpenGL 2.1 on macOS, which has no native
// GLES2; real GLES2/WebGL1 elsewhere).
//
// Only the API surface Soul Ride actually uses is provided.

#ifndef SRGL_HPP
#define SRGL_HPP


// Basic GL types.  Guarded so this header can coexist with the real
// GL headers inside srgl.cpp.
#if !defined(__gl_h_) && !defined(__gl2_h_) && !defined(GL_ES_VERSION_2_0)
typedef unsigned int	GLenum;
typedef unsigned char	GLboolean;
typedef unsigned int	GLbitfield;
typedef void		GLvoid;
typedef signed char	GLbyte;
typedef short		GLshort;
typedef int		GLint;
typedef unsigned char	GLubyte;
typedef unsigned short	GLushort;
typedef unsigned int	GLuint;
typedef int		GLsizei;
typedef float		GLfloat;
typedef float		GLclampf;
#endif	// GL types

// The GLES2 headers don't define the double types at all.
#if !defined(__gl_h_) && !defined(__GL_H__)
typedef double		GLdouble;
typedef double		GLclampd;
#endif	// GL double types


//
// Constants (standard GL values).
//

// Boolean
#define GL_FALSE			0
#define GL_TRUE				1

// Errors
#define GL_NO_ERROR			0
#define GL_INVALID_ENUM			0x0500
#define GL_INVALID_VALUE		0x0501
#define GL_INVALID_OPERATION		0x0502
#define GL_STACK_OVERFLOW		0x0503
#define GL_STACK_UNDERFLOW		0x0504
#define GL_OUT_OF_MEMORY		0x0505

// Primitive modes
#define GL_POINTS			0x0000
#define GL_LINES			0x0001
#define GL_LINE_LOOP			0x0002
#define GL_LINE_STRIP			0x0003
#define GL_TRIANGLES			0x0004
#define GL_TRIANGLE_STRIP		0x0005
#define GL_TRIANGLE_FAN			0x0006
#define GL_QUADS			0x0007
#define GL_POLYGON			0x0009

// Comparison functions
#define GL_NEVER			0x0200
#define GL_LESS				0x0201
#define GL_EQUAL			0x0202
#define GL_LEQUAL			0x0203
#define GL_GREATER			0x0204
#define GL_NOTEQUAL			0x0205
#define GL_GEQUAL			0x0206
#define GL_ALWAYS			0x0207

// Blend factors
#define GL_ZERO				0
#define GL_ONE				1
#define GL_SRC_COLOR			0x0300
#define GL_ONE_MINUS_SRC_COLOR		0x0301
#define GL_SRC_ALPHA			0x0302
#define GL_ONE_MINUS_SRC_ALPHA		0x0303
#define GL_DST_ALPHA			0x0304
#define GL_ONE_MINUS_DST_ALPHA		0x0305
#define GL_DST_COLOR			0x0306
#define GL_ONE_MINUS_DST_COLOR		0x0307

// Enable caps
#define GL_CULL_FACE			0x0B44
#define GL_FOG				0x0B60
#define GL_DEPTH_TEST			0x0B71
#define GL_ALPHA_TEST			0x0BC0
#define GL_DITHER			0x0BD0
#define GL_BLEND			0x0BE2
#define GL_TEXTURE_2D			0x0DE1
#define GL_TEXTURE_GEN_S		0x0C60
#define GL_TEXTURE_GEN_T		0x0C61

// Fog params
#define GL_EXP				0x0800
#define GL_EXP2				0x0801
#define GL_FOG_DENSITY			0x0B62
#define GL_FOG_START			0x0B63
#define GL_FOG_END			0x0B64
#define GL_FOG_MODE			0x0B65
#define GL_FOG_COLOR			0x0B66

// Face culling / winding
#define GL_CW				0x0900
#define GL_CCW				0x0901
#define GL_FRONT			0x0404
#define GL_BACK				0x0405
#define GL_FRONT_AND_BACK		0x0408

// Matrix modes
#define GL_MODELVIEW			0x1700
#define GL_PROJECTION			0x1701

// Pixel formats / types
#define GL_UNSIGNED_BYTE		0x1401
#define GL_UNSIGNED_SHORT		0x1403
#define GL_UNSIGNED_INT			0x1405
#define GL_FLOAT			0x1406
#define GL_RGB				0x1907
#define GL_RGBA				0x1908
#define GL_RGB5_A1			0x8057
#define GL_RGBA4			0x8056

// PixelStore
#define GL_UNPACK_ALIGNMENT		0x0CF5
#define GL_PACK_ALIGNMENT		0x0D05

// Texture params
#define GL_TEXTURE_MAG_FILTER		0x2800
#define GL_TEXTURE_MIN_FILTER		0x2801
#define GL_TEXTURE_WRAP_S		0x2802
#define GL_TEXTURE_WRAP_T		0x2803
#define GL_NEAREST			0x2600
#define GL_LINEAR			0x2601
#define GL_NEAREST_MIPMAP_NEAREST	0x2700
#define GL_LINEAR_MIPMAP_NEAREST	0x2701
#define GL_NEAREST_MIPMAP_LINEAR	0x2702
#define GL_LINEAR_MIPMAP_LINEAR		0x2703
#define GL_CLAMP			0x2900
#define GL_REPEAT			0x2901
#define GL_CLAMP_TO_EDGE		0x812F

// TexEnv
#define GL_TEXTURE_ENV			0x2300
#define GL_TEXTURE_ENV_MODE		0x2200
#define GL_MODULATE			0x2100
#define GL_DECAL			0x2101
#define GL_REPLACE			0x1E01

// TexGen
#define GL_S				0x2000
#define GL_T				0x2001
#define GL_TEXTURE_GEN_MODE		0x2500
#define GL_OBJECT_PLANE			0x2501
#define GL_OBJECT_LINEAR		0x2401

// Hints
#define GL_PERSPECTIVE_CORRECTION_HINT	0x0C50
#define GL_FOG_HINT			0x0C54
#define GL_DONT_CARE			0x1100
#define GL_FASTEST			0x1101
#define GL_NICEST			0x1102

// Shade model
#define GL_FLAT				0x1D00
#define GL_SMOOTH			0x1D01

// PolygonMode
#define GL_POINT			0x1B00
#define GL_LINE				0x1B01
#define GL_FILL				0x1B02

// Client array state
#define GL_VERTEX_ARRAY			0x8074
#define GL_TEXTURE_COORD_ARRAY		0x8078

// Clear bits
#define GL_DEPTH_BUFFER_BIT		0x00000100
#define GL_COLOR_BUFFER_BIT		0x00004000

// GetString
#define GL_VENDOR			0x1F00
#define GL_RENDERER			0x1F01
#define GL_VERSION			0x1F02
#define GL_EXTENSIONS			0x1F03


//
// Entry points.  Renamed so they never collide with the real GL
// library at link time.  srgl.cpp defines SRGL_NO_RENAME so it can
// call the real GL functions internally.
//

#ifndef SRGL_NO_RENAME

#define glAlphaFunc		srglAlphaFunc
#define glBegin			srglBegin
#define glBindTexture		srglBindTexture
#define glBlendFunc		srglBlendFunc
#define glClear			srglClear
#define glClearColor		srglClearColor
#define glClearDepth		srglClearDepth
#define glColor3f		srglColor3f
#define glColor3fv		srglColor3fv
#define glColor3ub		srglColor3ub
#define glColor3ubv		srglColor3ubv
#define glColor4f		srglColor4f
#define glColor4fv		srglColor4fv
#define glColor4ub		srglColor4ub
#define glCullFace		srglCullFace
#define glDeleteTextures	srglDeleteTextures
#define glDepthFunc		srglDepthFunc
#define glDepthMask		srglDepthMask
#define glDisable		srglDisable
#define glDisableClientState	srglDisableClientState
#define glDrawArrays		srglDrawArrays
#define glDrawBuffer		srglDrawBuffer
#define glDrawElements		srglDrawElements
#define glDrawPixels		srglDrawPixels
#define glEnable		srglEnable
#define glEnableClientState	srglEnableClientState
#define glEnd			srglEnd
#define glFinish		srglFinish
#define glFlush			srglFlush
#define glFogf			srglFogf
#define glFogfv			srglFogfv
#define glFogi			srglFogi
#define glFrontFace		srglFrontFace
#define glFrustum		srglFrustum
#define glGenTextures		srglGenTextures
#define glGetError		srglGetError
#define glGetFloatv		srglGetFloatv
#define glGetIntegerv		srglGetIntegerv
#define glGetString		srglGetString
#define glHint			srglHint
#define glIsEnabled		srglIsEnabled
#define glLoadIdentity		srglLoadIdentity
#define glLoadMatrixf		srglLoadMatrixf
#define glMatrixMode		srglMatrixMode
#define glMultMatrixf		srglMultMatrixf
#define glOrtho			srglOrtho
#define glPixelStoref		srglPixelStoref
#define glPixelStorei		srglPixelStorei
#define glPixelTransferf	srglPixelTransferf
#define glPolygonMode		srglPolygonMode
#define glPolygonOffset		srglPolygonOffset
#define glPopMatrix		srglPopMatrix
#define glPushMatrix		srglPushMatrix
#define glRasterPos2f		srglRasterPos2f
#define glReadPixels		srglReadPixels
#define glRotatef		srglRotatef
#define glScalef		srglScalef
#define glShadeModel		srglShadeModel
#define glTexCoord2f		srglTexCoord2f
#define glTexCoord2fv		srglTexCoord2fv
#define glTexCoord3f		srglTexCoord3f
#define glTexCoord3fv		srglTexCoord3fv
#define glTexCoordPointer	srglTexCoordPointer
#define glTexEnvf		srglTexEnvf
#define glTexEnvi		srglTexEnvi
#define glTexGenfv		srglTexGenfv
#define glTexGeni		srglTexGeni
#define glTexImage2D		srglTexImage2D
#define glTexParameterf		srglTexParameterf
#define glTexParameteri		srglTexParameteri
#define glTexSubImage2D		srglTexSubImage2D
#define glTranslatef		srglTranslatef
#define glVertex2f		srglVertex2f
#define glVertex3f		srglVertex3f
#define glVertex3fv		srglVertex3fv
#define glVertex3i		srglVertex3i
#define glVertex4f		srglVertex4f
#define glVertex4fv		srglVertex4fv
#define glVertexPointer		srglVertexPointer
#define glViewport		srglViewport
#define glLockArraysEXT		srglLockArraysEXT
#define glUnlockArraysEXT	srglUnlockArraysEXT

#endif // SRGL_NO_RENAME

void	srglAlphaFunc(GLenum func, GLclampf ref);
void	srglBegin(GLenum mode);
void	srglBindTexture(GLenum target, GLuint texture);
void	srglBlendFunc(GLenum sfactor, GLenum dfactor);
void	srglClear(GLbitfield mask);
void	srglClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void	srglClearDepth(GLclampd depth);
void	srglColor3f(GLfloat r, GLfloat g, GLfloat b);
void	srglColor3fv(const GLfloat* v);
void	srglColor3ub(GLubyte r, GLubyte g, GLubyte b);
void	srglColor3ubv(const GLubyte* v);
void	srglColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void	srglColor4fv(const GLfloat* v);
void	srglColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void	srglCullFace(GLenum mode);
void	srglDeleteTextures(GLsizei n, const GLuint* textures);
void	srglDepthFunc(GLenum func);
void	srglDepthMask(GLboolean flag);
void	srglDisable(GLenum cap);
void	srglDisableClientState(GLenum array);
void	srglDrawArrays(GLenum mode, GLint first, GLsizei count);
void	srglDrawBuffer(GLenum mode);
void	srglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
void	srglDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid* data);
void	srglEnable(GLenum cap);
void	srglEnableClientState(GLenum array);
void	srglEnd(void);
void	srglFinish(void);
void	srglFlush(void);
void	srglFogf(GLenum pname, GLfloat param);
void	srglFogfv(GLenum pname, const GLfloat* params);
void	srglFogi(GLenum pname, GLint param);
void	srglFrontFace(GLenum mode);
void	srglFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void	srglGenTextures(GLsizei n, GLuint* textures);
GLenum	srglGetError(void);
void	srglGetFloatv(GLenum pname, GLfloat* params);
void	srglGetIntegerv(GLenum pname, GLint* params);
const GLubyte*	srglGetString(GLenum name);
void	srglHint(GLenum target, GLenum mode);
GLboolean	srglIsEnabled(GLenum cap);
void	srglLoadIdentity(void);
void	srglLoadMatrixf(const GLfloat* m);
void	srglMatrixMode(GLenum mode);
void	srglMultMatrixf(const GLfloat* m);
void	srglOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void	srglPixelStoref(GLenum pname, GLfloat param);
void	srglPixelStorei(GLenum pname, GLint param);
void	srglPixelTransferf(GLenum pname, GLfloat param);
void	srglPolygonMode(GLenum face, GLenum mode);
void	srglPolygonOffset(GLfloat factor, GLfloat units);
void	srglPopMatrix(void);
void	srglPushMatrix(void);
void	srglRasterPos2f(GLfloat x, GLfloat y);
void	srglReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid* data);
void	srglRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void	srglScalef(GLfloat x, GLfloat y, GLfloat z);
void	srglShadeModel(GLenum mode);
void	srglTexCoord2f(GLfloat s, GLfloat t);
void	srglTexCoord2fv(const GLfloat* v);
void	srglTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void	srglTexCoord3fv(const GLfloat* v);
void	srglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
void	srglTexEnvf(GLenum target, GLenum pname, GLfloat param);
void	srglTexEnvi(GLenum target, GLenum pname, GLint param);
void	srglTexGenfv(GLenum coord, GLenum pname, const GLfloat* params);
void	srglTexGeni(GLenum coord, GLenum pname, GLint param);
void	srglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels);
void	srglTexParameterf(GLenum target, GLenum pname, GLfloat param);
void	srglTexParameteri(GLenum target, GLenum pname, GLint param);
void	srglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels);
void	srglTranslatef(GLfloat x, GLfloat y, GLfloat z);
void	srglVertex2f(GLfloat x, GLfloat y);
void	srglVertex3f(GLfloat x, GLfloat y, GLfloat z);
void	srglVertex3fv(const GLfloat* v);
void	srglVertex3i(GLint x, GLint y, GLint z);
void	srglVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void	srglVertex4fv(const GLfloat* v);
void	srglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
void	srglViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void	srglLockArraysEXT(GLint first, GLsizei count);
void	srglUnlockArraysEXT(void);

// Call after creating the GL context; compiles the shader and creates
// buffers.  Returns false on failure.  Call srglShutdown before
// destroying the context.
bool	srglInit();
void	srglShutdown();


#endif // SRGL_HPP
