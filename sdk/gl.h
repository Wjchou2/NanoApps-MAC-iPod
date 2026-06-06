/*
 * gl.h — OpenGL ES 1.1 C bindings for the iPod nano GPU.
 *
 * Standard GLES 1.1 function names (no hb_ prefix) so app code is portable GLES.
 * Each function forwards to the OS's own flat GL C entry point. The GL context 
 * must be current first — render inside a GL view's draw callback.
 *
 * This binding is grown incrementally as entry points are identified and
 * device-verified. All functions below are tested. Float args are softfp (they
 * arrive in core registers), so a soft-float caller is correct.
 *
 */
#ifndef HB_GLES_GL_H
#define HB_GLES_GL_H

#include <stdint.h>

typedef unsigned int   GLenum;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef unsigned int   GLuint;
typedef int            GLsizei;
typedef int            GLsizeiptr;
typedef unsigned char  GLboolean;
typedef unsigned char  GLubyte;
typedef float          GLfloat;
typedef float          GLclampf;
typedef int            GLfixed;
typedef void           GLvoid;

/* tokens used by the bound subset */
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_FALSE              0
#define GL_TRUE               1
#define GL_DEPTH_TEST         0x0B71
#define GL_CULL_FACE          0x0B44
#define GL_BLEND              0x0BE2
#define GL_TEXTURE_2D         0x0DE1
#define GL_LIGHTING           0x0B50
#define GL_MODELVIEW          0x1700
#define GL_PROJECTION         0x1701
#define GL_TEXTURE            0x1702
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_BYTE               0x1400
#define GL_UNSIGNED_BYTE      0x1401
#define GL_SHORT              0x1402
#define GL_UNSIGNED_SHORT     0x1403
#define GL_FLOAT              0x1406
#define GL_FIXED              0x140C
#define GL_POINTS             0x0000
#define GL_LINES              0x0001
#define GL_TRIANGLES          0x0004
#define GL_TRIANGLE_STRIP     0x0005
#define GL_TRIANGLE_FAN       0x0006
#define GL_VERTEX_ARRAY       0x8074
#define GL_NORMAL_ARRAY       0x8075
#define GL_COLOR_ARRAY        0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_ARRAY_BUFFER       0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW        0x88E4
#define GL_DYNAMIC_DRAW       0x88E8

/* blend factors */
#define GL_ZERO                0x0000
#define GL_ONE                 0x0001
#define GL_SRC_COLOR           0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR           0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE  0x0308
/* depth/compare funcs */
#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207
/* shade model / cull / winding */
#define GL_FLAT            0x1D00
#define GL_SMOOTH          0x1D01
#define GL_FRONT           0x0404
#define GL_BACK            0x0405
#define GL_FRONT_AND_BACK  0x0408
#define GL_CW              0x0900
#define GL_CCW             0x0901
/* texture formats / env */
#define GL_ALPHA            0x1906
#define GL_RGB              0x1907
#define GL_RGBA             0x1908
#define GL_LUMINANCE        0x1909
#define GL_LUMINANCE_ALPHA  0x190A
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_SHORT_5_6_5   0x8363
#define GL_TEXTURE_ENV      0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE         0x2100
#define GL_DECAL            0x2101
#define GL_REPLACE          0x1E01
#define GL_ADD              0x0104
#define GL_TEXTURE_WRAP_S   0x2802
#define GL_TEXTURE_WRAP_T   0x2803
#define GL_REPEAT           0x2901
#define GL_CLAMP_TO_EDGE    0x812F
/* error codes (glGetError) */
#define GL_NO_ERROR          0x0000
#define GL_INVALID_ENUM      0x0500
#define GL_INVALID_VALUE     0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY     0x0505
/* glGetFloatv pnames */
#define GL_MODELVIEW_MATRIX  0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
/* scalar light pnames (glLightf) */
#define GL_SPOT_EXPONENT        0x1205
#define GL_SPOT_CUTOFF          0x1206
#define GL_CONSTANT_ATTENUATION 0x1207
#define GL_LINEAR_ATTENUATION   0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#define GL_SPOT_DIRECTION       0x1204
/* queryable state */
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_DEPTH_BITS       0x0D56
#define GL_MAX_LIGHTS       0x0D31
/* multitexture + scissor */
#define GL_TEXTURE0         0x84C0
#define GL_TEXTURE1         0x84C1
#define GL_SCISSOR_TEST     0x0C11
/* lighting / material params */
#define GL_LIGHT0           0x4000
#define GL_AMBIENT          0x1200
#define GL_DIFFUSE          0x1201
#define GL_SPECULAR         0x1202
#define GL_POSITION         0x1203
#define GL_EMISSION         0x1600
#define GL_SHININESS        0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602

