/*
 * gl.c — OpenGL ES 1.1 C bindings: each standard gl* function forwards to the OS's
 * own flat GL entry point (base 0x08000000, Thumb). The flat entry points do the
 * current-context (TLS) lookup + dispatch to the OS GL renderer, so these are
 * drop-in GLES 1.1.
 * Softfp: float args pass in core registers, matching our soft-float build.
 */
#include "gl.h"

#define FN(a) ((a) | 1u)

/* OS flat GL entry points */
#define A_glClear         0x0804652au
#define A_glClearColor    0x08046548u
#define A_glClearDepthf   0x0804656eu
#define A_glClearStencil  0x0804658cu
#define A_glColorMask     0x084e501au
#define A_glEnable        0x083ce0f0u
#define A_glDisable       0x083ce0d0u
#define A_glViewport      0x08068a04u
#define A_glMatrixMode    0x080689e4u
#define A_glLoadIdentity  0x080582c0u
#define A_glLoadMatrixf   0x080582dcu
#define A_glOrthof        0x084e52c0u
#define A_glRotatef       0x084e54dcu
#define A_glTranslatef    0x084e552cu
#define A_glScalef        0x084e5504u
#define A_glPixelStorei   0x08082faau
#define A_glTexParameteri 0x083d5a2au

void glClear(GLbitfield mask)
{ ((void (*)(GLbitfield))FN(A_glClear))(mask); }

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{ ((void (*)(GLclampf, GLclampf, GLclampf, GLclampf))FN(A_glClearColor))(r, g, b, a); }

void glClearDepthf(GLclampf depth)
{ ((void (*)(GLclampf))FN(A_glClearDepthf))(depth); }

void glClearStencil(GLint s)
{ ((void (*)(GLint))FN(A_glClearStencil))(s); }

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{ ((void (*)(GLboolean, GLboolean, GLboolean, GLboolean))FN(A_glColorMask))(r, g, b, a); }

void glEnable(GLenum cap)
{ ((void (*)(GLenum))FN(A_glEnable))(cap); }

void glDisable(GLenum cap)
{ ((void (*)(GLenum))FN(A_glDisable))(cap); }

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{ ((void (*)(GLint, GLint, GLsizei, GLsizei))FN(A_glViewport))(x, y, w, h); }

void glMatrixMode(GLenum mode)
{ ((void (*)(GLenum))FN(A_glMatrixMode))(mode); }

void glLoadIdentity(void)
{ ((void (*)(void))FN(A_glLoadIdentity))(); }

void glLoadMatrixf(const GLfloat *m)
{ ((void (*)(const GLfloat *))FN(A_glLoadMatrixf))(m); }

/* glOrthof — implemented in C like glFrustumf. The OS's 6-float entry point uses a
 * softfp arg layout the flat forwarder can't satisfy (args spill past r0-r3), so the
 * forwarder produced a no-op/garbage projection: 2D content then drew through whatever
 * frustum the previous draw left set (slanted geometry, NaN coords, GPU faults). Build
 * the standard orthographic matrix and glMultMatrixf it onto the current (identity). */
