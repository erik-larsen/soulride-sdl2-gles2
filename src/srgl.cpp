/*
    srgl.cpp  -- Soul Ride GL: fixed-function OpenGL 1.x subset over GLES2.

    This file is part of The Soul Ride Engine, see http://soulride.com

    The Soul Ride Engine is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.
*/
// Implements the GL 1.x subset Soul Ride uses (immediate mode, matrix
// stacks, object-linear texgen, linear fog, alpha test, MODULATE /
// REPLACE texenv) on the GLES2 API subset, with a single "über"
// shader.  Vertex data is streamed through VBOs so the same code
// works on WebGL1, which forbids client-side arrays.
//
// On macOS there is no native GLES2, so we run on desktop OpenGL 2.1,
// which is a superset of what we need.

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GLES2/gl2.h>
#endif

#define SRGL_NO_RENAME
#include "srgl.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


//
// 4x4 column-major matrices, OpenGL convention.
//

struct Mat4 {
	float	m[16];
};


static void	mat_identity(Mat4* a)
{
	memset(a->m, 0, sizeof(a->m));
	a->m[0] = a->m[5] = a->m[10] = a->m[15] = 1;
}


static void	mat_multiply(Mat4* out, const Mat4* a, const Mat4* b)
// out = a * b.  out may not alias a or b.
{
	int	i, j, k;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			float	s = 0;
			for (k = 0; k < 4; k++) {
				s += a->m[k * 4 + j] * b->m[i * 4 + k];
			}
			out->m[i * 4 + j] = s;
		}
	}
}


static void	mat_mult_in_place(Mat4* a, const Mat4* b)
// a = a * b.
{
	Mat4	tmp;
	mat_multiply(&tmp, a, b);
	*a = tmp;
}


//
// Shim state.
//

const int	MATRIX_STACK_DEPTH = 48;
const int	IMM_MAX_VERTS = 65536;
const int	VERT_FLOATS = 10;	// x y z w  u v  r g b a

struct SrglState {
	bool	Inited;

	// Shader program.
	GLuint	Program;
	GLint	LocMV, LocProj;
	GLint	LocTexture, LocTexEnv;
	GLint	LocAlphaTest, LocAlphaRef;
	GLint	LocFog, LocFogColor, LocFogStart, LocFogEnd;
	GLint	LocTexGen, LocPlaneS, LocPlaneT;
	GLint	LocSampler;

	// Stream buffers.
	GLuint	VBO, IBO;

	// Matrix stacks.
	Mat4	MVStack[MATRIX_STACK_DEPTH];
	int	MVTop;
	Mat4	ProjStack[8];
	int	ProjTop;
	GLenum	MatrixMode;

	// Current per-vertex attributes.
	float	CurColor[4];
	float	CurUV[2];

	// Immediate-mode accumulation.
	int	ImmMode;	// -1 == not inside Begin/End
	int	ImmCount;
	float*	ImmData;

	// Fixed-function toggles.
	bool	EnTexture2D;
	bool	EnAlphaTest;
	bool	EnFog;
	bool	EnTexGenS, EnTexGenT;

	float	AlphaRef;
	int	TexEnvMode;	// GL_MODULATE or GL_REPLACE
	float	FogStart, FogEnd;
	float	FogColor[4];
	float	PlaneS[4], PlaneT[4];

	// Client vertex arrays.
	bool	VertexArrayOn, TexCoordArrayOn;
	const float*	VertexPtr;
	int	VertexSize;
	const float*	TexCoordPtr;
	int	TexCoordSize;

	GLuint	BoundTexture;
	GLuint	WhiteTexture;	// 1x1 white, bound when texturing is off

	bool	Wireframe;	// glPolygonMode(..., GL_LINE) emulation

	// Scratch for index conversion / vertex packing.
	unsigned short*	IndexScratch;
	int	IndexScratchSize;
	float*	VertScratch;
	int	VertScratchFloats;
};

static SrglState	S;

// Attribute locations (bound before link).
enum { ATTRIB_POS = 0, ATTRIB_UV = 1, ATTRIB_COLOR = 2 };


static const char*	VERTEX_SHADER =
	"uniform mat4 u_mv;\n"
	"uniform mat4 u_proj;\n"
	"uniform int u_texgen;\n"
	"uniform vec4 u_planeS;\n"
	"uniform vec4 u_planeT;\n"
	"attribute vec4 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"attribute vec4 a_color;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_uv;\n"
	"varying float v_eyedist;\n"
	"void main() {\n"
	"	vec4 eye = u_mv * a_pos;\n"
	"	gl_Position = u_proj * eye;\n"
	"	v_color = a_color;\n"
	"	if (u_texgen != 0) {\n"
	"		v_uv = vec2(dot(u_planeS, a_pos), dot(u_planeT, a_pos));\n"
	"	} else {\n"
	"		v_uv = a_uv;\n"
	"	}\n"
	"	v_eyedist = -eye.z;\n"
	"}\n";