#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

void glClear(GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepthf(GLclampf depth);
void glClearStencil(GLint s);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glLoadMatrixf(const GLfloat *m);
void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glPixelStorei(GLenum pname, GLint param);
void glTexParameteri(GLenum target, GLenum pname, GLint param);

/* Vertex-array drawing — the OS GL renderer methods (RE'd from the binary by
 * their context-offset/structure signatures). They fetch the current GLES context
 * via OGL_GetTLSValue (ignoring `this`), so they work whenever a context is current
 * (e.g. inside a GL view). Int/pointer args only — ABI-safe. */
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);

/* Vertex buffer objects — REQUIRED on this GPU: the GPU reads vertex data via
 * device addresses, so client-side arrays (a plain C pointer) can't be DMA'd. Upload
 * with glBufferData, bind, then glVertexPointer with a byte offset (cast to pointer). */
void glGenBuffers(GLsizei n, GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLsizeiptr offset, GLsizeiptr size, const GLvoid *data);

/* Extended GLES 1.1 (RE'd from the OS GL renderer; this driver is a SUBSET of
 * full GLES 1.1 — glColor4f is a wrapper over the native glColor4ub, and glNormal3f /
 * glFog* / glAlphaFunc / glFrontFace are not provided by this GPU). */
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glDepthFunc(GLenum func);
void glShadeModel(GLenum mode);
void glCullFace(GLenum mode);
void glLineWidth(GLfloat width);
void glPushMatrix(void);
void glPopMatrix(void);
void glDepthMask(GLboolean flag);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glActiveTexture(GLenum texture);
void glClientActiveTexture(GLenum texture);
void glMultMatrixf(const GLfloat *m);
void glLoadMatrixx(const GLfixed *m);   /* fixed-point (16.16) matrix load */
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glMaterialf(GLenum face, GLenum pname, GLfloat param);
void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);  /* C impl (OS GC'd it) */
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);   /* C impl: writes gc current normal */
void glAlphaFunc(GLenum func, GLclampf ref);           /* C impl: writes gc alpha-func-ref + dirty */

/* Textures */
void glGenTextures(GLsizei n, GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void glTexEnviv(GLenum target, GLenum pname, const GLint *params);

/* Lighting (found via the OS GL renderer vtable) */
void glLightf(GLenum light, GLenum pname, GLfloat param);
void glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);

/* Queries */
void glGetIntegerv(GLenum pname, GLint *params);
void glGetFloatv(GLenum pname, GLfloat *params);
GLenum glGetError(void);

/* glDrawElements — drop-in standard signature so indexed GL code ports directly.
 * Indexed drawing routes through the GPU draw worker
 * survives (flush-thread). Two paths, selected by hb_gl_drawelements_native():
 *   0 (default) = de-index into a VBO + glDrawArrays (expand indexed verts).
 *   1           = call the native flush-thread worker (true HW indexing) — enabled
 *                 once the worker address is located (HB_GL_DRAWELEMENTS_INTERNAL). */
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void hb_gl_drawelements_native(int on);   /* opt into the native worker path */

/* glGetString — C impl returning static GLES 1.1 strings (its
 * vtable slot is null), so we return static GLES 1.1 identification strings; engines
 * that query GL_VENDOR/RENDERER/VERSION get sane values. EXTENSIONS is intentionally
 * empty (no unverified extension promises). */
const GLubyte *glGetString(GLenum name);

/* glCompressedTexImage2D — present OS renderer method (compressed/palettised texture upload,
 * e.g. PVRTC). internalformat selects the compressed format; data is imageSize bytes. */
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid *data);

/* glTexSubImage2D — C impl. Origin updates (xoffset==
 * yoffset==0) route through the present glTexImage2D (exact for full-frame dynamic
 * textures); true offset sub-rects are not yet supported (error, no corruption). */
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid *pixels);

/* glReadPixels — C impl. Flushes
 * the GPU (eglWaitGL) then reads gc->sReadParams' linear surface with a Y-flip. First
 * cut: GL_RGBA / GL_UNSIGNED_BYTE from a 32bpp surface. */
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *pixels);


#endif /* HB_GLES_GL_H */