void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
    GLfloat m[16];
    GLfloat rl = r - l, tb = t - b, fn = f - n;
    m[0]=2.f/rl;     m[1]=0;          m[2]=0;          m[3]=0;
    m[4]=0;          m[5]=2.f/tb;     m[6]=0;          m[7]=0;
    m[8]=0;          m[9]=0;          m[10]=-2.f/fn;   m[11]=0;
    m[12]=-(r+l)/rl; m[13]=-(t+b)/tb; m[14]=-(f+n)/fn; m[15]=1.f;
    glMultMatrixf(m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{ ((void (*)(GLfloat, GLfloat, GLfloat, GLfloat))FN(A_glRotatef))(angle, x, y, z); }

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{ ((void (*)(GLfloat, GLfloat, GLfloat))FN(A_glTranslatef))(x, y, z); }

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{ ((void (*)(GLfloat, GLfloat, GLfloat))FN(A_glScalef))(x, y, z); }

void glPixelStorei(GLenum pname, GLint param)
{ ((void (*)(GLenum, GLint))FN(A_glPixelStorei))(pname, param); }

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{ ((void (*)(GLenum, GLenum, GLint))FN(A_glTexParameteri))(target, pname, param); }

/* OS GL renderer methods. These appear to be non-static C++ methods:
 * `this` is the renderer object (used for post-draw bookkeeping + array enables), so
 * we must pass the real gc — the same one the flat gl* forwarders dispatch through:
 * gc = *(*(tls_get() + 12) + 0x24). */
#define A_ENV_GETTLS             0x0841c1b4u   /* OS TLS-value getter (0-arg)       */
/* The GL_VERTEX_ARRAY case at ctx+0x1490 (the gc's array-enables word): 0x08483436 does `orr r3,r2,#1`
 * (sets the array bit = ENABLE); 0x084835a8 does `bic r3,r2,#1` (clears it =
 * DISABLE). Both take the array enum in r1 (this/r0 ignored). 0x08483380 — the
 * earlier guess — is glDeleteBuffers (count + id-array; it crashed on the enum). */
#define A_m_glEnableClientState  0x08483436u
#define A_m_glDisableClientState 0x084835a8u
#define A_m_glVertexPointer      0x084822dcu   /* writes ctx+0xd08 */
#define A_m_glColorPointer       0x08481d90u   /* writes ctx+0xdf0 */
#define A_m_glDrawArrays         0x084811ceu   /* mode 7/8 quad->tri switch */

static void *gl_current_gc(void)
{
    void *tls = ((void *(*)(void))FN(A_ENV_GETTLS))();
    void *ctx;
    if (!tls) return 0;
    ctx = *(void **)((char *)tls + 12);
    if (!ctx) return 0;
    return *(void **)((char *)ctx + 0x24);
}

void glEnableClientState(GLenum array)
{ ((void (*)(void *, GLenum))FN(A_m_glEnableClientState))(gl_current_gc(), array); }

void glDisableClientState(GLenum array)
{ ((void (*)(void *, GLenum))FN(A_m_glDisableClientState))(gl_current_gc(), array); }

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{ ((void (*)(void *, GLint, GLenum, GLsizei, const GLvoid *))FN(A_m_glVertexPointer))(gl_current_gc(), size, type, stride, ptr); }

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{ ((void (*)(void *, GLint, GLenum, GLsizei, const GLvoid *))FN(A_m_glColorPointer))(gl_current_gc(), size, type, stride, ptr); }

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{ ((void (*)(void *, GLenum, GLint, GLsizei))FN(A_m_glDrawArrays))(gl_current_gc(), mode, first, count); }

/* VBO entry points — flat (no `this`); found at the buffer-object setup path. */
#define A_glGenBuffers 0x08076e74u
#define A_glBindBuffer 0x08076da8u
#define A_glBufferData 0x08076dcau

void glGenBuffers(GLsizei n, GLuint *buffers)
{ ((void (*)(GLsizei, GLuint *))FN(A_glGenBuffers))(n, buffers); }

void glBindBuffer(GLenum target, GLuint buffer)
{ ((void (*)(GLenum, GLuint))FN(A_glBindBuffer))(target, buffer); }

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{ ((void (*)(GLenum, GLsizeiptr, const GLvoid *, GLenum))FN(A_glBufferData))(target, size, data, usage); }

/* ---- Extended GLES 1.1
 * Called like the vertex methods: the renderer fetches the real gc via the OS
 * TLS-value getter and ignores `this`, so we pass the current gc and the GL args
 * follow. Verified against each function's signature constants
 * (blend factors, GL_FLAT, GL_FRONT, the depth-func range, the matrix-stack fields). */
#define A_m_glColor4ub       0x084807f4u   /* checks 4 ubyte->float current color    */
#define A_m_glNormalPointer  0x08482184u   /* (type,stride,ptr) normal array          */
#define A_m_glTexCoordPointer 0x08482574u  /* (size,type,stride,ptr) texcoord array   */
#define A_m_glBlendFunc      0x08480d64u   /* (sfactor,dfactor); cmps blend tokens    */
#define A_m_glDepthFunc      0x08480eb6u   /* (func); range GL_NEVER..GL_ALWAYS        */
#define A_m_glShadeModel     0x0848141eu   /* (mode); GL_FLAT/GL_SMOOTH (0x1d00/01)    */
#define A_m_glCullFace       0x08480868u   /* (mode); GL_FRONT/BACK (0x404..)          */
#define A_m_glLineWidth      0x08480f56u   /* (width float); vcmpe >0                  */
#define A_m_glBindTexture    0x084817ccu   /* (target,texture)                         */
#define A_m_glTexImage2D     0x0848146cu   /* 9-arg texture upload                     */
#define A_m_glGenTextures    0x084819a8u   /* (n,textures)                             */
#define A_m_glDeleteTextures 0x08482468u   /* (n,textures)                             */
#define A_m_glGetIntegerv    0x084819deu   /* (pname,params)                           */
#define A_m_glTexEnvi        0x08484b24u   /* (target,pname,param)                     */
#define A_m_glPushMatrix     0x08481408u   /* tail-calls the push handler (ctx+0x5e8)  */
#define A_m_glPopMatrix      0x08480feeu   /* matrix stack (ctx+0x5ec)                 */

#define GC() gl_current_gc()

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{ ((void (*)(void *, GLubyte, GLubyte, GLubyte, GLubyte))FN(A_m_glColor4ub))(GC(), r, g, b, a); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)   /* convenience: -> glColor4ub */
{
    GLubyte cr = (GLubyte)(r <= 0.f ? 0 : r >= 1.f ? 255 : (int)(r * 255.f + 0.5f));
    GLubyte cg = (GLubyte)(g <= 0.f ? 0 : g >= 1.f ? 255 : (int)(g * 255.f + 0.5f));
    GLubyte cb = (GLubyte)(b <= 0.f ? 0 : b >= 1.f ? 255 : (int)(b * 255.f + 0.5f));
    GLubyte ca = (GLubyte)(a <= 0.f ? 0 : a >= 1.f ? 255 : (int)(a * 255.f + 0.5f));
    glColor4ub(cr, cg, cb, ca);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{ ((void (*)(void *, GLenum, GLsizei, const GLvoid *))FN(A_m_glNormalPointer))(GC(), type, stride, ptr); }

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{ ((void (*)(void *, GLint, GLenum, GLsizei, const GLvoid *))FN(A_m_glTexCoordPointer))(GC(), size, type, stride, ptr); }

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{ ((void (*)(void *, GLenum, GLenum))FN(A_m_glBlendFunc))(GC(), sfactor, dfactor); }

void glDepthFunc(GLenum func)
{ ((void (*)(void *, GLenum))FN(A_m_glDepthFunc))(GC(), func); }

void glShadeModel(GLenum mode)
{ ((void (*)(void *, GLenum))FN(A_m_glShadeModel))(GC(), mode); }

void glCullFace(GLenum mode)
{ ((void (*)(void *, GLenum))FN(A_m_glCullFace))(GC(), mode); }

void glLineWidth(GLfloat width)
{ ((void (*)(void *, GLfloat))FN(A_m_glLineWidth))(GC(), width); }

void glBindTexture(GLenum target, GLuint texture)
{ ((void (*)(void *, GLenum, GLuint))FN(A_m_glBindTexture))(GC(), target, texture); }

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{ ((void (*)(void *, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *))
   FN(A_m_glTexImage2D))(GC(), target, level, internalformat, width, height, border, format, type, pixels); }

void glGenTextures(GLsizei n, GLuint *textures)
{ ((void (*)(void *, GLsizei, GLuint *))FN(A_m_glGenTextures))(GC(), n, textures); }

void glDeleteTextures(GLsizei n, const GLuint *textures)
{ ((void (*)(void *, GLsizei, const GLuint *))FN(A_m_glDeleteTextures))(GC(), n, textures); }

void glGetIntegerv(GLenum pname, GLint *params)
{ ((void (*)(void *, GLenum, GLint *))FN(A_m_glGetIntegerv))(GC(), pname, params); }

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{ ((void (*)(void *, GLenum, GLenum, GLint))FN(A_m_glTexEnvi))(GC(), target, pname, param); }

void glPushMatrix(void)
{ ((void (*)(void *))FN(A_m_glPushMatrix))(GC()); }

void glPopMatrix(void)
{ ((void (*)(void *))FN(A_m_glPopMatrix))(GC()); }

/* glBufferSubData at 0x08482030 (target 0x8892/0x8893,
 * offset/size >=0, bound buffer @gc+0x14b0). An earlier guess (0x08481004) was wrong and
 * crashed — it corrupted the VBO -> draw fault. */
#define A_m_glBufferSubData 0x08482030u
void glBufferSubData(GLenum target, GLsizeiptr offset, GLsizeiptr size, const GLvoid *data)
{ ((void (*)(void *, GLenum, GLsizeiptr, GLsizeiptr, const GLvoid *))FN(A_m_glBufferSubData))(GC(), target, offset, size, data); }

/* batch 2 — glDepthMask writes the depth-write boolean at
 * ctx+0x354 (it's the function right after glDepthFunc); glScissor takes 4 ints;
 * glActiveTexture checks GL_TEXTURE0 (0x84C0); glClientActiveTexture mirrors it. */
#define A_m_glDepthMask          0x08480efeu
#define A_m_glScissor            0x08484a64u
#define A_m_glActiveTexture      0x08481ffau
#define A_m_glClientActiveTexture 0x0848365cu

void glDepthMask(GLboolean flag)
{ ((void (*)(void *, GLboolean))FN(A_m_glDepthMask))(GC(), flag); }

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{ ((void (*)(void *, GLint, GLint, GLsizei, GLsizei))FN(A_m_glScissor))(GC(), x, y, width, height); }

void glActiveTexture(GLenum texture)
{ ((void (*)(void *, GLenum))FN(A_m_glActiveTexture))(GC(), texture); }

void glClientActiveTexture(GLenum texture)
{ ((void (*)(void *, GLenum))FN(A_m_glClientActiveTexture))(GC(), texture); }

/* batch 3 — glMultMatrixf is the 3rd matrix method (after glLoadMatrixf/glLoadMatrixx;
 * the third matrix-multiply entry); glDeleteBuffers takes a count + names and works
 * on the ctx+0x14ac buffer table (came over in the same 5g batch as glEnableClientState). */
#define A_m_glMultMatrixf    0x08481c94u
#define A_m_glDeleteBuffers  0x08483380u

void glMultMatrixf(const GLfloat *m)
{ ((void (*)(void *, const GLfloat *))FN(A_m_glMultMatrixf))(GC(), m); }

/* glLoadMatrixx (0x08481aec) — the fixed-point twin of glLoadMatrixf, between it and
 * glMultMatrixf; like glLoadMatrixf but takes a 16.16 fixed-point matrix and loads it
 * into the current matrix. Device-verified. (glDrawTexxOES at 0x0848192c was also identified but faults
 * without GL_TEXTURE_CROP_RECT_OES, which needs glTexParameteriv — left unbound.) */
#define A_m_glLoadMatrixx  0x08481aecu

void glLoadMatrixx(const GLfixed *m)
{ ((void (*)(void *, const GLfixed *))FN(A_m_glLoadMatrixx))(GC(), m); }

/* The lighting/getter cluster — these sit together in the OS GL renderer vtable at
 * 0x087e7798; the slot positions were pinned on the 7g by disasm. glGetError
 * returns and clears the pending error (the error word is at
 * ctx+0x5b8); glLightf takes the scalar light params (pnames 0x1205..0x1209); the *fv
 * variants take a float-vector pointer. */
#define A_m_glGetError    0x084808acu
#define A_m_glGetFloatv   0x08480f38u
#define A_m_glLightf      0x084846d4u
#define A_m_glLightfv     0x08484a14u
#define A_m_glMaterialfv  0x084812eau

GLenum glGetError(void)
{ return ((GLenum (*)(void *))FN(A_m_glGetError))(GC()); }

void glGetFloatv(GLenum pname, GLfloat *params)
{ ((void (*)(void *, GLenum, GLfloat *))FN(A_m_glGetFloatv))(GC(), pname, params); }

void glLightf(GLenum light, GLenum pname, GLfloat param)
{ ((void (*)(void *, GLenum, GLenum, GLfloat))FN(A_m_glLightf))(GC(), light, pname, param); }

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{ ((void (*)(void *, GLenum, GLenum, const GLfloat *))FN(A_m_glLightfv))(GC(), light, pname, params); }

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{ ((void (*)(void *, GLenum, GLenum, const GLfloat *))FN(A_m_glMaterialfv))(GC(), face, pname, params); }

/* glTexEnvfv / glTexEnviv (vtable finds; both check GL_TEXTURE_ENV 0x2300). */
#define A_m_glTexEnvfv 0x084808c4u
#define A_m_glTexEnviv 0x08480a80u

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{ ((void (*)(void *, GLenum, GLenum, const GLfloat *))FN(A_m_glTexEnvfv))(GC(), target, pname, params); }

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{ ((void (*)(void *, GLenum, GLenum, const GLint *))FN(A_m_glTexEnviv))(GC(), target, pname, params); }

#define A_OGL_GETTLS       0x083c2df0u   /* OS TLS getter -> the real GL context         */
#define A_FT_GETINSTANCE   0x083ec870u   /* GPU flush-thread singleton getter            */
#define A_GPU3DCMD_CTOR    0x083fe878u   /* GPU draw-command constructor                 */
#define A_Q_ENQUEUE        0x083fe7f8u   /* command-queue enqueue                        */
#define A_OS_INITSEM       0x0842b194u
#define A_OS_POSTSEM       0x08427a60u
#define A_OS_WAITSEM       0x0842b1a0u
#define A_OS_DESTROYSEM    0x084268f0u
#define A_OS_SETERROR      0x08408ca4u   /* set-error(gc, code): if gc+0x5b8==0 set it   */
#define A_EGL_WAITGL       0x08058120u   /* GL wait/finish (SW-render sync; HW no-ops)   */

/* set-error(gc, code): records the first GL error so glGetError() can report it.
 * GL_INVALID_ENUM=0x500, GL_INVALID_VALUE=0x501, GL_INVALID_OPERATION=0x502. */
static void gl_set_error(void *gc, uint32_t code)
{ ((void (*)(void *, uint32_t))FN(A_OS_SETERROR))(gc, code); }

static int s_glde_native = 1;   /* native HW indexed draw on by default */
void hb_gl_drawelements_native(int on) { s_glde_native = on; }

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (s_glde_native) {
        char *gc = (char *)((void *(*)(void))FN(A_OGL_GETTLS))();   /* the worker's gc */
        uint32_t sem[16];
        uint8_t  cmd[0x40];
        void *ft;
        if (!gc) return;
        /* input validation — including the set-error calls so a bad
         * call is reportable via glGetError. */
        if (count < 0) { gl_set_error(gc, 0x501u); return; }                 /* GL_INVALID_VALUE */
        if (mode > GL_TRIANGLE_FAN) { gl_set_error(gc, 0x500u); return; }    /* GL_INVALID_ENUM  */
        if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT) {
            gl_set_error(gc, 0x500u); return;                                /* GL_INVALID_ENUM  */
        }
        if (count == 0) return;   /* OS returns with no error here */
        if ((*(uint32_t *)(gc + 0x1490) & 0x101u) == 0) return;    /* VARRAY_PROVOKE gate */

        /* Point-primitive state: only the GL_POINTS<->non-points transition touches it.
         * On transition, set the point-prim flag @gc+0x14bc and OR the format-dirty bits
         * (0xa000) into the dirty-mask @gc+0x5d4 so the next draw rebuilds the vertex
         * format. Without this, mixing GL_POINTS after triangles leaves a stale format. */
        {
            int *pp = (int *)(gc + 0x14bc);
            if (mode == GL_POINTS) {            /* GL_POINTS == 0 */
                if (*pp == 0) { *pp = 1; *(uint32_t *)(gc + 0x5d4) |= 0xa000u; }
            } else {
                if (*pp != 0) { *pp = 0; *(uint32_t *)(gc + 0x5d4) |= 0xa000u; }
            }
        }

        ((void (*)(void *))FN(A_OS_INITSEM))(sem);
        ((void (*)(void *))FN(A_GPU3DCMD_CTOR))(cmd);
        *(void   **)(cmd + 0x00) = gc;
        *(uint32_t *)(cmd + 0x04) = (uint32_t)mode;
        *(uint32_t *)(cmd + 0x08) = (uint32_t)count;
        *(uint32_t *)(cmd + 0x0c) = (uint32_t)type;
        *(const void **)(cmd + 0x10) = indices;
        *(uint32_t *)(cmd + 0x18) = 4u;          /* kind = 4, the value the OS indexed-draw enqueue writes */
        *(void   **)(cmd + 0x1c) = sem;          /* completion semaphore */
        ft = ((void *(*)(void))FN(A_FT_GETINSTANCE))();
        ((void (*)(void *, void *))FN(A_Q_ENQUEUE))((char *)ft + 0x14, cmd);
        ((void (*)(void *))FN(A_OS_POSTSEM))((char *)ft + 0x34);    /* wake flush thread */
        ((void (*)(void *))FN(A_OS_WAITSEM))(sem);                  /* block until drawn */
        ((void (*)(void *))FN(A_OS_DESTROYSEM))(sem);

        /* crash workaround: for points/lines (mode < GL_TRIANGLES) OR the ISPCTL bit into
         * the HW-context TA state (TA-control/3D-state word @gc+0xdc |= 1). Found empirically:
         * without it, GL_POINTS draws corrupt/panic over time while triangles (mode>=4) stay
         * clean. We OR just that one bit and leave the rest of the state word
         * alone — we don't know what else it holds. */
        if (mode < GL_TRIANGLES) {
            *(uint32_t *)(gc + 0xdc) |= 1u;
        }
        /* Bump the renderer's draw counter at this+4 (the renderer = gl_current_gc(),
         * distinct from gc). */
        {
            char *self = (char *)gl_current_gc();
            if (self) *(uint32_t *)(self + 4) += 1u;
        }
        return;
    }
    (void)mode; (void)count; (void)type; (void)indices;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{ ((void (*)(void *, GLsizei, const GLuint *))FN(A_m_glDeleteBuffers))(GC(), n, buffers); }

/* glMaterialf — keyed off its GL_EMISSION (0x1600) reference (scalar material
 * param, e.g. GL_SHININESS). The vector glMaterialfv/glLightfv/glLightf live in the
 * same lighting code but are float-array-heavy with no distinctive int immediates
 * and are left unbound. */
#define A_m_glMaterialf      0x08480fc2u

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{ ((void (*)(void *, GLenum, GLenum, GLfloat))FN(A_m_glMaterialf))(GC(), face, pname, param); }

#define GC_NORMAL_OFF   0x160u   /* current normal attribute (x,y,z floats) */
#define GC_ALPHAREF_OFF 0x3d0u   /* alpha-func reference field (live
                                  * glBlendFunc writes the blend factor field at gc+0x3d4,
                                  * so this prior field is 0x3d0). */