static const char*	FRAGMENT_SHADER =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform int u_texture;\n"
	"uniform int u_texenv;\n"		// 0 == MODULATE, 1 == REPLACE
	"uniform int u_alphatest;\n"
	"uniform float u_alpharef;\n"
	"uniform int u_fog;\n"
	"uniform vec3 u_fogcolor;\n"
	"uniform float u_fogstart;\n"
	"uniform float u_fogend;\n"
	"uniform sampler2D u_sampler;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_uv;\n"
	"varying float v_eyedist;\n"
	"void main() {\n"
	"	vec4 c = v_color;\n"
	"	if (u_texture != 0) {\n"
	"		vec4 t = texture2D(u_sampler, v_uv);\n"
	"		if (u_texenv == 1) {\n"
	"			c = t;\n"
	"		} else {\n"
	"			c = c * t;\n"
	"		}\n"
	"	}\n"
	"	if (u_alphatest != 0 && c.a < u_alpharef) discard;\n"
	"	if (u_fog != 0) {\n"
	"		float f = clamp((u_fogend - v_eyedist) / (u_fogend - u_fogstart), 0.0, 1.0);\n"
	"		c.rgb = mix(u_fogcolor, c.rgb, f);\n"
	"	}\n"
	"	gl_FragColor = c;\n"
	"}\n";