#define GC_DIRTY_OFF    0x5d4u   /* context dirty-mask (confirmed via live glDepthFunc/
                                  * glBlendFunc/glDrawArrays — NOT 0x1500, which was wrong
                                  * and silently corrupted the field there) */
#define GL_DIRTY_ISPTSP_TSPOBJ 0x3u   /* ISP-TSP | TSP-object dirty bits */

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    char *gc = (char *)gl_current_gc();
    if (!gc) return;
    /* sets the current normal; the OS sets no dirty flag here (read fresh in T&L) */
    *(GLfloat *)(gc + GC_NORMAL_OFF + 0) = nx;
    *(GLfloat *)(gc + GC_NORMAL_OFF + 4) = ny;
    *(GLfloat *)(gc + GC_NORMAL_OFF + 8) = nz;
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    char *gc = (char *)gl_current_gc();
    GLfloat c;
    GLuint r, packed, *p;
    if (!gc) return;
    if (func < GL_NEVER || func > GL_ALWAYS) { gl_set_error(gc, 0x500u); return; }  /* GL_INVALID_ENUM */
    c = ref < 0.f ? 0.f : ref > 1.f ? 1.f : ref;
    r = (GLuint)(c * 256.0f);
    if (r & 0x100u) r = 0xFFu;                          /* post-clamp (matches OS) */
    packed = (r << 21) | ((func - GL_NEVER) << 29);     /* alpha-ref / alpha-cmp field shifts */
    p = (GLuint *)(gc + GC_ALPHAREF_OFF);
    if (*p != packed) {
        *p = packed;
        *(GLuint *)(gc + GC_DIRTY_OFF) |= GL_DIRTY_ISPTSP_TSPOBJ;
    }
}

/* glFrustumf — implemented in C. Multiplies the current matrix by the standard
 * perspective frustum, column-major, then glMultMatrixf's it onto the stack. */
void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
    GLfloat m[16];
    GLfloat rl = r - l, tb = t - b, fn = f - n;
    m[0]=2.f*n/rl; m[1]=0;        m[2]=0;            m[3]=0;
    m[4]=0;        m[5]=2.f*n/tb; m[6]=0;            m[7]=0;
    m[8]=(r+l)/rl; m[9]=(t+b)/tb; m[10]=-(f+n)/fn;  m[11]=-1.f;
    m[12]=0;       m[13]=0;       m[14]=-2.f*f*n/fn; m[15]=0;
    glMultMatrixf(m);
}

/* glGetString — C impl returning static GLES 1.1 identification strings. VERSION 
 * must contain "1.1" for engine capability checks. EXTENSIONS is empty on purpose 
 * (no unverified promises). */
const GLubyte *glGetString(GLenum name)
{
    switch (name) {
        case GL_VENDOR:     return (const GLubyte *)"iPod";
        case GL_RENDERER:   return (const GLubyte *)"iPod";
        case GL_VERSION:    return (const GLubyte *)"OpenGL ES 1.1";
        case GL_EXTENSIONS: return (const GLubyte *)"";
        default:            return (const GLubyte *)0;
    }
}