static GLuint	compile_shader(GLenum type, const char* source)
{
	GLuint	sh = glCreateShader(type);
	glShaderSource(sh, 1, &source, NULL);
	glCompileShader(sh);

	GLint	ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char	log[1024];
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		fprintf(stderr, "srgl: shader compile failed:\n%s\n", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}


bool	srglInit()
// Compile the shader and create stream buffers.  Requires a current
// GL context.
{
	if (S.Inited) return true;

	// One-time allocations (survive re-init).
	if (S.ImmData == NULL) {
		S.ImmData = new float[IMM_MAX_VERTS * VERT_FLOATS];

		// First init ever: set up sane fixed-function defaults.
		mat_identity(&S.MVStack[0]);
		mat_identity(&S.ProjStack[0]);
		S.MVTop = 0;
		S.ProjTop = 0;
		S.MatrixMode = GL_MODELVIEW;
	}

	GLuint	vs = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
	GLuint	fs = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
	if (vs == 0 || fs == 0) return false;

	S.Program = glCreateProgram();
	glAttachShader(S.Program, vs);
	glAttachShader(S.Program, fs);
	glBindAttribLocation(S.Program, ATTRIB_POS, "a_pos");
	glBindAttribLocation(S.Program, ATTRIB_UV, "a_uv");
	glBindAttribLocation(S.Program, ATTRIB_COLOR, "a_color");
	glLinkProgram(S.Program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint	ok = 0;
	glGetProgramiv(S.Program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char	log[1024];
		glGetProgramInfoLog(S.Program, sizeof(log), NULL, log);
		fprintf(stderr, "srgl: program link failed:\n%s\n", log);
		return false;
	}

	S.LocMV = glGetUniformLocation(S.Program, "u_mv");
	S.LocProj = glGetUniformLocation(S.Program, "u_proj");
	S.LocTexture = glGetUniformLocation(S.Program, "u_texture");
	S.LocTexEnv = glGetUniformLocation(S.Program, "u_texenv");
	S.LocAlphaTest = glGetUniformLocation(S.Program, "u_alphatest");
	S.LocAlphaRef = glGetUniformLocation(S.Program, "u_alpharef");
	S.LocFog = glGetUniformLocation(S.Program, "u_fog");
	S.LocFogColor = glGetUniformLocation(S.Program, "u_fogcolor");
	S.LocFogStart = glGetUniformLocation(S.Program, "u_fogstart");
	S.LocFogEnd = glGetUniformLocation(S.Program, "u_fogend");
	S.LocTexGen = glGetUniformLocation(S.Program, "u_texgen");
	S.LocPlaneS = glGetUniformLocation(S.Program, "u_planeS");
	S.LocPlaneT = glGetUniformLocation(S.Program, "u_planeT");
	S.LocSampler = glGetUniformLocation(S.Program, "u_sampler");

	glGenBuffers(1, &S.VBO);
	glGenBuffers(1, &S.IBO);

	glUseProgram(S.Program);
	glUniform1i(S.LocSampler, 0);
	glActiveTexture(GL_TEXTURE0);

	// A 1x1 white texture stays bound whenever texturing is off, so
	// the sampler never reads an incomplete texture.
	glGenTextures(1, &S.WhiteTexture);
	glBindTexture(GL_TEXTURE_2D, S.WhiteTexture);
	static const unsigned char	white[4] = { 255, 255, 255, 255 };
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Reset the fixed-function state to GL defaults, but keep any
	// matrix state built up before init.
	S.CurColor[0] = S.CurColor[1] = S.CurColor[2] = S.CurColor[3] = 1;
	S.CurUV[0] = S.CurUV[1] = 0;
	S.ImmMode = -1;
	S.TexEnvMode = GL_MODULATE;
	S.AlphaRef = 0;
	S.Inited = true;

	return true;
}


void	srglShutdown()
{
	if (!S.Inited) return;

	glDeleteProgram(S.Program);
	glDeleteBuffers(1, &S.VBO);
	glDeleteBuffers(1, &S.IBO);
	glDeleteTextures(1, &S.WhiteTexture);
	S.Program = 0;
	S.VBO = S.IBO = 0;
	S.WhiteTexture = 0;
	S.Inited = false;
}


//
// Matrix stack.
//

static Mat4*	current_matrix()
{
	if (S.MatrixMode == GL_PROJECTION) return &S.ProjStack[S.ProjTop];
	return &S.MVStack[S.MVTop];
}


void	srglMatrixMode(GLenum mode)
{
	S.MatrixMode = mode;
}


void	srglLoadIdentity()
{
	mat_identity(current_matrix());
}


void	srglLoadMatrixf(const GLfloat* m)
{
	memcpy(current_matrix()->m, m, 16 * sizeof(float));
}


void	srglMultMatrixf(const GLfloat* m)
{
	Mat4	b;
	memcpy(b.m, m, 16 * sizeof(float));
	mat_mult_in_place(current_matrix(), &b);
}


void	srglPushMatrix()
{
	if (S.MatrixMode == GL_PROJECTION) {
		if (S.ProjTop + 1 < (int) (sizeof(S.ProjStack) / sizeof(S.ProjStack[0]))) {
			S.ProjStack[S.ProjTop + 1] = S.ProjStack[S.ProjTop];
			S.ProjTop++;
		}
	} else {
		if (S.MVTop + 1 < MATRIX_STACK_DEPTH) {
			S.MVStack[S.MVTop + 1] = S.MVStack[S.MVTop];
			S.MVTop++;
		}
	}
}


void	srglPopMatrix()
{
	if (S.MatrixMode == GL_PROJECTION) {
		if (S.ProjTop > 0) S.ProjTop--;
	} else {
		if (S.MVTop > 0) S.MVTop--;
	}
}


void	srglTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	Mat4	t;
	mat_identity(&t);
	t.m[12] = x; t.m[13] = y; t.m[14] = z;
	mat_mult_in_place(current_matrix(), &t);
}


void	srglScalef(GLfloat x, GLfloat y, GLfloat z)
{
	Mat4	t;
	mat_identity(&t);
	t.m[0] = x; t.m[5] = y; t.m[10] = z;
	mat_mult_in_place(current_matrix(), &t);
}


void	srglRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	float	len = sqrtf(x * x + y * y + z * z);
	if (len < 1e-9f) return;
	x /= len; y /= len; z /= len;

	float	rad = angle * (float) (3.14159265358979323846 / 180.0);
	float	c = cosf(rad);
	float	s = sinf(rad);
	float	ic = 1 - c;

	Mat4	r;
	r.m[0] = x * x * ic + c;
	r.m[1] = y * x * ic + z * s;
	r.m[2] = x * z * ic - y * s;
	r.m[3] = 0;
	r.m[4] = x * y * ic - z * s;
	r.m[5] = y * y * ic + c;
	r.m[6] = y * z * ic + x * s;
	r.m[7] = 0;
	r.m[8] = x * z * ic + y * s;
	r.m[9] = y * z * ic - x * s;
	r.m[10] = z * z * ic + c;
	r.m[11] = 0;
	r.m[12] = r.m[13] = r.m[14] = 0;
	r.m[15] = 1;
	mat_mult_in_place(current_matrix(), &r);
}


void	srglFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	Mat4	p;
	memset(p.m, 0, sizeof(p.m));
	p.m[0] = (float) (2 * n / (r - l));
	p.m[5] = (float) (2 * n / (t - b));
	p.m[8] = (float) ((r + l) / (r - l));
	p.m[9] = (float) ((t + b) / (t - b));
	p.m[10] = (float) (-(f + n) / (f - n));
	p.m[11] = -1;
	p.m[14] = (float) (-2 * f * n / (f - n));
	mat_mult_in_place(current_matrix(), &p);
}


void	srglOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	Mat4	p;
	memset(p.m, 0, sizeof(p.m));
	p.m[0] = (float) (2 / (r - l));
	p.m[5] = (float) (2 / (t - b));
	p.m[10] = (float) (-2 / (f - n));
	p.m[12] = (float) (-(r + l) / (r - l));
	p.m[13] = (float) (-(t + b) / (t - b));
	p.m[14] = (float) (-(f + n) / (f - n));
	p.m[15] = 1;
	mat_mult_in_place(current_matrix(), &p);
}


//
// Draw submission.
//

static void	setup_draw_state()
// Bind the program and refresh all uniforms.  Called before every
// draw; cheap enough at this game's draw-call counts.
{
	glUseProgram(S.Program);

	glUniformMatrix4fv(S.LocMV, 1, GL_FALSE, S.MVStack[S.MVTop].m);
	glUniformMatrix4fv(S.LocProj, 1, GL_FALSE, S.ProjStack[S.ProjTop].m);

	bool	texturing = S.EnTexture2D && S.BoundTexture != 0;
	glBindTexture(GL_TEXTURE_2D, texturing ? S.BoundTexture : S.WhiteTexture);
	glUniform1i(S.LocTexture, texturing ? 1 : 0);
	glUniform1i(S.LocTexEnv, (S.TexEnvMode == GL_REPLACE) ? 1 : 0);

	glUniform1i(S.LocAlphaTest, S.EnAlphaTest ? 1 : 0);
	glUniform1f(S.LocAlphaRef, S.AlphaRef);

	glUniform1i(S.LocFog, S.EnFog ? 1 : 0);
	glUniform3f(S.LocFogColor, S.FogColor[0], S.FogColor[1], S.FogColor[2]);
	glUniform1f(S.LocFogStart, S.FogStart);
	glUniform1f(S.LocFogEnd, S.FogEnd);

	// The game always enables/disables GEN_S and GEN_T together.
	glUniform1i(S.LocTexGen, (S.EnTexGenS && S.EnTexGenT) ? 1 : 0);
	glUniform4fv(S.LocPlaneS, 1, S.PlaneS);
	glUniform4fv(S.LocPlaneT, 1, S.PlaneT);
}


static void	ensure_index_scratch(int count)
{
	if (S.IndexScratchSize < count) {
		delete [] S.IndexScratch;
		S.IndexScratch = new unsigned short[count];
		S.IndexScratchSize = count;
	}
}


static void	ensure_vert_scratch(int floats)
{
	if (S.VertScratchFloats < floats) {
		delete [] S.VertScratch;
		S.VertScratch = new float[floats];
		S.VertScratchFloats = floats;
	}
}


//
// Immediate mode.
//

void	srglBegin(GLenum mode)
{
	S.ImmMode = (int) mode;
	S.ImmCount = 0;
}


static void	imm_vertex(float x, float y, float z, float w)
{
	if (S.ImmMode < 0 || S.ImmCount >= IMM_MAX_VERTS || S.ImmData == NULL) return;

	float*	v = S.ImmData + S.ImmCount * VERT_FLOATS;
	v[0] = x; v[1] = y; v[2] = z; v[3] = w;
	v[4] = S.CurUV[0]; v[5] = S.CurUV[1];
	v[6] = S.CurColor[0]; v[7] = S.CurColor[1];
	v[8] = S.CurColor[2]; v[9] = S.CurColor[3];
	S.ImmCount++;
}