/* glCompressedTexImage2D — present OS renderer method. Compressed/palettised
 * texture upload (PVRTC etc.); ABI is the same gc-as-this method pattern. */
#define A_m_glCompressedTexImage2D 0x08483794u
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid *data)
{ ((void (*)(void *, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *))
    FN(A_m_glCompressedTexImage2D))(GC(), target, level, internalformat, width, height, border, imageSize, data); }

#define A_TEX_REMOVERESIDENT 0x08106decu   /* mark a texture non-resident */
#define GL_TEXTURE_2D_TOK    0x0DE1u
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid *pixels)
{
    char *gc = (char *)((void *(*)(void))FN(A_OGL_GETTLS))();
    char *tex, *mip, *fmt; uint8_t *pui8;
    uint32_t texW, texH, bpp, dstStride, active; GLsizei r;
    (void)type;
    if (!gc || !pixels) return;
    if (target != GL_TEXTURE_2D_TOK) { gl_set_error(gc, 0x500u); return; }   /* GL_INVALID_ENUM */
    if (level < 0 || level > 11 || width < 0 || height < 0) { gl_set_error(gc, 0x501u); return; }

    active = *(uint32_t *)(gc + 0x400);                  /* active texture unit */
    tex    = *(char **)(gc + 0x660 + active * 8u);       /* bound 2D texture for that unit */
    if (!tex) { gl_set_error(gc, 0x502u); return; }
    mip   = tex + 0x30u + (uint32_t)level * 0x20u;
    texW  = *(uint32_t *)(mip + 0x04);
    texH  = *(uint32_t *)(mip + 0x08);
    fmt   = *(char **)(mip + 0x1c);                      /* texture format descriptor */
    pui8  = *(uint8_t **)(mip + 0x00);                   /* linear pixel store (linear store) */
    if (!fmt) { gl_set_error(gc, 0x502u); return; }
    bpp   = (*(uint32_t *)fmt + 7u) >> 3;                /* bytes per texel */

    /* sub-rect range check: the sub-rect must lie within the level */
    if (xoffset < 0 || yoffset < 0 ||
        (uint32_t)(xoffset + width) > texW || (uint32_t)(yoffset + height) > texH) {
        gl_set_error(gc, 0x501u); return;               /* GL_INVALID_VALUE */
    }
    if (width == 0 || height == 0) return;

    /* full-level re-spec -> reuse the present glTexImage2D (residency + conversion safe) */
    if (xoffset == 0 && yoffset == 0 && (uint32_t)width == texW && (uint32_t)height == texH) {
        glTexImage2D(target, level, (GLint)format, width, height, 0, format, type, pixels);
        return;
    }
    if (!pui8) { gl_set_error(gc, 0x502u); return; }     /* buffer freed -> needs readback (TODO) */

    dstStride = texW * bpp;
    {
        /* source is GL_RGBA / GL_UNSIGNED_BYTE = 4 bytes/texel; the texture's STORED
         * format (bpp) may be narrower (GPU downconverts RGBA8888 -> RGBA4444 = 2bpp),
         * so convert per-texel rather than raw-copy (raw 8888->4444 = red/blue stripes). */
        const uint8_t *src = (const uint8_t *)pixels;
        uint32_t srcRow = (uint32_t)width * 4u;
        GLsizei cc;
        for (r = 0; r < height; r++) {
            uint8_t *d = pui8 + (uint32_t)(yoffset + r) * dstStride + (uint32_t)xoffset * bpp;
            const uint8_t *s = src + (uint32_t)r * srcRow;
            if (bpp == 4u) {                              /* 8888 store: straight copy */
                uint32_t n = (uint32_t)width * 4u;
                while (n--) *d++ = *s++;
            } else if (bpp == 2u) {                       /* ARGB4444 store:
                                                           * [15:12]=A [11:8]=R [7:4]=G [3:0]=B) */
                for (cc = 0; cc < width; cc++) {
                    uint32_t R=s[0],G=s[1],B=s[2],A=s[3];
                    uint16_t p = (uint16_t)(((A>>4)<<12) | ((R>>4)<<8) | ((G>>4)<<4) | (B>>4));
                    d[0]=(uint8_t)(p & 0xff); d[1]=(uint8_t)(p >> 8);
                    d += 2; s += 4;
                }
            } else { gl_set_error(gc, 0x500u); return; }  /* unsupported store bpp */
        }
    }
    ((void (*)(void *, void *))FN(A_TEX_REMOVERESIDENT))(gc, tex);    /* re-twiddle on next use */
}