void	srglEnd()
{
	int	mode = S.ImmMode;
	int	count = S.ImmCount;
	S.ImmMode = -1;

	if (!S.Inited || count == 0) return;

	setup_draw_state();

	glBindBuffer(GL_ARRAY_BUFFER, S.VBO);
	glBufferData(GL_ARRAY_BUFFER, count * VERT_FLOATS * sizeof(float), S.ImmData, GL_STREAM_DRAW);

	const int	stride = VERT_FLOATS * sizeof(float);
	glVertexAttribPointer(ATTRIB_POS, 4, GL_FLOAT, GL_FALSE, stride, (const void*) 0);
	glVertexAttribPointer(ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, stride, (const void*) (4 * sizeof(float)));
	glVertexAttribPointer(ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, stride, (const void*) (6 * sizeof(float)));
	glEnableVertexAttribArray(ATTRIB_POS);
	glEnableVertexAttribArray(ATTRIB_UV);
	glEnableVertexAttribArray(ATTRIB_COLOR);

	if (mode == GL_QUADS) {
		if (S.Wireframe) {
			// Quad edges as lines.
			int	quads = count / 4;
			ensure_index_scratch(quads * 8);
			int	q;
			for (q = 0; q < quads; q++) {
				unsigned short*	i = S.IndexScratch + q * 8;
				i[0] = (unsigned short) (q * 4 + 0);
				i[1] = (unsigned short) (q * 4 + 1);
				i[2] = (unsigned short) (q * 4 + 1);
				i[3] = (unsigned short) (q * 4 + 2);
				i[4] = (unsigned short) (q * 4 + 2);
				i[5] = (unsigned short) (q * 4 + 3);
				i[6] = (unsigned short) (q * 4 + 3);
				i[7] = (unsigned short) (q * 4 + 0);
			}
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.IBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, quads * 8 * sizeof(unsigned short), S.IndexScratch, GL_STREAM_DRAW);
			glDrawElements(GL_LINES, quads * 8, GL_UNSIGNED_SHORT, (const void*) 0);
			return;
		}

		// GLES2 has no quads; emit two triangles per quad.
		int	quads = count / 4;
		ensure_index_scratch(quads * 6);
		int	q;
		for (q = 0; q < quads; q++) {
			unsigned short*	i = S.IndexScratch + q * 6;
			i[0] = (unsigned short) (q * 4 + 0);
			i[1] = (unsigned short) (q * 4 + 1);
			i[2] = (unsigned short) (q * 4 + 2);
			i[3] = (unsigned short) (q * 4 + 0);
			i[4] = (unsigned short) (q * 4 + 2);
			i[5] = (unsigned short) (q * 4 + 3);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.IBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, quads * 6 * sizeof(unsigned short), S.IndexScratch, GL_STREAM_DRAW);
		glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_SHORT, (const void*) 0);
	} else {
		GLenum	glmode = (GLenum) mode;
		if (mode == GL_POLYGON) glmode = GL_TRIANGLE_FAN;

		if (S.Wireframe) {
			// Debug view: approximate filled primitives with
			// line versions (interior strip/fan edges shown).
			if (mode == GL_TRIANGLES) {
				int	tris = count / 3;
				ensure_index_scratch(tris * 6);
				int	t;
				for (t = 0; t < tris; t++) {
					unsigned short*	i = S.IndexScratch + t * 6;
					i[0] = (unsigned short) (t * 3 + 0);
					i[1] = (unsigned short) (t * 3 + 1);
					i[2] = (unsigned short) (t * 3 + 1);
					i[3] = (unsigned short) (t * 3 + 2);
					i[4] = (unsigned short) (t * 3 + 2);
					i[5] = (unsigned short) (t * 3 + 0);
				}
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.IBO);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, tris * 6 * sizeof(unsigned short), S.IndexScratch, GL_STREAM_DRAW);
				glDrawElements(GL_LINES, tris * 6, GL_UNSIGNED_SHORT, (const void*) 0);
				return;
			}
			if (glmode == GL_TRIANGLE_FAN || glmode == GL_TRIANGLE_STRIP) {
				glmode = (glmode == GL_TRIANGLE_FAN) ? GL_LINE_LOOP : GL_LINE_STRIP;
			}
		}

		glDrawArrays(glmode, 0, count);
	}
}


void	srglVertex2f(GLfloat x, GLfloat y)		{ imm_vertex(x, y, 0, 1); }
void	srglVertex3f(GLfloat x, GLfloat y, GLfloat z)	{ imm_vertex(x, y, z, 1); }
void	srglVertex3fv(const GLfloat* v)			{ imm_vertex(v[0], v[1], v[2], 1); }
void	srglVertex3i(GLint x, GLint y, GLint z)		{ imm_vertex((float) x, (float) y, (float) z, 1); }
void	srglVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)	{ imm_vertex(x, y, z, w); }
void	srglVertex4fv(const GLfloat* v)			{ imm_vertex(v[0], v[1], v[2], v[3]); }

void	srglColor3f(GLfloat r, GLfloat g, GLfloat b)	{ S.CurColor[0] = r; S.CurColor[1] = g; S.CurColor[2] = b; S.CurColor[3] = 1; }
void	srglColor3fv(const GLfloat* v)			{ srglColor3f(v[0], v[1], v[2]); }
void	srglColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)	{ S.CurColor[0] = r; S.CurColor[1] = g; S.CurColor[2] = b; S.CurColor[3] = a; }
void	srglColor4fv(const GLfloat* v)			{ srglColor4f(v[0], v[1], v[2], v[3]); }
void	srglColor3ub(GLubyte r, GLubyte g, GLubyte b)	{ srglColor3f(r / 255.0f, g / 255.0f, b / 255.0f); }
void	srglColor3ubv(const GLubyte* v)			{ srglColor3ub(v[0], v[1], v[2]); }
void	srglColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)	{ srglColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }

void	srglTexCoord2f(GLfloat s, GLfloat t)		{ S.CurUV[0] = s; S.CurUV[1] = t; }
void	srglTexCoord2fv(const GLfloat* v)		{ S.CurUV[0] = v[0]; S.CurUV[1] = v[1]; }
void	srglTexCoord3f(GLfloat s, GLfloat t, GLfloat r)	{ S.CurUV[0] = s; S.CurUV[1] = t; }
void	srglTexCoord3fv(const GLfloat* v)		{ S.CurUV[0] = v[0]; S.CurUV[1] = v[1]; }