#define GC_READPARAMS_OFF 0x1578u
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *pixels)
{
    char *gc = (char *)((void *(*)(void))FN(A_OGL_GETTLS))();
    char *params; const uint8_t *src; uint32_t srcStride, srcH; uint8_t *dst; GLsizei r, c;  /* read-surface params */
    if (!gc || !pixels) return;
    if (width < 0 || height < 0) { gl_set_error(gc, 0x501u); return; }       /* GL_INVALID_VALUE */
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {                     /* first cut */
        gl_set_error(gc, 0x500u); return;                                   /* GL_INVALID_ENUM */
    }
    ((void (*)(void))FN(A_EGL_WAITGL))();           /* flush + finish rendering to the surface */
    params    = gc + GC_READPARAMS_OFF;
    srcH      = *(uint32_t *)(params + 0x08);
    srcStride = *(uint32_t *)(params + 0x10);
    src       = *(const uint8_t **)(params + 0x14);
    if (!src || !srcStride) return;
    dst = (uint8_t *)pixels;
    for (r = 0; r < height; r++) {
        int gy   = y + r;                            /* GL row, counted from the bottom */
        int srow = (int)srcH - 1 - gy;               /* corresponding top-down surface row */
        const uint8_t *s;
        uint8_t *d = dst + (uint32_t)r * (uint32_t)width * 4u;
        if (srow < 0 || srow >= (int)srcH) continue;
        s = src + (uint32_t)srow * srcStride + (uint32_t)x * 4u;
        /* surface is BGRA (a blue clear reads back byte-reversed) ->
         * emit RGBA by swapping R and B */
        for (c = 0; c < width; c++) { d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=s[3]; d+=4; s+=4; }
    }
}