//
// Client vertex arrays (game uses tightly packed float arrays only).
//

void	srglEnableClientState(GLenum array)
{
	if (array == GL_VERTEX_ARRAY) S.VertexArrayOn = true;
	else if (array == GL_TEXTURE_COORD_ARRAY) S.TexCoordArrayOn = true;
}


void	srglDisableClientState(GLenum array)
{
	if (array == GL_VERTEX_ARRAY) S.VertexArrayOn = false;
	else if (array == GL_TEXTURE_COORD_ARRAY) S.TexCoordArrayOn = false;
}


void	srglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)
{
	// Game always uses tightly packed GL_FLOAT arrays.
	S.VertexPtr = (const float*) ptr;
	S.VertexSize = size;
}


void	srglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)
{
	S.TexCoordPtr = (const float*) ptr;
	S.TexCoordSize = size;
}


static void	array_draw_setup(int vert_count)
// Upload [0, vert_count) vertices from the client arrays into the
// stream VBO and point the attributes at them.
{
	setup_draw_state();

	int	pos_floats = vert_count * S.VertexSize;
	int	uv_floats = S.TexCoordArrayOn ? vert_count * S.TexCoordSize : 0;

	glBindBuffer(GL_ARRAY_BUFFER, S.VBO);
	glBufferData(GL_ARRAY_BUFFER, (pos_floats + uv_floats) * sizeof(float), NULL, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, pos_floats * sizeof(float), S.VertexPtr);
	if (uv_floats) {
		glBufferSubData(GL_ARRAY_BUFFER, pos_floats * sizeof(float), uv_floats * sizeof(float), S.TexCoordPtr);
	}

	glVertexAttribPointer(ATTRIB_POS, S.VertexSize, GL_FLOAT, GL_FALSE, 0, (const void*) 0);
	glEnableVertexAttribArray(ATTRIB_POS);

	if (S.TexCoordArrayOn) {
		glVertexAttribPointer(ATTRIB_UV, S.TexCoordSize, GL_FLOAT, GL_FALSE, 0,
				      (const void*) (size_t) (pos_floats * sizeof(float)));
		glEnableVertexAttribArray(ATTRIB_UV);
	} else {
		glDisableVertexAttribArray(ATTRIB_UV);
		glVertexAttrib2fv(ATTRIB_UV, S.CurUV);
	}

	// Color is constant across array draws (set via glColor*).
	glDisableVertexAttribArray(ATTRIB_COLOR);
	glVertexAttrib4fv(ATTRIB_COLOR, S.CurColor);
}


void	srglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (!S.Inited || !S.VertexArrayOn || count <= 0) return;

	if (S.Wireframe) {
		if (mode == GL_TRIANGLE_FAN) mode = GL_LINE_LOOP;
		else if (mode == GL_TRIANGLE_STRIP) mode = GL_LINE_STRIP;
	}

	array_draw_setup(first + count);
	glDrawArrays(mode, first, count);
}


void	srglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
	if (!S.Inited || !S.VertexArrayOn || count <= 0) return;

	// Find the vertex range in use.
	int	max_index = 0;
	int	i;
	if (type == GL_UNSIGNED_INT) {
		const unsigned int*	idx = (const unsigned int*) indices;
		for (i = 0; i < count; i++) {
			if ((int) idx[i] > max_index) max_index = idx[i];
		}
	} else {	// GL_UNSIGNED_SHORT
		const unsigned short*	idx = (const unsigned short*) indices;
		for (i = 0; i < count; i++) {
			if ((int) idx[i] > max_index) max_index = idx[i];
		}
	}

	array_draw_setup(max_index + 1);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.IBO);

	if (S.Wireframe && mode == GL_TRIANGLES && max_index < 65536) {
		// Debug view: triangle edges as lines.
		int	tris = count / 3;
		ensure_index_scratch(tris * 6);
		for (i = 0; i < tris; i++) {
			unsigned int	a, b, c;
			if (type == GL_UNSIGNED_INT) {
				const unsigned int*	idx = (const unsigned int*) indices;
				a = idx[i * 3]; b = idx[i * 3 + 1]; c = idx[i * 3 + 2];
			} else {
				const unsigned short*	idx = (const unsigned short*) indices;
				a = idx[i * 3]; b = idx[i * 3 + 1]; c = idx[i * 3 + 2];
			}
			unsigned short*	o = S.IndexScratch + i * 6;
			o[0] = (unsigned short) a; o[1] = (unsigned short) b;
			o[2] = (unsigned short) b; o[3] = (unsigned short) c;
			o[4] = (unsigned short) c; o[5] = (unsigned short) a;
		}
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, tris * 6 * sizeof(unsigned short), S.IndexScratch, GL_STREAM_DRAW);
		glDrawElements(GL_LINES, tris * 6, GL_UNSIGNED_SHORT, (const void*) 0);
		return;
	}

	if (S.Wireframe && (mode == GL_TRIANGLE_FAN || mode == GL_TRIANGLE_STRIP)) {
		mode = (mode == GL_TRIANGLE_FAN) ? GL_LINE_LOOP : GL_LINE_STRIP;
	}

	if (type == GL_UNSIGNED_INT && max_index < 65536) {
		// Convert to 16-bit indices; GLES2/WebGL1 can't count on
		// 32-bit index support.
		const unsigned int*	idx = (const unsigned int*) indices;
		ensure_index_scratch(count);
		for (i = 0; i < count; i++) {
			S.IndexScratch[i] = (unsigned short) idx[i];
		}
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(unsigned short), S.IndexScratch, GL_STREAM_DRAW);
		glDrawElements(mode, count, GL_UNSIGNED_SHORT, (const void*) 0);
	} else {
		int	index_size = (type == GL_UNSIGNED_INT) ? 4 : 2;
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * index_size, indices, GL_STREAM_DRAW);
		glDrawElements(mode, count, type, (const void*) 0);
	}
}


//
// Fixed-function state.
//

void	srglEnable(GLenum cap)
{
	switch (cap) {
	case GL_TEXTURE_2D:	S.EnTexture2D = true; break;
	case GL_ALPHA_TEST:	S.EnAlphaTest = true; break;
	case GL_FOG:		S.EnFog = true; break;
	case GL_TEXTURE_GEN_S:	S.EnTexGenS = true; break;
	case GL_TEXTURE_GEN_T:	S.EnTexGenT = true; break;
	case GL_BLEND:
	case GL_DEPTH_TEST:
	case GL_CULL_FACE:
	case GL_DITHER:
		glEnable(cap);
		break;
	default:
		break;
	}
}


void	srglDisable(GLenum cap)
{
	switch (cap) {
	case GL_TEXTURE_2D:	S.EnTexture2D = false; break;
	case GL_ALPHA_TEST:	S.EnAlphaTest = false; break;
	case GL_FOG:		S.EnFog = false; break;
	case GL_TEXTURE_GEN_S:	S.EnTexGenS = false; break;
	case GL_TEXTURE_GEN_T:	S.EnTexGenT = false; break;
	case GL_BLEND:
	case GL_DEPTH_TEST:
	case GL_CULL_FACE:
	case GL_DITHER:
		glDisable(cap);
		break;
	default:
		break;
	}
}


GLboolean	srglIsEnabled(GLenum cap)
{
	switch (cap) {
	case GL_TEXTURE_2D:	return S.EnTexture2D;
	case GL_ALPHA_TEST:	return S.EnAlphaTest;
	case GL_FOG:		return S.EnFog;
	case GL_TEXTURE_GEN_S:	return S.EnTexGenS;
	case GL_TEXTURE_GEN_T:	return S.EnTexGenT;
	default:		return glIsEnabled(cap);
	}
}


void	srglAlphaFunc(GLenum func, GLclampf ref)
{
	// Game only uses GL_GEQUAL.
	S.AlphaRef = ref;
}


void	srglFogf(GLenum pname, GLfloat param)
{
	if (pname == GL_FOG_START) S.FogStart = param;
	else if (pname == GL_FOG_END) S.FogEnd = param;
}


void	srglFogfv(GLenum pname, const GLfloat* params)
{
	if (pname == GL_FOG_COLOR) {
		memcpy(S.FogColor, params, 4 * sizeof(float));
	} else {
		srglFogf(pname, params[0]);
	}
}


void	srglFogi(GLenum pname, GLint param)
{
	// GL_FOG_MODE: only GL_LINEAR is supported (all the game uses).
}


void	srglTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	srglTexEnvi(target, pname, (GLint) param);
}


void	srglTexEnvi(GLenum target, GLenum pname, GLint param)
{
	if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) {
		S.TexEnvMode = param;
	}
	// GL_TEXTURE_FILTER_CONTROL_EXT (LOD bias): unsupported, ignored.
}


void	srglTexGeni(GLenum coord, GLenum pname, GLint param)
{
	// Only GL_OBJECT_LINEAR is used.
}


void	srglTexGenfv(GLenum coord, GLenum pname, const GLfloat* params)
{
	if (pname == GL_OBJECT_PLANE) {
		if (coord == GL_S) memcpy(S.PlaneS, params, 4 * sizeof(float));
		else if (coord == GL_T) memcpy(S.PlaneT, params, 4 * sizeof(float));
	}
}


//
// Textures.
//

void	srglBindTexture(GLenum target, GLuint texture)
{
	S.BoundTexture = texture;
	glBindTexture(target, texture);
}


void	srglGenTextures(GLsizei n, GLuint* textures)
{
	glGenTextures(n, textures);
}


void	srglDeleteTextures(GLsizei n, const GLuint* textures)
{
	glDeleteTextures(n, textures);
}


void	srglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels)
{
	// The game always submits GL_RGBA / GL_UNSIGNED_BYTE data; the
	// internalformat only picks the storage layout.  GLES2 requires
	// internalformat == format, so everything becomes plain RGBA.
	// For GL_RGB requests, force the alpha channel opaque so the
	// alpha lane can't leak into blending or alpha test.
	if (internalformat == GL_RGB && pixels != NULL) {
		int	texels = width * height;
		ensure_vert_scratch((texels + 3) / 4 * 4);	// reuse float scratch as byte storage
		unsigned char*	fixed = (unsigned char*) S.VertScratch;
		memcpy(fixed, pixels, texels * 4);
		int	i;
		for (i = 0; i < texels; i++) {
			fixed[i * 4 + 3] = 255;
		}
		glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, fixed);
		return;
	}

	glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}


void	srglTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels)
{
	glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}


static GLint	fix_tex_param(GLenum pname, GLint param)
{
	// GL_CLAMP doesn't exist in GLES2; the game means clamp-to-edge.
	if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param == GL_CLAMP) {
		return GL_CLAMP_TO_EDGE;
	}
	return param;
}


void	srglTexParameteri(GLenum target, GLenum pname, GLint param)
{
	glTexParameteri(target, pname, fix_tex_param(pname, param));
}


void	srglTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	// The game passes enum values through the float entry point;
	// route them as ints.
	glTexParameteri(target, pname, fix_tex_param(pname, (GLint) param));
}


//
// Simple passthroughs.
//

void	srglBlendFunc(GLenum sfactor, GLenum dfactor)	{ glBlendFunc(sfactor, dfactor); }
void	srglClear(GLbitfield mask)			{ glClear(mask); }
void	srglClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)	{ glClearColor(r, g, b, a); }
void	srglCullFace(GLenum mode)			{ glCullFace(mode); }
void	srglDepthFunc(GLenum func)			{ glDepthFunc(func); }
void	srglDepthMask(GLboolean flag)			{ glDepthMask(flag); }
void	srglFinish()					{ glFinish(); }
void	srglFlush()					{ glFlush(); }
void	srglFrontFace(GLenum mode)			{ glFrontFace(mode); }
GLenum	srglGetError()					{ return glGetError(); }
const GLubyte*	srglGetString(GLenum name)		{ return glGetString(name); }
void	srglPolygonOffset(GLfloat factor, GLfloat units)	{ glPolygonOffset(factor, units); }
void	srglReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid* data)	{ glReadPixels(x, y, w, h, format, type, data); }
void	srglViewport(GLint x, GLint y, GLsizei w, GLsizei h)	{ glViewport(x, y, w, h); }


void	srglClearDepth(GLclampd depth)
{
#ifdef __APPLE__
	glClearDepth(depth);
#else
	glClearDepthf((GLclampf) depth);
#endif
}


void	srglPixelStorei(GLenum pname, GLint param)
{
	if (pname == GL_UNPACK_ALIGNMENT || pname == GL_PACK_ALIGNMENT) {
		glPixelStorei(pname, param);
	}
}


void	srglPixelStoref(GLenum pname, GLfloat param)
{
	srglPixelStorei(pname, (GLint) param);
}


void	srglGetFloatv(GLenum pname, GLfloat* params)
{
	// Not used by the game.
	if (params) *params = 0;
}


void	srglGetIntegerv(GLenum pname, GLint* params)
{
	glGetIntegerv(pname, params);
}


//
// No-ops (features with no GLES2 equivalent, none load-bearing).
//

void	srglDrawBuffer(GLenum mode)			{}
void	srglShadeModel(GLenum mode)			{}	// only GL_SMOOTH is used
void	srglHint(GLenum target, GLenum mode)		{}
void	srglPixelTransferf(GLenum pname, GLfloat param)	{}
void	srglPolygonMode(GLenum face, GLenum mode)
{
	// Emulated: draw paths convert filled primitives to lines.
	S.Wireframe = (mode == GL_LINE);
}
void	srglRasterPos2f(GLfloat x, GLfloat y)		{}
void	srglDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid* data)	{}
void	srglLockArraysEXT(GLint first, GLsizei count)	{}
void	srglUnlockArraysEXT()				{}
